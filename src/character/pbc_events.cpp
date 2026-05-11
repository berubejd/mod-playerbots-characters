#include "pbc_events.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_item_helpers.h"
#include "pbc_quest_helpers.h"
#include "Log.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Item.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "GridNotifiers.h"
#include "CellImpl.h"
#include "SharedDefines.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Chat.h"
#include "DBCStores.h"

#include <algorithm>
#include <regex>
#include <unordered_set>
#include <random>

// ---------------------------------------------------------------------------
// Narrator formatting helpers
// ---------------------------------------------------------------------------

std::string PBC_MakeEventLine(const std::string& text) { return "*" + text + "*"; }
std::string PBC_MakeHistLine(const std::string& text)  { return "Narrator: *" + text + "*"; }

// ---------------------------------------------------------------------------
// PBC_NotifyRealPlayersInGroup
//
// Sends the current event text as a system message to all real (non-bot)
// players in the same group as 'anchor'.  Called at event dispatch time
// when PBC.DisplayNarratorEvents is enabled, before bots process the event.
// For bot anchors (e.g. location events) the function walks the bot's group
// so real players in that group still receive the notification.
// ---------------------------------------------------------------------------
void PBC_NotifyRealPlayersInGroup(Player* anchor, const std::string& eventLine)
{
    if (!g_PBC_DisplayNarratorEvents) return;
    if (!PBC_PTR_VALID(anchor) || eventLine.empty()) return;

    auto sendTo = [&](Player* p)
    {
        if (!PBC_PTR_VALID(p) || !p->IsInWorld()) return;
        WorldSession* sess = p->GetSession();
        if (!PBC_PTR_VALID(sess) || sess->IsBot()) return;
        ChatHandler(sess).SendSysMessage(eventLine);
    };

    Group* grp = anchor->GetGroup();
    if (grp)
    {
        for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            sendTo(ref->GetSource());
    }
    else
    {
        sendTo(anchor);
    }
}

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

static bool IsBlacklisted(const std::string& msg)
{
    for (const auto& prefix : g_PBC_Blacklist)
        if (!prefix.empty() && msg.rfind(prefix, 0) == 0)
            return true;
    return false;
}


static std::vector<Player*> FindNearbyBots(WorldObject* source, float range = 60.0f)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(source) || !source->GetMap()) return bots;

    auto doWork = [&](Player* p)
    {
        if (!PBC_PTR_VALID(p) || p == source) return;
        if (!p->IsInWorld()) return;
        if (!p->GetSession() || !p->GetSession()->IsBot()) return;
        if (p->IsWithinDist(source, range))
            bots.push_back(p);
    };
    Acore::PlayerDistWorker<decltype(doWork)> worker(source, range, doWork);
    Cell::VisitObjects(source, worker, range);
    return bots;
}

std::vector<Player*> PBC_FindGroupBots(Player* player)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(player)) return bots;

    Group* grp = player->GetGroup();
    if (!grp) return bots;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || member == player) continue;
        if (!member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) continue;
        bots.push_back(member);
    }
    return bots;
}

// ---------------------------------------------------------------------------
// BuildCharacterEvent
//
// Rolls chance for each character in the provided list, builds snapshots for
// responders, collects GUIDs for silent characters, and returns a ready-to-push
// PBC_EventItem.  chatType and combatSkip are passed through.
// ---------------------------------------------------------------------------
static PBC_EventItem BuildCharacterEvent(const std::vector<Player*>& bots,
                                    const std::string& eventLine,
                                    const std::string& histLine,
                                    uint32_t chance,
                                    uint32_t chatType,
                                    bool canCreateEvents = false)
{
    PBC_EventItem ev;
    ev.type                  = PBC_EventType::Normal;
    ev.eventLine             = eventLine;
    ev.histLine              = histLine;
    ev.chatType              = chatType;
    ev.canCreateEvents       = canCreateEvents;

    PBC_RollBotsWithPenalty(ev, bots, chance, "event");

    return ev;
}

// ---------------------------------------------------------------------------
// PBC_DispatchGroupEvent
// ---------------------------------------------------------------------------

void PBC_DispatchGroupEvent(Player* anchor, const std::string& eventLine,
                             const std::string& histLine, uint32_t chance,
                             bool notifyRealPlayers)
{
    if (!PBC_PTR_VALID(anchor)) return;

    WorldSession* anchorSess = anchor->GetSession();
    bool anchorIsReal = PBC_PTR_VALID(anchorSess) && !anchorSess->IsBot();
    bool anchorIsBot  = PBC_PTR_VALID(anchorSess) && anchorSess->IsBot();

    if (!anchorIsReal && !PBC_BotIsGroupedWithRealPlayer(anchor))
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] DispatchGroupEvent: skipped — no real player in group (anchor={})", anchor->GetName());
        return;
    }

    if (notifyRealPlayers)
        PBC_NotifyRealPlayersInGroup(anchor, eventLine);

    auto bots = PBC_FindGroupBots(anchor);
    if (bots.empty() && !anchorIsBot) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] DispatchGroupEvent: anchor={} bots={} chance={}% event=\"{}\"",
                 anchor->GetName(), bots.size(), chance, eventLine);

    PBC_EventItem ev = BuildCharacterEvent(bots, eventLine, histLine, chance, CHAT_MSG_PARTY,
                                     /*canCreateEvents=*/true);

    // If the anchor is itself a bot (e.g. a bot leveling up), it was excluded
    // from PBC_FindGroupBots() but still needs to receive the histLine and any
    // responder replies in its own history.
    if (anchorIsBot)
        ev.silentCharGuids.push_back(anchor->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}


// ---------------------------------------------------------------------------
// HandleChatMessage  (shared for all chat types)
// ---------------------------------------------------------------------------

static void HandleChatMessage(Player* sender, uint32 type, const std::string& rawMsg,
                               Player* whisperTarget = nullptr)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(sender)) return;
    if (type == CHAT_MSG_AFK || type == CHAT_MSG_DND) return;
    if (IsBlacklisted(rawMsg)) return;

    const std::string msg = PBC_SanitizeChatMessage(rawMsg);
    std::string senderName = sender->GetName();

    // --- Whisper path ---
    if (type == CHAT_MSG_WHISPER)
    {
        if (!PBC_PTR_VALID(whisperTarget)
            || !whisperTarget->GetSession()
            || !whisperTarget->GetSession()->IsBot())
            return;

        PBC_DispatchWhisperEvent(sender, whisperTarget, msg);
        return;
    }

    // --- Say / Yell / Group / Raid ---
    bool senderIsBot = sender->GetSession() && sender->GetSession()->IsBot();

    // Bot-originated say/yell messages must not trigger new response events.
    if (senderIsBot)
    {
        return;
    }

    // canCreateEvents is true for group chat (party/raid), false for say/yell
    // to prevent infinite reply loops in public channels.
    bool isGroupChat = (type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER ||
                        type == CHAT_MSG_RAID  || type == CHAT_MSG_RAID_LEADER  ||
                        type == CHAT_MSG_RAID_WARNING);

    PBC_DispatchPartyMessageEvent(sender, msg, "", type, isGroupChat);
}

