#include "pbc_events.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_llm.h"
#include "Log.h"
#include "Player.h"
#include "Creature.h"
#include "Item.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "GridNotifiers.h"
#include "CellImpl.h"
#include "SharedDefines.h"
#include "ObjectMgr.h"
#include "QuestDef.h"

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
                             const std::string& histLine, uint32_t chance)
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
                          bool skipHistoryIfSilent)
{
    if (!PBC_PTR_VALID(bot)) return;

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

    uint32 questionCount = static_cast<uint32>(std::count(msg.begin(), msg.end(), '?'));

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
        // No mention: all bots roll at question or message chance.
        // Question chance scales with the number of '?' in the message, capped at 100%.
        uint32 chance;
        const char* chanceLabel;
        if (questionCount > 0)
        {
            uint32 scaled = g_PBC_ReplyChanceQuestion * questionCount;
            chance = scaled > 100 ? 100 : scaled;
            chanceLabel = "question";
        }
        else
        {
            chance = g_PBC_ReplyChanceMessage;
            chanceLabel = "message";
        }
        for (Player* bot : bots)
        {
            bool rolled = RollChance(chance);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll {} bot={} chance={}% -> {}",
                         chanceLabel, bot->GetName(), chance, rolled ? "RESPOND" : "silent");
            if (rolled)
                ev.respondingBots.push_back(PBC_SnapshotBot(bot));
            else
                ev.silentBotGuids.push_back(bot->GetGUID().GetCounter());
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
    PLAYERHOOK_ON_PLAYER_ENTER_COMBAT,
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

void PBC_PlayerEvents::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(item)) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;

    std::string itemName = tmpl->Name1;
    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine(player->GetName() + " is picking up [" + itemName + "]"),
        PBC_MakeHistLine(player->GetName() + " picked up [" + itemName + "]"),
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

void PBC_PlayerEvents::OnPlayerEnterCombat(Player* player, Unit* enemy)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(enemy)) return;

    // Only react when the party LEADER enters combat.
    Group* grp = player->GetGroup();
    if (!grp) return;
    if (grp->GetLeaderGUID() != player->GetGUID()) return;

    WorldSession* sess = player->GetSession();
    bool leaderIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    if (!leaderIsReal && !BotIsGroupedWithRealPlayer(player)) return;

    std::string eventLine = PBC_MakeEventLine("Your party is entering combat with " + std::string(enemy->GetName()));
    std::string histLine  = PBC_MakeHistLine("Your party fought " + std::string(enemy->GetName()));

    auto bots = FindGroupBots(player);
    if (bots.empty()) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerEnterCombat: leader={} bots={} chance={}%",
                 player->GetName(), bots.size(), g_PBC_ReplyChanceCombat);

    // skipHistoryIfSilent=true: worker skips silent history if no one responds.
    PBC_PushEvent(BuildBotEvent(bots, eventLine, histLine,
                                g_PBC_ReplyChanceCombat, CHAT_MSG_PARTY,
                                /*skipHistoryIfSilent=*/true, /*canCreateEvents=*/true));
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
