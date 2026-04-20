#include "pbc_events.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_llm.h"
#include "Log.h"
#include "Player.h"
#include "Creature.h"
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

#include <cstdlib>
#include <algorithm>
#include <regex>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Pointer sanity guard
//
// On a 64-bit system, valid heap/stack pointers are always well above the
// first 64 KiB of address space.  Values in that range are garbage that the
// playerbots module occasionally passes to hooks during bot packet processing.
// ---------------------------------------------------------------------------
#define PBC_PTR_VALID(p) (reinterpret_cast<uintptr_t>(p) > 0xFFFFu)

// ---------------------------------------------------------------------------
// Strip WoW item/object link markup, keeping only the display name.
// ---------------------------------------------------------------------------
static std::string SanitizeChatMessage(const std::string& msg)
{
    static const std::regex linkPattern(
        R"((?:\|c[0-9a-fA-F]{8})?\|H[^|]+\|h(\[[^\]]*\])\|h(?:\|r)?)",
        std::regex::optimize);

    std::string result;
    result.reserve(msg.size());

    auto it  = msg.cbegin();
    auto end = msg.cend();
    std::smatch m;

    while (std::regex_search(it, end, m, linkPattern))
    {
        result.append(it, m.prefix().second);
        result += m[1].str();
        it = m.suffix().first;
    }
    result.append(it, end);
    return result;
}

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
static void PBC_NotifyRealPlayersInGroup(Player* anchor, const std::string& eventLine)
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

static bool RollChance(uint32 chance)
{
    return chance > 0 && (static_cast<uint32>(std::rand() % 100) < chance);
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

static std::vector<Player*> FindGroupBots(Player* player)
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

static bool BotIsGroupedWithRealPlayer(Player* bot)
{
    if (!PBC_PTR_VALID(bot)) return false;
    Group* grp = bot->GetGroup();
    if (!grp) return false;
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || !member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) return true;
    }
    return false;
}

static bool MentionsBot(const std::string& msg, const std::string& botName)
{
    std::string lower = msg, lname = botName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    return lower.find(lname) != std::string::npos;
}