// ---------------------------------------------------------------------------
// PBC_PlayerEvents
// ---------------------------------------------------------------------------

PBC_PlayerEvents::PBC_PlayerEvents() : PlayerScript("PBC_PlayerEvents",
{
    PLAYERHOOK_CAN_PLAYER_USE_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
    PLAYERHOOK_ON_STORE_NEW_ITEM,
    PLAYERHOOK_ON_DUEL_END,
    PLAYERHOOK_ON_LEVEL_CHANGED,
    PLAYERHOOK_ON_CREATURE_KILL,
    PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
}) {}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg, Player* receiver)
{
    HandleChatMessage(player, type, msg, receiver);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg)
{
    HandleChatMessage(player, type, msg);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg, Group* /*group*/)
{
    HandleChatMessage(player, type, msg);
    return true;
}

// ---------------------------------------------------------------------------
// Bitmask of item classes that trigger loot events.
// Only weapons and armor by default — add more ITEM_CLASS_* values to enable
// additional item types (e.g. | (1u << ITEM_CLASS_CONSUMABLE)).
// ---------------------------------------------------------------------------
#define PBC_LOOT_EVENT_ITEM_CLASSES  ((1u << ITEM_CLASS_WEAPON) | (1u << ITEM_CLASS_ARMOR))

void PBC_PlayerEvents::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(item)) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;
    if (!(PBC_LOOT_EVENT_ITEM_CLASSES & (1u << tmpl->Class))) return;

    std::string itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine("The party has found " + phrase + " named " + itemName),
        PBC_MakeHistLine("The party acquired " + phrase + " named " + itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(winner) || !PBC_PTR_VALID(loser) || type != DUEL_WON) return;
    PBC_DispatchGroupEvent(winner,
        PBC_MakeEventLine(winner->GetName() + " just won the duel against " + loser->GetName()),
        PBC_MakeHistLine(winner->GetName() + " won the duel against " + loser->GetName()),
        g_PBC_ReplyChanceDuel);
}

void PBC_PlayerEvents::OnPlayerLevelChanged(Player* player, uint8 oldLevel)
{
    if (!g_PBC_Enable || !PBC_PTR_VALID(player)) return;

    // Only produce level-up events on every 5th level (5, 10, 15, …).
    if (player->GetLevel() % 5 != 0) return;

    WorldSession* sess = player->GetSession();
    bool anchorIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    bool anchorIsBot  = PBC_PTR_VALID(sess) && sess->IsBot();

    if (!anchorIsReal && !PBC_BotIsGroupedWithRealPlayer(player)) return;

    auto bots = PBC_FindGroupBots(player);
    if (bots.empty() && !anchorIsBot) return;

    static const char* levelUpEventPhrases[] = {
        " can feel their abilities growing stronger",
        " grows more powerful, their skills sharpened by experience",
        " has grown stronger through their trials and hardships",
        " feels a surge of power as their abilities improve",
        " has become more capable, their experience forging them anew",
    };
    static const char* levelUpHistPhrases[] = {
        " grew stronger",
        " became more powerful through experience",
        " emerged from their trials more capable than before",
        " felt their abilities sharpen and grow",
        " gained new strength and skill",
    };

    const std::string& name = player->GetName();
    int idx = std::uniform_int_distribution<int>(0, 4)(PBC_GetRNG());
    std::string eventLine = PBC_MakeEventLine(name + levelUpEventPhrases[idx]);
    std::string histLine  = PBC_MakeHistLine(name + levelUpHistPhrases[idx]);

    PBC_NotifyRealPlayersInGroup(player, eventLine);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerLevelChanged: player={} level={} bots={} chance={}%",
                 player->GetName(), player->GetLevel(), bots.size(), g_PBC_ReplyChanceLevelUp);

    PBC_EventItem ev = BuildCharacterEvent(bots, eventLine, histLine,
                                     g_PBC_ReplyChanceLevelUp, CHAT_MSG_PARTY,
                                     /*canCreateEvents=*/true);

    if (anchorIsBot)
        ev.silentCharGuids.push_back(player->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// IsSignificantKill
//
// Returns true for dungeon/raid bosses, world bosses, and rare-elite or
// higher-ranked creatures (unique named rares such as King Mosh).
// Plain ELITE rank (rank 1) is intentionally excluded — there are many
// open-world elite creatures (e.g. Plated Stegodon, Devilsaurs) that spawn
// in packs and are not meaningful story events.
// ---------------------------------------------------------------------------
static bool IsSignificantKill(const Creature* killed)
{
    if (!PBC_PTR_VALID(killed)) return false;
    if (killed->IsDungeonBoss() || killed->isWorldBoss()) return true;
    uint32 rank = killed->GetCreatureTemplate()->rank;
    return rank >= CREATURE_ELITE_RAREELITE;
}

// ---------------------------------------------------------------------------
// BuildBossLabel
//
// Produces a display string for the killed creature, including its subtitle
// if it has one: "Kel'Thuzad (The Lich's Champion)" or just "Murloc Raider".
// ---------------------------------------------------------------------------
static std::string BuildBossLabel(const Creature* killed)
{
    std::string label = killed->GetName();
    const std::string& sub = killed->GetCreatureTemplate()->SubName;
    if (!sub.empty())
        label += " (" + sub + ")";
    return label;
}

void PBC_PlayerEvents::OnPlayerCreatureKill(Player* killer, Creature* killed)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(killer) || !PBC_PTR_VALID(killed)) return;
    if (!IsSignificantKill(killed)) return;

    // Only fire when the killer is in a group that contains at least one real player.
    Group* grp = killer->GetGroup();
    if (!grp) return;

    WorldSession* killerSess = killer->GetSession();
    bool killerIsBot = PBC_PTR_VALID(killerSess) && killerSess->IsBot();

    if (!PBC_BotIsGroupedWithRealPlayer(killer))
    {
        // Also allow real players as the killer themselves.
        if (!PBC_PTR_VALID(killerSess) || killerSess->IsBot()) return;
    }

    auto bots = PBC_FindGroupBots(killer);
    // If killer is a bot they are excluded from PBC_FindGroupBots; still proceed so
    // they receive the event.  If killer is a real player and no bots are in the
    // group there is nothing to dispatch.
    if (bots.empty() && !killerIsBot) return;

    std::string bossLabel = BuildBossLabel(killed);

    // Location: use party leader's zone/area for a specific location string,
    // matching the same approach used for character location in pbc_character.cpp.
    std::string location;
    {
        Player* locAnchor = killer;
        if (Player* leader = ObjectAccessor::FindPlayer(grp->GetLeaderGUID()))
            if (leader->IsInWorld())
                locAnchor = leader;

        location = PBC_BuildPlaceName(locAnchor);
    }

    std::string eventSuffix = bossLabel;
    if (!location.empty())
        eventSuffix += " in " + location;

    std::string eventLine = PBC_MakeEventLine("The party has slain " + eventSuffix);
    std::string histLine  = PBC_MakeHistLine("The party fought and slain " + eventSuffix);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerCreatureKill: killer={} boss='{}' location='{}' bots={} chance={}%",
                 killer->GetName(), bossLabel, location, bots.size(), g_PBC_ReplyChanceBossKill);

    PBC_NotifyRealPlayersInGroup(killer, eventLine);

    PBC_EventItem ev = BuildCharacterEvent(bots, eventLine, histLine,
                                     g_PBC_ReplyChanceBossKill, CHAT_MSG_PARTY,
                                     /*canCreateEvents=*/true);

    // If the killer is itself a bot it was excluded from PBC_FindGroupBots() but
    // still needs to receive the event in its own history.
    if (killerIsBot)
        ev.silentCharGuids.push_back(killer->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Quest completed event
// ---------------------------------------------------------------------------
void PBC_PlayerEvents::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!PBC_QuestEventGuard(player) || !quest) return;

    if (g_PBC_QuestCompletedSystemPrompt.empty() || g_PBC_QuestCompletedUserPrompt.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] OnPlayerCompleteQuest: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = PBC_StripWowTextCodes(quest->GetTitle());
    std::string questDescription    = PBC_StripWowTextCodes(quest->GetDetails());
    std::string questLogDescription = PBC_StripWowTextCodes(quest->GetObjectives());
    std::string questCompletionLog  = PBC_StripWowTextCodes(quest->GetCompletedText());
    std::string questRewardText     = PBC_StripWowTextCodes(quest->GetOfferRewardText());
    std::string questGiver          = PBC_GetQuestStarterNames(quest->GetQuestId());
    std::string questEnder          = PBC_GetQuestEnderNames(quest->GetQuestId());
    std::string questGiverType      = PBC_GetQuestStarterType(quest->GetQuestId());
    std::string questEnderType      = PBC_GetQuestEnderType(quest->GetQuestId());

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerCompleteQuest: leader={} quest='{}' (id={})",
                 player->GetName(), questTitle, quest->GetQuestId());

    std::string userPrompt = PBC_SubstituteQuestVars(
        g_PBC_QuestCompletedUserPrompt,
        questTitle, questDescription, questLogDescription, questCompletionLog,
        questRewardText, questGiver, questEnder, questGiverType, questEnderType);

    auto bots = PBC_FindGroupBots(player);
    if (bots.empty()) return;

    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = CHAT_MSG_PARTY;
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestCompletedSystemPrompt;
    ev.questUserPrompt    = userPrompt;
    ev.anchorObjGuid      = player->GetGUID();

    PBC_EventItem rolled = BuildCharacterEvent(bots, "", "", g_PBC_ReplyChanceQuestCompleted,
                                         CHAT_MSG_PARTY, /*canCreateEvents=*/true);
    ev.respondingChars  = std::move(rolled.respondingChars);
    ev.silentCharGuids  = std::move(rolled.silentCharGuids);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Quest taken event — internal handler shared by all three script types.
// ---------------------------------------------------------------------------
static void HandleQuestTaken(Player* player, Quest const* quest,
                              std::string const& questGiver,
                              std::string const& questGiverType)
{
    if (!PBC_QuestEventGuard(player) || !quest) return;

    if (g_PBC_QuestTakenSystemPrompt.empty() || g_PBC_QuestTakenUserPrompt.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] HandleQuestTaken: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = PBC_StripWowTextCodes(quest->GetTitle());
    std::string questDescription    = PBC_StripWowTextCodes(quest->GetDetails());
    std::string questLogDescription = PBC_StripWowTextCodes(quest->GetObjectives());
    std::string questCompletionLog  = PBC_StripWowTextCodes(quest->GetCompletedText());

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] HandleQuestTaken: leader={} quest='{}' (id={}) giver='{}' type='{}'",
                 player->GetName(), questTitle, quest->GetQuestId(), questGiver, questGiverType);

    std::string userPrompt = PBC_SubstituteQuestVars(
        g_PBC_QuestTakenUserPrompt,
        questTitle, questDescription, questLogDescription, questCompletionLog,
        /*rewardText=*/"", questGiver, /*questEnder=*/"", questGiverType, /*questEnderType=*/"");

    auto bots = PBC_FindGroupBots(player);
    if (bots.empty()) return;

    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = CHAT_MSG_PARTY;
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestTakenSystemPrompt;
    ev.questUserPrompt    = userPrompt;
    ev.anchorObjGuid      = player->GetGUID();

    PBC_EventItem rolled = BuildCharacterEvent(bots, "", "", g_PBC_ReplyChanceQuestTaken,
                                         CHAT_MSG_PARTY, /*canCreateEvents=*/true);
    ev.respondingChars  = std::move(rolled.respondingChars);
    ev.silentCharGuids  = std::move(rolled.silentCharGuids);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Quest taken — from creature (AllCreatureScript fires for ALL creatures)
// ---------------------------------------------------------------------------
PBC_AllCreatureQuestScript::PBC_AllCreatureQuestScript()
    : AllCreatureScript("PBC_AllCreatureQuestScript") {}

bool PBC_AllCreatureQuestScript::CanCreatureQuestAccept(Player* player, Creature* creature, Quest const* quest)
{
    if (creature && quest && player)
    {
        std::string giverName = creature->GetName();
        HandleQuestTaken(player, quest, giverName, "person");
    }
    return false; // don't prevent quest acceptance
}

// ---------------------------------------------------------------------------
// Quest taken — from gameobject (AllGameObjectScript fires for ALL GOs)
// ---------------------------------------------------------------------------
PBC_AllGameObjectQuestScript::PBC_AllGameObjectQuestScript()
    : AllGameObjectScript("PBC_AllGameObjectQuestScript") {}

bool PBC_AllGameObjectQuestScript::CanGameObjectQuestAccept(Player* player, GameObject* go, Quest const* quest)
{
    if (go && quest && player)
    {
        GameObjectTemplate const* goInfo = go->GetGOInfo();
        std::string giverName = goInfo ? goInfo->name : go->GetName();
        HandleQuestTaken(player, quest, giverName, "object");
    }
    return false; // don't prevent quest acceptance
}

// ---------------------------------------------------------------------------
// Quest taken — from item (AllItemScript fires for ALL items)
// ---------------------------------------------------------------------------
PBC_AllItemQuestScript::PBC_AllItemQuestScript()
    : AllItemScript("PBC_AllItemQuestScript") {}

bool PBC_AllItemQuestScript::CanItemQuestAccept(Player* player, Item* item, Quest const* quest)
{
    if (item && quest && player)
    {
        ItemTemplate const* itemInfo = item->GetTemplate();
        std::string giverName = itemInfo ? itemInfo->Name1 : "Unknown Item";
        HandleQuestTaken(player, quest, giverName, "item");
    }
    return true; // true = allow quest acceptance (AllItemScript convention)
}