// ---------------------------------------------------------------------------
// BuildBotEvent
//
// Rolls chance for each bot in the provided list, builds snapshots for
// responders, collects GUIDs for silent bots, and returns a ready-to-push
// PBC_EventItem.  chatType and combatSkip are passed through.
// ---------------------------------------------------------------------------
static PBC_EventItem BuildBotEvent(const std::vector<Player*>& bots,
                                    const std::string& eventLine,
                                    const std::string& histLine,
                                    uint32_t chance,
                                    uint32_t chatType,
                                    bool skipHistoryIfSilent = false,
                                    bool canCreateEvents = false)
{
    PBC_EventItem ev;
    ev.type                  = PBC_EventType::Normal;
    ev.eventLine             = eventLine;
    ev.histLine              = histLine;
    ev.chatType              = chatType;
    ev.skipHistoryIfSilent   = skipHistoryIfSilent;
    ev.canCreateEvents       = canCreateEvents;

    for (Player* bot : bots)
    {
        bool rolled = RollChance(chance);
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] Roll event bot={} chance={}% -> {}",
                     bot->GetName(), chance, rolled ? "RESPOND" : "silent");
        if (rolled)
            ev.respondingBots.push_back(PBC_SnapshotBot(bot));
        else
            ev.silentBotGuids.push_back(bot->GetGUID().GetCounter());
    }

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

    if (!anchorIsReal && !BotIsGroupedWithRealPlayer(anchor))
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] DispatchGroupEvent: skipped — no real player in group (anchor={})", anchor->GetName());
        return;
    }

    if (notifyRealPlayers)
        PBC_NotifyRealPlayersInGroup(anchor, eventLine);

    auto bots = FindGroupBots(anchor);
    if (bots.empty() && !anchorIsBot) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] DispatchGroupEvent: anchor={} bots={} chance={}% event=\"{}\"",
                 anchor->GetName(), bots.size(), chance, eventLine);

    PBC_EventItem ev = BuildBotEvent(bots, eventLine, histLine, chance, CHAT_MSG_PARTY,
                                     /*skipHistoryIfSilent=*/false, /*canCreateEvents=*/true);

    // If the anchor is itself a bot (e.g. a bot leveling up), it was excluded
    // from FindGroupBots() but still needs to receive the histLine and any
    // responder replies in its own history.
    if (anchorIsBot)
        ev.silentBotGuids.push_back(anchor->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_DispatchBotEvent
// ---------------------------------------------------------------------------

void PBC_DispatchBotEvent(Player* bot, const std::string& eventLine,
                          const std::string& histLine, uint32_t chance,
                          bool skipHistoryIfSilent,
                          bool notifyRealPlayers)
{
    if (!PBC_PTR_VALID(bot)) return;

    if (notifyRealPlayers)
        PBC_NotifyRealPlayersInGroup(bot, eventLine);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] DispatchBotEvent: bot={} chance={}% event=\"{}\"",
                 bot->GetName(), chance, eventLine);

    PBC_PushEvent(BuildBotEvent({ bot }, eventLine, histLine, chance, CHAT_MSG_PARTY,
                                skipHistoryIfSilent, /*canCreateEvents=*/true));
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

    const std::string msg = SanitizeChatMessage(rawMsg);
    std::string senderName = sender->GetName();

    // --- Whisper path ---
    if (type == CHAT_MSG_WHISPER)
    {
        if (!PBC_PTR_VALID(whisperTarget)
            || !whisperTarget->GetSession()
            || !whisperTarget->GetSession()->IsBot())
            return;

        std::string eventLine   = senderName + " tells you privately: " + msg;
        std::string historyLine = senderName + " (privately to you): " + msg;

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] Whisper event: {} -> {}: \"{}\" (chance={}%)",
                     senderName, whisperTarget->GetName(), msg, g_PBC_ReplyChanceWhisper);

        PBC_EventItem ev;
        ev.type    = PBC_EventType::Normal;
        ev.eventLine = eventLine;
        ev.histLine  = historyLine;
        ev.chatType  = CHAT_MSG_WHISPER;

        if (RollChance(g_PBC_ReplyChanceWhisper))
        {
            PBC_BotSnapshot snap = PBC_SnapshotBot(whisperTarget);
            snap.whisperTargetGuid = sender->GetGUID();
            snap.whisperTargetName = senderName;
            ev.respondingBots.push_back(std::move(snap));
        }
        else
        {
            ev.silentBotGuids.push_back(whisperTarget->GetGUID().GetCounter());
        }

        PBC_PushEvent(std::move(ev));
        return;
    }

    // --- Say / Yell / Group / Raid ---
    std::string historyLine = senderName + ": " + msg;
    std::string eventLine   = senderName + " says: " + msg;

    bool senderIsBot = sender->GetSession() && sender->GetSession()->IsBot();

    bool isGroupChat = (type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER ||
                        type == CHAT_MSG_RAID  || type == CHAT_MSG_RAID_LEADER  ||
                        type == CHAT_MSG_RAID_WARNING);

    std::vector<Player*> bots = isGroupChat ? FindGroupBots(sender) : FindNearbyBots(sender);

    if (bots.empty()) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Chat event from {} (isBot={}, type={}): \"{}\" — {} bot(s)",
                 senderName, senderIsBot, type, msg, bots.size());

    // Check if the message mentions any specific bot (real player only).
    bool anyMention = false;
    if (!senderIsBot)
    {
        for (Player* bot : bots)
            if (MentionsBot(msg, bot->GetName())) { anyMention = true; break; }
    }

    PBC_EventItem ev;
    ev.type      = PBC_EventType::Normal;
    ev.eventLine = eventLine;
    ev.histLine  = historyLine;
    ev.chatType  = type;

    if (anyMention)
    {
        // Build ordered list of mentioned bots (by position in message).
        std::string lowerMsg = msg;
        std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

        std::vector<std::pair<size_t, Player*>> positions;
        for (Player* bot : bots)
        {
            if (senderIsBot) break;
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
            bool rolled = RollChance(g_PBC_ReplyChanceMention);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll mention bot={} chance={}% -> {}",
                         bot->GetName(), g_PBC_ReplyChanceMention, rolled ? "RESPOND" : "silent");
            if (rolled)
                ev.respondingBots.push_back(PBC_SnapshotBot(bot));
            else
                ev.silentBotGuids.push_back(bot->GetGUID().GetCounter());
        }

        // Non-mentioned bots hear the message but always stay silent.
        for (Player* bot : bots)
        {
            if (mentionedGuids.count(bot->GetGUID().GetCounter())) continue;
            ev.silentBotGuids.push_back(bot->GetGUID().GetCounter());
        }
    }
    else
    {
        // No mention: randomize bot roll order, roll with penalty on success.
        // First bot rolls at g_PBC_ReplyChanceMessage. Each time a bot rolls
        // successfully, the chance for the next bot is reduced by
        // g_PBC_RollPenaltyOnAnswer. If a bot fails its roll, the next bot
        // rolls at the same chance (no penalty). Once the chance reaches 0
        // all remaining bots are silently skipped.
        // Fisher-Yates shuffle using std::rand() (consistent with RollChance)
        std::vector<Player*> shuffledBots = bots;
        for (size_t i = shuffledBots.size(); i > 1; --i)
        {
            size_t j = static_cast<size_t>(std::rand() % i);
            std::swap(shuffledBots[i - 1], shuffledBots[j]);
        }

        uint32 currentChance = g_PBC_ReplyChanceMessage;
        for (Player* bot : shuffledBots)
        {
            if (currentChance == 0)
            {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] Roll message bot={} chance=0% -> silent (no chance left)",
                             bot->GetName());
                ev.silentBotGuids.push_back(bot->GetGUID().GetCounter());
                continue;
            }

            bool rolled = RollChance(currentChance);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll message bot={} chance={}% -> {}",
                         bot->GetName(), currentChance, rolled ? "RESPOND" : "silent");
            if (rolled)
            {
                ev.respondingBots.push_back(PBC_SnapshotBot(bot));
                currentChance = currentChance > g_PBC_RollPenaltyOnAnswer
                    ? currentChance - g_PBC_RollPenaltyOnAnswer : 0;
            }
            else
            {
                ev.silentBotGuids.push_back(bot->GetGUID().GetCounter());
            }
        }
    }

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Chat from {} type={} -> {}/{} bots will respond",
                 senderName, type, ev.respondingBots.size(), bots.size());

    PBC_PushEvent(std::move(ev));
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

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg, Player* receiver)
{
    HandleChatMessage(player, type, msg, receiver);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg)
{
    HandleChatMessage(player, type, msg);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg, Group* /*group*/)
{
    HandleChatMessage(player, type, msg);
    return true;
}