// ---------------------------------------------------------------------------
// PBC_RollBotsWithPenalty
//
// Roll bots with decaying penalty.  Does NOT shuffle — caller should shuffle
// the bot list first if randomised order is desired.
// Fills ev.respondingChars and ev.silentCharGuids.
// ---------------------------------------------------------------------------
void PBC_RollBotsWithPenalty(PBC_EventItem& ev,
                              const std::vector<Player*>& bots,
                              uint32_t baseChance,
                              const char* debugLabel)
{
    uint32 currentChance = baseChance;
    for (Player* bot : bots)
    {
        if (currentChance == 0)
        {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll {} character={} chance=0% -> silent (no chance left)",
                         debugLabel, bot->GetName());
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
            continue;
        }

        uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), currentChance);
        bool rolled = PBC_RollChance(effectiveChance);
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] Roll {} character={} chance={}% (base={}% mod={}) -> {}",
                     debugLabel, bot->GetName(), effectiveChance, currentChance,
                     static_cast<int32_t>(effectiveChance) - static_cast<int32_t>(currentChance),
                     rolled ? "RESPOND" : "silent");
        if (rolled)
        {
            ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
            currentChance = currentChance > g_PBC_RollPenaltyOnAnswer
                ? currentChance - g_PBC_RollPenaltyOnAnswer : 0;
        }
        else
        {
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }
    }
}

// ---------------------------------------------------------------------------
// PBC_RollBotsForMessage
//
// Full message roll logic: checks for mentions, sorts mentioned bots by
// position in the message, rolls mentioned bots at mention chance, then
// non-mentioned bots at a reduced chance with decaying penalty.
// Handles shuffling internally.
// ---------------------------------------------------------------------------
void PBC_RollBotsForMessage(PBC_EventItem& ev,
                             const std::vector<Player*>& bots,
                             const std::string& message)
{
    // Check if the message mentions any specific bot.
    bool anyMention = false;
    for (Player* bot : bots)
        if (PBC_MentionsCharacter(message, bot->GetName())) { anyMention = true; break; }

    if (anyMention)
    {
        // Build ordered list of mentioned bots (by position in message).
        std::string lowerMsg = message;
        std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

        std::vector<std::pair<size_t, Player*>> positions;
        for (Player* bot : bots)
        {
            std::string lname = bot->GetName();
            std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
            size_t pos = lowerMsg.find(lname);
            if (pos != std::string::npos)
                positions.emplace_back(pos, bot);
        }
        std::sort(positions.begin(), positions.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        // Mentioned bots: roll at mention chance, in mention order.
        std::unordered_set<uint64_t> mentionedGuids;
        for (auto& [pos, bot] : positions)
        {
            mentionedGuids.insert(bot->GetGUID().GetCounter());
            uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), g_PBC_ReplyChanceMention);
            bool rolled = PBC_RollChance(effectiveChance);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll mention character={} chance={}% -> {}",
                         bot->GetName(), effectiveChance, rolled ? "RESPOND" : "silent");
            if (rolled)
                ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
            else
                ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }

        // Non-mentioned bots roll at a reduced chance: ReplyChanceMention
        // minus (RollPenaltyOnAnswer * number of mentions). Each successful
        // roll further reduces the chance by RollPenaltyOnAnswer.
        uint32 mentionPenalty = g_PBC_RollPenaltyOnAnswer * mentionedGuids.size();
        uint32 baseChance = g_PBC_ReplyChanceMention > mentionPenalty
            ? g_PBC_ReplyChanceMention - mentionPenalty : 0;

        std::vector<Player*> nonMentionedBots;
        for (Player* bot : bots)
            if (!mentionedGuids.count(bot->GetGUID().GetCounter()))
                nonMentionedBots.push_back(bot);

        std::shuffle(nonMentionedBots.begin(), nonMentionedBots.end(), PBC_GetRNG());

        PBC_RollBotsWithPenalty(ev, nonMentionedBots, baseChance, "mention-bystander");
    }
    else
    {
        // No mention: randomize bot roll order, roll with penalty on success.
        std::vector<Player*> shuffledBots = bots;
        std::shuffle(shuffledBots.begin(), shuffledBots.end(), PBC_GetRNG());

        PBC_RollBotsWithPenalty(ev, shuffledBots, g_PBC_ReplyChanceMessage, "message");
    }
}

// ---------------------------------------------------------------------------
// PBC_DispatchWhisperEvent
// ---------------------------------------------------------------------------

void PBC_DispatchWhisperEvent(Player* sender, Player* target, const std::string& msg)
{
    if (!PBC_PTR_VALID(sender) || !PBC_PTR_VALID(target)) return;

    std::string senderName = sender->GetName();
    std::string eventLine   = senderName + " tells you privately: " + msg;
    std::string historyLine = senderName + " (privately to you): " + msg;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Whisper event: {} -> {}: \"{}\" (chance={}%)",
                 senderName, target->GetName(), msg, g_PBC_ReplyChanceWhisper);

    PBC_EventItem ev;
    ev.type      = PBC_EventType::Normal;
    ev.eventLine = eventLine;
    ev.histLine  = historyLine;
    ev.chatType  = CHAT_MSG_WHISPER;

    // Deliberately not applying roll chance modifiers — whispers are direct
    // conversations and should always use the base chance.
    if (PBC_RollChance(g_PBC_ReplyChanceWhisper))
    {
        PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(target);
        snap.whisperTargetGuid = sender->GetGUID();
        snap.whisperTargetName = senderName;
        ev.respondingChars.push_back(std::move(snap));
    }
    else
    {
        ev.silentCharGuids.push_back(target->GetGUID().GetCounter());
    }

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_DispatchPartyMessageEvent
// ---------------------------------------------------------------------------

void PBC_DispatchPartyMessageEvent(Player* sender, const std::string& msg,
                                    const std::string& senderNameOverride,
                                    uint32_t chatType,
                                    bool canCreateEvents)
{
    if (!PBC_PTR_VALID(sender)) return;

    std::string senderName = senderNameOverride.empty() ? sender->GetName() : senderNameOverride;
    std::string historyLine = senderName + ": " + msg;
    std::string eventLine   = senderName + " says: " + msg;

    // For group chat, find group bots; for say/yell, find nearby bots
    bool isGroupChat = (chatType == CHAT_MSG_PARTY || chatType == CHAT_MSG_PARTY_LEADER ||
                        chatType == CHAT_MSG_RAID  || chatType == CHAT_MSG_RAID_LEADER  ||
                        chatType == CHAT_MSG_RAID_WARNING);

    std::vector<Player*> bots = isGroupChat ? PBC_FindGroupBots(sender) : FindNearbyBots(sender);
    if (bots.empty()) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Chat message event from {} type={} ({} bots): \"{}\"",
                 senderName, chatType, bots.size(), msg);

    PBC_EventItem ev;
    ev.type            = PBC_EventType::Normal;
    ev.eventLine       = eventLine;
    ev.histLine        = historyLine;
    ev.chatType        = chatType ? chatType : CHAT_MSG_PARTY;
    ev.canCreateEvents = canCreateEvents;

    PBC_RollBotsForMessage(ev, bots, msg);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Chat from {} type={} -> {}/{} bots will respond",
                 senderName, chatType, ev.respondingChars.size(), bots.size());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_FindGroupBotsExcluding
// ---------------------------------------------------------------------------

std::vector<Player*> PBC_FindGroupBotsExcluding(Player* player,
    const std::unordered_set<uint64_t>& excludedGuids)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(player)) return bots;

    Group* grp = player->GetGroup();
    if (!grp) return bots;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || member == player) continue;
        if (!member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) continue;
        if (excludedGuids.count(member->GetGUID().GetCounter())) continue;
        bots.push_back(member);
    }
    return bots;
}

// ---------------------------------------------------------------------------
// PBC_CondenseInline
//
// Runs condensation synchronously inside the event thread for a bot whose
// history has exceeded g_PBC_MaxCtx.  Calls the LLM, writes the card
// addition directly (thread-safe), and resets the in-memory history to a
// short tail.  Does NOT touch any Player* objects.
//
// Returns true if condensation succeeded (history was trimmed and card
// addition written).  Returns false if the LLM call failed or returned
// empty — in that case the history is left untouched so no data is lost.
// ---------------------------------------------------------------------------
static bool PBC_CondenseInline(PBC_CharacterSnapshot& snap,
                                const std::string& sysPrompt,
                                const std::string& userPromptTmpl)
{
    if (sysPrompt.empty() || userPromptTmpl.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] CondenseInline: prompts not configured, skipping for character={}", snap.charName);
        return false;
    }

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] CondenseInline: character={} history_lines={}", snap.charName, snap.history.size());

    std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(snap, userPromptTmpl);
    PBC_LLMResult res = g_PBC_UseAltModelForCondensation
        ? PBC_CallLLMAlt(sysPrompt, userPrompt, /*maxTokensOverride=*/-1)
        : PBC_CallLLM(sysPrompt, userPrompt, /*maxTokensOverride=*/-1);

    if (!res.success || res.text.empty())
    {
        LOG_WARN("server.loading", "[PBC] CondenseInline: LLM failed for character={} — history left untouched, will retry on next event", snap.charName);
        return false;
    }

    // Write card addition
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
        g_PBC_CardAdditions[snap.charGuidRaw].push_back(res.text);
    }
    DB_InsertCardAddition(snap.charGuidRaw, res.text);
    PBC_WsNotify(snap.charGuidRaw, "additions");

    // Keep only the last N lines as the tail (configured via PBC.CondensationPreservedLines)
    const size_t kTailLines = static_cast<size_t>(g_PBC_CondensationPreservedLines);
    std::deque<std::string> tail;
    for (size_t i = snap.history.size() > kTailLines ? snap.history.size() - kTailLines : 0;
         i < snap.history.size(); ++i)
        tail.push_back(snap.history[i]);

    // Reset global history
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        g_PBC_ChatHistory[snap.charGuidRaw] = tail;
    }
    // Delete from DB and re-insert tail
    DB_DeleteHistoryForCharacter(snap.charGuidRaw);
    for (const auto& line : tail)
        DB_InsertHistoryLine(snap.charGuidRaw, line);

    // Update snapshot's local history to match
    snap.history = tail;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] CondenseInline: condensed character={} summary_len={} tail_lines={}",
                 snap.charName, res.text.size(), tail.size());

    return true;
}