// ---------------------------------------------------------------------------
// Item quality / weapon-type helpers (used by OnPlayerStoreNewItem)
// ---------------------------------------------------------------------------

static std::string ItemQualityStr(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_UNCOMMON: return "uncommon";
        case ITEM_QUALITY_RARE:     return "rare";
        case ITEM_QUALITY_EPIC:     return "epic";
        case ITEM_QUALITY_LEGENDARY:return "legendary";
        case ITEM_QUALITY_ARTIFACT: return "artifact";
        case ITEM_QUALITY_HEIRLOOM: return "heirloom";
        default:                    return "rare";
    }
}

static std::string WeaponTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_WEAPON_AXE:      return "one-handed axe";
        case ITEM_SUBCLASS_WEAPON_AXE2:     return "two-handed axe";
        case ITEM_SUBCLASS_WEAPON_BOW:      return "bow";
        case ITEM_SUBCLASS_WEAPON_GUN:      return "gun";
        case ITEM_SUBCLASS_WEAPON_MACE:     return "one-handed mace";
        case ITEM_SUBCLASS_WEAPON_MACE2:    return "two-handed mace";
        case ITEM_SUBCLASS_WEAPON_POLEARM:  return "polearm";
        case ITEM_SUBCLASS_WEAPON_SWORD:    return "one-handed sword";
        case ITEM_SUBCLASS_WEAPON_SWORD2:   return "two-handed sword";
        case ITEM_SUBCLASS_WEAPON_STAFF:    return "staff";
        case ITEM_SUBCLASS_WEAPON_FIST:     return "fist weapon";
        case ITEM_SUBCLASS_WEAPON_DAGGER:   return "dagger";
        case ITEM_SUBCLASS_WEAPON_THROWN:   return "thrown weapon";
        case ITEM_SUBCLASS_WEAPON_CROSSBOW: return "crossbow";
        case ITEM_SUBCLASS_WEAPON_WAND:     return "wand";
        case ITEM_SUBCLASS_WEAPON_SPEAR:    return "spear";
        default:                            return "weapon";
    }
}