// ---------------------------------------------------------------------------
// PBC_ProcessEventItem
//
// Worker function — runs in a detached thread.  Processes one PBC_EventItem
// completely (all LLM calls, history writes) then sets g_PBC_EventThreadDone.
//
// Only game-object operations (sending chat packets) are deferred to the main
// thread via PBC_PendingActions.
// ---------------------------------------------------------------------------
void PBC_ProcessEventItem(PBC_EventItem ev)
{
    // Capture config strings we need (read-only, safe without lock since
    // only main thread writes them and this thread just reads).
    std::string sysPrompt         = g_PBC_SystemPrompt;
    std::string condenseSysPrompt = g_PBC_CondensationSystemPrompt;
    std::string condenseUsrTmpl   = g_PBC_CondensationUserPrompt;

    // -----------------------------------------------------------------------
    // HistoryReload event
    //
    // Reload all bot histories from the database, replacing the in-memory
    // maps.  This runs after all previously queued events have been processed,
    // so no in-flight history writes are lost.
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::HistoryReload)
    {
        LOG_INFO("server.loading", "[PBC] HistoryReload: reloading chat history from DB...");

        QueryResult result = CharacterDatabase.Query(
            "SELECT bot_guid, message, UNIX_TIMESTAMP(timestamp) FROM mod_pbc_chat_history ORDER BY id ASC"
        );

        {
            std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
            g_PBC_ChatHistory.clear();
            g_PBC_LastHistoryTime.clear();

            if (result)
            {
                do {
                    uint64_t    botGuid = (*result)[0].Get<uint64_t>();
                    std::string msg     = (*result)[1].Get<std::string>();
                    time_t      ts      = static_cast<time_t>((*result)[2].Get<uint64_t>());
                    g_PBC_ChatHistory[botGuid].push_back(std::move(msg));
                    if (ts > 0)
                        g_PBC_LastHistoryTime[botGuid] = ts;
                } while (result->NextRow());
            }
        }

        // Also reload relationships so the in-memory map stays consistent.
        PBC_LoadRelationshipsFromDB();

        LOG_INFO("server.loading", "[PBC] HistoryReload: chat history and relationships reloaded from DB.");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    // -----------------------------------------------------------------------
    // Condensation event
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::Condensation)
    {
        // Capture the full history snapshot BEFORE condensation truncates it
        // so the relationship LLM calls have complete context.
        std::deque<std::string> preCondensationHistory = ev.condensationChar.history;

        bool condensed = PBC_CondenseInline(ev.condensationChar, condenseSysPrompt, condenseUsrTmpl);

        if (!condensed)
        {
            LOG_WARN("server.loading",
                     "[PBC] Condensation event failed for character={} — history left untouched, "
                     "will retry when threshold is reached again",
                     ev.condensationChar.charName);
            g_PBC_EventThreadDone.store(true);
            return;
        }

        // Condensation succeeded — queue relationship updates for all party
        // members.  No threshold check needed here.  The history has been
        // wiped, so we must capture relationships now before that context is gone.
        // We pass mention_count=0 to reset the baseline; the normal threshold
        // tracking will accumulate fresh counts from the new post-condensation history.
        {
            const PBC_CharacterSnapshot& snap = ev.condensationChar;
            if (!g_PBC_RelationshipUpdateSystemPrompt.empty() &&
                !g_PBC_RelationshipUpdateUserPrompt.empty() &&
                !snap.partyMemberNames.empty())
            {
                for (const auto& memberName : snap.partyMemberNames)
                {
                    std::string currentRel;
                    {
                        std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
                        auto botIt = g_PBC_Relationships.find(snap.charGuidRaw);
                        if (botIt != g_PBC_Relationships.end())
                        {
                            auto tgtIt = botIt->second.find(memberName);
                            if (tgtIt != botIt->second.end())
                                currentRel = tgtIt->second.text;
                        }
                    }
                    if (currentRel.empty())
                        currentRel = PBC_DefaultRelationshipText(memberName);

                    // Use the pre-condensation history so the LLM has full context.
                    PBC_CharacterSnapshot relSnap = snap;
                    relSnap.history = preCondensationHistory;

                    PBC_EventItem relEv;
                    relEv.type                       = PBC_EventType::RelationshipUpdate;
                    relEv.relationshipChar            = std::move(relSnap);
                    relEv.relationshipTargetName     = memberName;
                    relEv.relationshipTargetInfo     = PBC_BuildTargetInfo(memberName);
                    relEv.relationshipCurrentText    = currentRel;
                    relEv.relationshipMentionTotal   = 0; // reset baseline for post-condensation tracking
                    relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
                    relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

                    PBC_PushEvent(std::move(relEv));

                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading",
                                 "[PBC] Condensation: queuing relationship update for character={} target={}",
                                 snap.charName, memberName);
                }
            }
        }

        g_PBC_EventThreadDone.store(true);
        return;
    }

    // -----------------------------------------------------------------------
    // RelationshipUpdate event
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::RelationshipUpdate)
    {
        if (ev.relationshipSystemPrompt.empty() || ev.relationshipUserPromptTmpl.empty())
        {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] RelationshipUpdate: prompts not configured, skipping for character={}",
                         ev.relationshipChar.charName);
            g_PBC_EventThreadDone.store(true);
            return;
        }

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] RelationshipUpdate: character={} target={}",
                     ev.relationshipChar.charName, ev.relationshipTargetName);

        // Build the user prompt: substitute {character_card}, {chat_history},
        // {relationship_target}, {target_current_relationship}.
        std::string userPrompt = ev.relationshipUserPromptTmpl;
        PBC_ExpandNewlineEscapes(userPrompt);

        PBC_ReplaceToken(userPrompt, "character_card",           ev.relationshipChar.characterCard);
        { std::ostringstream histOss; for (const auto& line : ev.relationshipChar.history) histOss << line << "\n"; PBC_ReplaceToken(userPrompt, "chat_history", histOss.str()); }
        PBC_ReplaceToken(userPrompt, "relationship_target",      ev.relationshipTargetInfo);
        PBC_ReplaceToken(userPrompt, "target_current_relationship", ev.relationshipCurrentText);

        PBC_LLMResult res = g_PBC_UseAltModelForRelationshipUpdate
            ? PBC_CallLLMAlt(ev.relationshipSystemPrompt, userPrompt, /*maxTokensOverride=*/-1)
            : PBC_CallLLM(ev.relationshipSystemPrompt, userPrompt, /*maxTokensOverride=*/-1);

        if (!res.success || res.text.empty())
        {
            LOG_WARN("server.loading", "[PBC] RelationshipUpdate: LLM failed for character={} target={}",
                     ev.relationshipChar.charName, ev.relationshipTargetName);
            g_PBC_EventThreadDone.store(true);
            return;
        }

        // Store the updated relationship text and the mention count at time of
        // update, so server restarts don't trigger redundant LLM calls.
        {
            std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
            auto& entry = g_PBC_Relationships[ev.relationshipChar.charGuidRaw][ev.relationshipTargetName];
            entry.text = res.text;
            entry.mentionCountAtLastUpdate = ev.relationshipMentionTotal;
        }
        DB_UpsertRelationship(ev.relationshipChar.charGuidRaw, ev.relationshipTargetName,
                              res.text, ev.relationshipMentionTotal);
        PBC_WsNotify(ev.relationshipChar.charGuidRaw, "relationship");

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] RelationshipUpdate: updated character={} target={} mentions={} text=\"{}\"",
                     ev.relationshipChar.charName, ev.relationshipTargetName,
                     ev.relationshipMentionTotal, PBC_SanitizeForFmt(res.text));

        g_PBC_EventThreadDone.store(true);
        return;
    }

    // -----------------------------------------------------------------------
    // QuestSummarization: generate the summary first, then treat as Normal.
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::QuestSummarization)
    {
        if (ev.questSystemPrompt.empty() || ev.questUserPrompt.empty())
        {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] ProcessEvent: QuestSummarization prompts empty, skipping");
            g_PBC_EventThreadDone.store(true);
            return;
        }

        PBC_LLMResult summary = PBC_CallLLM(ev.questSystemPrompt, ev.questUserPrompt);
        if (!summary.success || summary.text.empty())
        {
            LOG_WARN("server.loading", "[PBC] ProcessEvent: QuestSummarization LLM failed");
            g_PBC_EventThreadDone.store(true);
            return;
        }

        ev.eventLine = PBC_MakeEventLine(summary.text);
        ev.histLine  = PBC_MakeHistLine(summary.text);

        // Send the narrator summary to all real players in the anchor's group,
        // using the same PBC_PendingAction mechanism as "thinks..." messages.
        if (g_PBC_DisplayNarratorEvents && !ev.anchorObjGuid.IsEmpty())
        {
            PBC_PendingAction narrAction;
            narrAction.charGuid          = ev.anchorObjGuid;
            narrAction.text              = ev.eventLine;
            narrAction.isNarratorMessage = true;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(narrAction));
        }

        // Fall through to Normal processing
    }

    // -----------------------------------------------------------------------
    // Normal event processing
    // -----------------------------------------------------------------------
    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] ProcessEvent: type={} respondingChars={} silentChars={} event=\"{}\"",
                 static_cast<int>(ev.type), ev.respondingChars.size(), ev.silentCharGuids.size(), ev.eventLine);

    // The "current event" for the first bot is the original event line.
    // After each bot responds, the current event for the next bot becomes
    // "<BotName> says: <reply>" — so each subsequent responder reacts to
    // what the previous bot just said.
    std::string currentEvent = ev.eventLine;

    // Track the last successful responder so we can post a single secondary
    // event request after the entire chain has finished.
    uint64_t    lastResponderGuid = 0;
    std::string lastReplyLine;   // "<BotName>: <text>"  (history format)
    std::string lastEventLine;   // "<BotName> says: <text>" (event format)

    // Collects every reply line in the order they are generated.
    // Global history writes for responding characters are deferred until after the
    // loop so that every character receives the complete, correctly-ordered
    // conversation (histLine + ALL replies) rather than only the replies from
    // bots that preceded it.
    //
    // During the loop we only update each bot's LOCAL snapshot history so the
    // next bot in the chain sees the previous reply when building its prompt.
    std::vector<std::string> completedReplyLines;

    for (PBC_CharacterSnapshot& snap : ev.respondingChars)
    {
        // Post deferred "thinks..." notification to the main thread so it
        // appears right before this bot's LLM call, not at event-creation time.
        // Only posted when PBC.DisplayNarratorEvents is enabled.
        if (g_PBC_DisplayNarratorEvents)
        {
            PBC_PendingAction action;
            action.charGuid          = snap.charObjGuid;
            action.text              = PBC_MakeEventLine(snap.charName + " thinks...");
            action.isNarratorMessage = true;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(action));
        }
        PBC_WsNotify(snap.charGuidRaw, "thinks");

        // Condense inline if over token budget before building the prompt.
        // If condensation fails, the history is left untouched and will be
        // retried on the next event that triggers this character.
        int histTokens = PBC_EstimateHistoryTokens(snap.charGuidRaw);
        if (histTokens > static_cast<int>(g_PBC_MaxCtx))
        {
            bool condensed = PBC_CondenseInline(snap, condenseSysPrompt, condenseUsrTmpl);
            // If condensation succeeded, snap.history is now the condensed tail.
            // If it failed, snap.history still contains the full uncondensed
            // history — the LLM call below will use it as-is, and condensation
            // will be retried on the next event for this character.
            (void)condensed; // suppress unused-variable warning
        }

        // Build user prompt from snapshot (uses snap.history for {chat_history}).
        std::string userPrompt = PBC_BuildUserPromptFromSnapshot(snap, currentEvent);

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] ProcessEvent: calling LLM for character={} event=\"{}\"",
                     snap.charName, currentEvent);

        PBC_LLMResult res = PBC_CallLLM(sysPrompt, userPrompt);

        if (!res.success || res.text.empty())
        {
            LOG_WARN("server.loading", "[PBC] ProcessEvent: LLM failed/empty for character={}", snap.charName);
            // Don't advance currentEvent — the next character reacts to the same event.
            // Global history for this character is handled in the deferred write below.
            continue;
        }

        // Build the history line for this bot's own reply.
        std::string replyLine;
        if (ev.chatType == CHAT_MSG_WHISPER && !snap.whisperTargetName.empty())
            replyLine = snap.charName + " (privately to " + snap.whisperTargetName + "): " + res.text;
        else
            replyLine = snap.charName + ": " + res.text;

        // Update the snapshot's local history so subsequent characters in the chain
        // see all previous replies when building their prompts.
        // Global history is written in the deferred pass after the loop.
        if (!ev.histLine.empty())
            snap.history.push_back(ev.histLine);
        snap.history.push_back(replyLine);

        // Collect reply for deferred global history write.
        completedReplyLines.push_back(replyLine);

        // If narrator events are enabled, split the reply into alternating
        // narrator (*text*) and regular text segments, posting each as a
        // separate in-game message.
        if (g_PBC_DisplayNarratorEvents)
        {
            // Pre-scan: find all valid *text* narrator spans (opening '*'
            // followed by at least one character and a closing '*').
            std::vector<std::pair<size_t, size_t>> narrSpans; // [start, end] inclusive
            {
                size_t pos = 0;
                while (pos < res.text.size())
                {
                    if (res.text[pos] == '*')
                    {
                        size_t closingPos = res.text.find('*', pos + 1);
                        if (closingPos != std::string::npos && closingPos > pos + 1)
                        {
                            narrSpans.emplace_back(pos, closingPos);
                            pos = closingPos + 1;
                            continue;
                        }
                    }
                    pos++;
                }
            }

            if (narrSpans.empty())
            {
                // No narrator blocks — send the whole reply as one chat message.
                if (!res.text.empty())
                {
                    PBC_PendingAction action;
                    action.charGuid    = snap.charObjGuid;
                    action.targetGuid = snap.whisperTargetGuid;
                    action.chatType   = ev.chatType;
                    action.text       = res.text;

                    std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
                    g_PBC_PendingActions.push(std::move(action));
                }
            }
            else
            {
                // Push alternating narrator / regular segments under a single lock.
                std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);

                size_t lastEnd = 0;
                for (const auto& span : narrSpans)
                {
                    // Regular text before this narrator block.
                    if (span.first > lastEnd)
                    {
                        std::string reg = res.text.substr(lastEnd, span.first - lastEnd);
                        size_t s = reg.find_first_not_of(" \t\n\r");
                        size_t e = reg.find_last_not_of(" \t\n\r");
                        if (s != std::string::npos && e != std::string::npos && s <= e)
                        {
                            PBC_PendingAction action;
                            action.charGuid    = snap.charObjGuid;
                            action.targetGuid = snap.whisperTargetGuid;
                            action.chatType   = ev.chatType;
                            action.text       = reg.substr(s, e - s + 1);
                            g_PBC_PendingActions.push(std::move(action));
                        }
                    }

                    // Narrator block.
                    {
                        PBC_PendingAction narrAction;
                        narrAction.charGuid          = snap.charObjGuid;
                        narrAction.text              = res.text.substr(span.first, span.second - span.first + 1);
                        narrAction.isNarratorMessage = true;
                        g_PBC_PendingActions.push(std::move(narrAction));
                    }

                    lastEnd = span.second + 1;
                }

                // Remaining regular text after the last narrator block.
                if (lastEnd < res.text.size())
                {
                    std::string reg = res.text.substr(lastEnd);
                    size_t s = reg.find_first_not_of(" \t\n\r");
                    size_t e = reg.find_last_not_of(" \t\n\r");
                    if (s != std::string::npos && e != std::string::npos && s <= e)
                    {
                        PBC_PendingAction action;
                        action.charGuid    = snap.charObjGuid;
                        action.targetGuid = snap.whisperTargetGuid;
                        action.chatType   = ev.chatType;
                        action.text       = reg.substr(s, e - s + 1);
                        g_PBC_PendingActions.push(std::move(action));
                    }
                }
            }
        }
        else
        {
            // Narrator events disabled — send the full reply as one chat message.
            if (!res.text.empty())
            {
                PBC_PendingAction action;
                action.charGuid    = snap.charObjGuid;
                action.targetGuid = snap.whisperTargetGuid;
                action.chatType   = ev.chatType;
                action.text       = res.text;

                std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
                g_PBC_PendingActions.push(std::move(action));
            }
        }

        // Advance the chain: the next character reacts to this bot's reply.
        currentEvent = snap.charName + " says: " + res.text;

        // Remember the last successful reply details for the secondary event.
        lastResponderGuid = snap.charGuidRaw;
        lastReplyLine     = replyLine;
        lastEventLine     = currentEvent;

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] ProcessEvent: character={} replied", snap.charName);
    }

    // -----------------------------------------------------------------------
    // Deferred global history write for all responding characters.
    //
    // Every responding bot (whether it replied successfully or failed) ends up
    // with the full conversation in its history:
    //   histLine, reply[0], reply[1], ..., reply[N-1]
    //
    // PBC_AppendHistory deduplicates consecutive identical lines, so there is
    // no risk of double-writing entries already present from a previous event.
    // -----------------------------------------------------------------------
    if (!ev.histLine.empty() || !completedReplyLines.empty())
    {
        for (const PBC_CharacterSnapshot& snap : ev.respondingChars)
        {
            if (!ev.histLine.empty())
                PBC_AppendHistory(snap.charGuidRaw, ev.histLine);
            for (const auto& replyLine : completedReplyLines)
                PBC_AppendHistory(snap.charGuidRaw, replyLine);
        }
    }

    // -----------------------------------------------------------------------
    // Secondary event: if this event can spawn message events and at least
    // one bot replied, ask the main thread to push a new message event for
    // any other group characters that did NOT participate in this event.  We post
    // exactly ONE request here — after the full responder chain — using the
    // last successful reply as the trigger.  This prevents intermediate
    // replies from generating extra events when multiple bots responded.
    //
    // Note: we do NOT require silentCharGuids to be non-empty.  OnUpdate handles
    // the case where there are no eligible targets gracefully (pushes nothing).
    // -----------------------------------------------------------------------
    if (ev.canCreateEvents && lastResponderGuid != 0)
    {
        // Excluded = every bot that already participated (responders + silent).
        std::unordered_set<uint64_t> excluded;
        for (const auto& rs : ev.respondingChars)
            excluded.insert(rs.charGuidRaw);
        for (uint64_t g : ev.silentCharGuids)
            excluded.insert(g);

        PBC_PendingEventRequest req;
        req.eventLine        = lastEventLine;   // "<LastBot> says: <text>"
        req.histLine         = lastReplyLine;   // "<LastBot>: <text>"
        req.originHistLine   = ev.histLine;     // original trigger (Narrator line, etc.)
        req.chatType         = ev.chatType;
        req.anchorCharGuid    = lastResponderGuid;
        // Collect the GUIDs of all original responders so OnUpdate can pass
        // them as replyOnlyCharGuids on the secondary event — they already have
        // histLine but still need to receive any new replies.
        for (const auto& rs : ev.respondingChars)
            req.originCharGuids.push_back(rs.charGuidRaw);
        req.excludedCharGuids = std::move(excluded);

        // Capture debug values before the move.
        size_t dbgExcluded = req.excludedCharGuids.size();
        std::string dbgEvent = req.eventLine;

        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingEventRequestsMutex);
            g_PBC_PendingEventRequests.push(std::move(req));
        }

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading",
                     "[PBC] ProcessEvent: queued secondary event from last responder guid={} "
                     "excluded={} silent={} event=\"{}\"",
                     lastResponderGuid,
                     dbgExcluded,
                     ev.silentCharGuids.size(),
                     dbgEvent);
    }

    // -----------------------------------------------------------------------
    // Update silent characters' history.
    // -----------------------------------------------------------------------
    if (!ev.histLine.empty())
    {
        for (uint64_t guid : ev.silentCharGuids)
            PBC_AppendHistory(guid, ev.histLine);

        // Propagate all responding characters' replies to silent peers so their
        // history stays current.  completedReplyLines holds every reply line
        // in the order they were generated, which is exactly what silent bots
        // need to see.
        if (!completedReplyLines.empty() && ev.chatType != CHAT_MSG_WHISPER)
        {
            for (uint64_t guid : ev.silentCharGuids)
            {
                for (const auto& replyLine : completedReplyLines)
                    PBC_AppendHistory(guid, replyLine);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Propagate replies to replyOnlyCharGuids.
    // These characters already have histLine in their history (they were the
    // original responders that triggered this secondary event) but still need
    // to receive any new replies generated here.  histLine is NOT re-written.
    // -----------------------------------------------------------------------
    if (!completedReplyLines.empty() && !ev.replyOnlyCharGuids.empty()
        && ev.chatType != CHAT_MSG_WHISPER)
    {
        for (uint64_t guid : ev.replyOnlyCharGuids)
        {
            for (const auto& replyLine : completedReplyLines)
                PBC_AppendHistory(guid, replyLine);
        }
    }

    // -----------------------------------------------------------------------
    // Mention tracking for relationship updates.
    //
    // After all history writes, for each responding bot count how many times
    // each party member's name appears in that bot's full global history.
    // We compare the total against the mentionCountAtLastUpdate stored in
    // g_PBC_Relationships (persisted in DB across server restarts).
    // If new mentions since the last update >= threshold, push a
    // RelationshipUpdate event.
    // -----------------------------------------------------------------------
    if (g_PBC_RelationshipUpdateThreshold > 0 &&
        !g_PBC_RelationshipUpdateSystemPrompt.empty() &&
        !g_PBC_RelationshipUpdateUserPrompt.empty() &&
        !ev.respondingChars.empty())
    {
        for (const PBC_CharacterSnapshot& snap : ev.respondingChars)
        {
            if (snap.partyMemberNames.empty()) continue;

            // Read the bot's full current history from the global map.
            std::deque<std::string> fullHistory;
            {
                std::lock_guard<std::mutex> lh(g_PBC_HistoryMutex);
                auto hIt = g_PBC_ChatHistory.find(snap.charGuidRaw);
                if (hIt != g_PBC_ChatHistory.end())
                    fullHistory = hIt->second;
            }
            if (fullHistory.empty()) continue;

            for (const auto& memberName : snap.partyMemberNames)
            {
                // Count total occurrences of memberName in the full history.
                uint32_t total = PBC_CountMentions(fullHistory, memberName);

                // Read the mention count recorded at the last update.
                uint32_t lastCount = 0;
                std::string currentRel;
                {
                    std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
                    auto botIt = g_PBC_Relationships.find(snap.charGuidRaw);
                    if (botIt != g_PBC_Relationships.end())
                    {
                        auto tgtIt = botIt->second.find(memberName);
                        if (tgtIt != botIt->second.end())
                        {
                            lastCount  = tgtIt->second.mentionCountAtLastUpdate;
                            currentRel = tgtIt->second.text;
                        }
                    }
                }

                // Guard against counter going backwards (history was condensed/reset).
                if (total < lastCount) lastCount = 0;

                uint32_t newMentions = total - lastCount;
                if (newMentions < g_PBC_RelationshipUpdateThreshold) continue;

                if (currentRel.empty())
                    currentRel = PBC_DefaultRelationshipText(memberName);

                // Build a snapshot for the relationship event.  Use the
                // current responding bot's snapshot but refresh its history
                // to the full global history we just read.
                PBC_CharacterSnapshot relSnap = snap;
                relSnap.history         = fullHistory;

                PBC_EventItem relEv;
                relEv.type                       = PBC_EventType::RelationshipUpdate;
                relEv.relationshipChar            = std::move(relSnap);
                relEv.relationshipTargetName     = memberName;
                relEv.relationshipTargetInfo     = PBC_BuildTargetInfo(memberName);
                relEv.relationshipCurrentText    = currentRel;
                // Pass the current total so the handler can persist it.
                relEv.relationshipMentionTotal   = total;
                relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
                relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

                PBC_PushEvent(std::move(relEv));

                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading",
                             "[PBC] MentionTracker: character={} target={} total_mentions={} new_since_last={} — relationship update queued",
                             snap.charName, memberName, total, newMentions);
            }
        }
    }

    g_PBC_EventThreadDone.store(true);
}