// Returns "a" or "an" based on the first character of the word that follows.
static const char* ArticleFor(const std::string& word)
{
    if (word.empty()) return "a";
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(word[0])));
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an" : "a";
}

// Builds a phrase like "a legendary two-handed mace" or "an epic item".
static std::string BuildItemPhrase(ItemTemplate const* tmpl)
{
    std::string quality = ItemQualityStr(tmpl->Quality);
    std::string type;
    if (tmpl->Class == ITEM_CLASS_WEAPON)
        type = WeaponTypeStr(tmpl->SubClass);
    else
        type = "item";
    return std::string(ArticleFor(quality)) + " " + quality + " " + type;
}

void PBC_PlayerEvents::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(item)) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;

    std::string itemName  = tmpl->Name1;
    std::string phrase    = BuildItemPhrase(tmpl);
    std::string playerName = player->GetName();

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine(playerName + " is picking up " + phrase + " called " + itemName),
        PBC_MakeHistLine(playerName + " picked up " + phrase + " called " + itemName),
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

void PBC_PlayerEvents::OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!g_PBC_Enable || !PBC_PTR_VALID(player)) return;

    WorldSession* sess = player->GetSession();
    bool anchorIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    bool anchorIsBot  = PBC_PTR_VALID(sess) && sess->IsBot();

    if (!anchorIsReal && !BotIsGroupedWithRealPlayer(player)) return;

    auto bots = FindGroupBots(player);
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
    int idx = std::rand() % 5;
    std::string eventLine = PBC_MakeEventLine(name + levelUpEventPhrases[idx]);
    std::string histLine  = PBC_MakeHistLine(name + levelUpHistPhrases[idx]);

    PBC_NotifyRealPlayersInGroup(player, eventLine);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerLevelChanged: player={} level={} bots={} chance={}%",
                 player->GetName(), player->GetLevel(), bots.size(), g_PBC_ReplyChanceLevelUp);

    PBC_EventItem ev = BuildBotEvent(bots, eventLine, histLine,
                                     g_PBC_ReplyChanceLevelUp, CHAT_MSG_PARTY,
                                     /*skipHistoryIfSilent=*/true, /*canCreateEvents=*/true);

    if (anchorIsBot)
        ev.silentBotGuids.push_back(player->GetGUID().GetCounter());

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

    if (!BotIsGroupedWithRealPlayer(killer))
    {
        // Also allow real players as the killer themselves.
        if (!PBC_PTR_VALID(killerSess) || killerSess->IsBot()) return;
    }

    auto bots = FindGroupBots(killer);
    // If killer is a bot they are excluded from FindGroupBots; still proceed so
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

        uint32 areaId = locAnchor->GetAreaId();
        uint32 zoneId = locAnchor->GetZoneId();
        std::string areaName, zoneName;
        if (AreaTableEntry const* a = sAreaTableStore.LookupEntry(areaId))
            areaName = a->area_name[0];
        if (AreaTableEntry const* z = sAreaTableStore.LookupEntry(zoneId))
            zoneName = z->area_name[0];
        if (!areaName.empty() && !zoneName.empty() && areaName != zoneName)
            location = zoneName + ", " + areaName;
        else if (!areaName.empty())
            location = areaName;
        else
            location = zoneName;
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

    // skipHistoryIfSilent=false: boss kills are always written to all histories.
    PBC_EventItem ev = BuildBotEvent(bots, eventLine, histLine,
                                     g_PBC_ReplyChanceBossKill, CHAT_MSG_PARTY,
                                     /*skipHistoryIfSilent=*/false, /*canCreateEvents=*/true);

    // If the killer is itself a bot it was excluded from FindGroupBots() but
    // still needs to receive the event in its own history.
    if (killerIsBot)
        ev.silentBotGuids.push_back(killer->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Strip WoW quest text formatting codes.
// $b / $B  = line break
// $N / $n  = player name placeholder
// $R / $r  = player race placeholder
// $C / $c  = player class placeholder
// $G x:y;  = gender-conditional text
// ---------------------------------------------------------------------------
static std::string StripWowTextCodes(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '$' && i + 1 < text.size())
        {
            char next = text[i + 1];
            // $b / $B -> newline
            if (next == 'b' || next == 'B')
            {
                result += '\n';
                ++i;
                continue;
            }
            // $N / $n / $R / $r / $C / $c -> skip placeholder
            if (next == 'N' || next == 'n' || next == 'R' || next == 'r' ||
                next == 'C' || next == 'c')
            {
                ++i;
                continue;
            }
            // $G x:y; -> skip entire conditional
            if (next == 'G' || next == 'g')
            {
                i += 2; // skip '$G'
                while (i < text.size() && text[i] != ';')
                    ++i;
                continue;
            }
        }
        result += text[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Simple string substitution for quest completion user prompt placeholders.
// ---------------------------------------------------------------------------
static std::string SubstituteQuestVars(const std::string& tmpl,
                                        const std::string& title,
                                        const std::string& description,
                                        const std::string& rewardText)
{
    auto replace = [](std::string s, const std::string& from, const std::string& to) -> std::string
    {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos)
        {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    };

    std::string result = tmpl;
    result = replace(result, "{quest_title}",       title);
    result = replace(result, "{quest_description}", description);
    result = replace(result, "{quest_reward_text}", rewardText);
    return result;
}

void PBC_PlayerEvents::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !quest) return;

    Group* grp = player->GetGroup();
    if (!grp) return;
    if (grp->GetLeaderGUID() != player->GetGUID()) return;

    WorldSession* sess = player->GetSession();
    bool leaderIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    if (!leaderIsReal && !BotIsGroupedWithRealPlayer(player)) return;

    if (g_PBC_QuestCompletionSystemPrompt.empty() || g_PBC_QuestCompletionUserPrompt.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] OnPlayerCompleteQuest: prompts not configured, skipping");
        return;
    }

    std::string questTitle       = StripWowTextCodes(quest->GetTitle());
    std::string questDescription = StripWowTextCodes(quest->GetDetails());
    std::string questRewardText  = StripWowTextCodes(quest->GetOfferRewardText());

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerCompleteQuest: leader={} quest='{}' (id={})",
                 player->GetName(), questTitle, quest->GetQuestId());

    std::string userPrompt = SubstituteQuestVars(
        g_PBC_QuestCompletionUserPrompt,
        questTitle, questDescription, questRewardText);

    auto bots = FindGroupBots(player);
    if (bots.empty()) return;

    // Build a QuestSummarization event: the worker calls the LLM first to
    // generate a narrative summary, then processes each responding bot.
    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = CHAT_MSG_PARTY;
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestCompletionSystemPrompt;
    ev.questUserPrompt    = userPrompt;

    PBC_EventItem rolled = BuildBotEvent(bots, "", "", g_PBC_ReplyChanceQuestCompletion,
                                         CHAT_MSG_PARTY, /*skipHistoryIfSilent=*/false,
                                         /*canCreateEvents=*/true);
    ev.respondingBots  = std::move(rolled.respondingBots);
    ev.silentBotGuids  = std::move(rolled.silentBotGuids);

    PBC_PushEvent(std::move(ev));
}
