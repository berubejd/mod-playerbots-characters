#include "pbc_event_dispatch.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_utils.h"
#include "pbc_group_helpers.h"
#include "pbc_log.h"

#include "Player.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "WorldSession.h"
#include "SharedDefines.h"

#include <algorithm>
#include <random>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Narrator formatting helpers
// ---------------------------------------------------------------------------
std::string PBC_MakeEventLine(const std::string& text) { return "*" + text + "*"; }
std::string PBC_MakeHistLine(const std::string& text)  { return "Narrator: *" + text + "*"; }

// ---------------------------------------------------------------------------
// PBC_NotifyRealPlayersInGroup
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
// PBC_PushEvent
// ---------------------------------------------------------------------------
void PBC_PushEvent(PBC_EventItem item)
{
    std::lock_guard<std::mutex> lock(g_PBC_EventQueueMutex);
    g_PBC_EventQueue.push(std::move(item));
}

// ---------------------------------------------------------------------------
// AddTrackedPlayersToEvent
// ---------------------------------------------------------------------------
void AddTrackedPlayersToEvent(PBC_EventItem& ev, Player* anchor)
{
    if (!g_PBC_TrackPlayerCharacter) return;
    if (!PBC_PTR_VALID(anchor)) return;

    auto realPlayers = PBC_FindRealPlayersInGroup(anchor);
    for (Player* rp : realPlayers)
    {
        uint64_t guid = rp->GetGUID().GetCounter();
        ev.silentCharGuids.push_back(guid);
        ev.playerCharGuids.push_back(guid);
    }

    WorldSession* anchorSess = anchor->GetSession();
    if (PBC_PTR_VALID(anchorSess) && !anchorSess->IsBot())
    {
        uint64_t guid = anchor->GetGUID().GetCounter();
        if (std::find(ev.playerCharGuids.begin(), ev.playerCharGuids.end(), guid) == ev.playerCharGuids.end())
        {
            ev.silentCharGuids.push_back(guid);
            ev.playerCharGuids.push_back(guid);
        }
    }
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
        PBC_Log(PBC_LogLevel::DEBUG, "DispatchGroupEvent: skipped — no real player in group (anchor={})", anchor->GetName());
        return;
    }

    if (notifyRealPlayers)
        PBC_NotifyRealPlayersInGroup(anchor, eventLine);

    auto bots = PBC_FindGroupBots(anchor);
    if (bots.empty() && !anchorIsBot) return;

    std::shuffle(bots.begin(), bots.end(), PBC_GetRNG());

    PBC_Log(PBC_LogLevel::DEBUG, "DispatchGroupEvent: anchor={} bots={} chance={}% event=\"{}\"",
             anchor->GetName(), bots.size(), chance, eventLine);

    PBC_EventItem ev;
    ev.type            = PBC_EventType::Normal;
    ev.eventLine       = eventLine;
    ev.histLine        = histLine;
    ev.chatType        = CHAT_MSG_PARTY;
    ev.canCreateEvents = true;

    PBC_RollBotsWithPenalty(ev, bots, chance, "event");

    if (anchorIsBot)
        ev.silentCharGuids.push_back(anchor->GetGUID().GetCounter());

    AddTrackedPlayersToEvent(ev, anchor);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_RollGroupBotsIntoEvent
// ---------------------------------------------------------------------------
bool PBC_RollGroupBotsIntoEvent(PBC_EventItem& ev, Player* player,
                                 uint32_t chance, const char* debugLabel)
{
    auto bots = PBC_FindGroupBots(player);
    if (bots.empty()) return false;

    std::shuffle(bots.begin(), bots.end(), PBC_GetRNG());
    PBC_RollBotsWithPenalty(ev, bots, chance, debugLabel);
    return true;
}

// ---------------------------------------------------------------------------
// PBC_RollBotsWithPenalty
// ---------------------------------------------------------------------------
void PBC_RollBotsWithPenalty(PBC_EventItem& ev,
                              const std::vector<Player*>& bots,
                              uint32_t baseChance,
                              const char* debugLabel)
{
    uint32_t currentChance = baseChance;
    for (Player* bot : bots)
    {
        if (currentChance == 0)
        {
            PBC_Log(PBC_LogLevel::DEBUG, "Roll {} character={} chance=0% -> silent (no chance left)",
                     debugLabel, bot->GetName());
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
            continue;
        }

        uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), currentChance);
        bool rolled = PBC_RollChance(effectiveChance);
        PBC_Log(PBC_LogLevel::DEBUG, "Roll {} character={} chance={}% (base={}% mod={}) -> {}",
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
// ---------------------------------------------------------------------------
void PBC_RollBotsForMessage(PBC_EventItem& ev,
                             const std::vector<Player*>& bots,
                             const std::string& message)
{
    bool anyMention = false;
    for (Player* bot : bots)
        if (PBC_MentionsCharacter(message, bot->GetName())) { anyMention = true; break; }

    if (anyMention)
    {
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

        std::unordered_set<uint64_t> mentionedGuids;
        for (auto& [pos, bot] : positions)
        {
            mentionedGuids.insert(bot->GetGUID().GetCounter());
            uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), g_PBC_ReplyChanceMention);
            bool rolled = PBC_RollChance(effectiveChance);
            PBC_Log(PBC_LogLevel::DEBUG, "Roll mention character={} chance={}% -> {}",
                     bot->GetName(), effectiveChance, rolled ? "RESPOND" : "silent");
            if (rolled)
                ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
            else
                ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }

        uint32_t mentionPenalty = g_PBC_RollPenaltyOnAnswer * mentionedGuids.size();
        uint32_t baseChance = g_PBC_ReplyChanceMention > mentionPenalty
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
    std::string targetName = target->GetName();
    std::string eventLine   = senderName + " tells you privately: " + msg;
    std::string historyLine = senderName + " (privately to you): " + msg;

    PBC_Log(PBC_LogLevel::DEBUG, "Whisper event: {} -> {}: \"{}\" (chance={}%)",
             senderName, targetName, msg, g_PBC_ReplyChanceWhisper);

    PBC_EventItem ev;
    ev.type      = PBC_EventType::Normal;
    ev.eventLine = eventLine;
    ev.histLine  = historyLine;
    ev.chatType  = CHAT_MSG_WHISPER;

    ev.whisperSenderName = senderName;
    ev.whisperTargetName = targetName;

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

    if (g_PBC_TrackPlayerCharacter)
    {
        WorldSession* senderSess = sender->GetSession();
        if (PBC_PTR_VALID(senderSess) && !senderSess->IsBot())
        {
            uint64_t senderGuid = sender->GetGUID().GetCounter();
            ev.playerCharGuids.push_back(senderGuid);
        }
    }

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_DispatchTriggerEvent
// ---------------------------------------------------------------------------
void PBC_DispatchTriggerEvent(Player* bot)
{
    if (!PBC_PTR_VALID(bot)) return;

    uint32_t chatType = bot->GetGroup() ? CHAT_MSG_PARTY : CHAT_MSG_SAY;

    PBC_EventItem ev;
    ev.type             = PBC_EventType::Normal;
    ev.eventLine        = PBC_MakeEventLine("you feel the urge to say something");
    ev.histLine         = "";
    ev.chatType         = chatType;
    ev.canCreateEvents  = false;

    PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(bot);
    ev.respondingChars.push_back(std::move(snap));

    if (chatType == CHAT_MSG_PARTY)
    {
        auto groupBots = PBC_FindGroupBots(bot);
        for (Player* groupBot : groupBots)
            ev.silentCharGuids.push_back(groupBot->GetGUID().GetCounter());
    }

    AddTrackedPlayersToEvent(ev, bot);

    PBC_Log(PBC_LogLevel::DEBUG, "Trigger event: character={} chatType={} silentChars={}",
             bot->GetName(), chatType, ev.silentCharGuids.size());

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

    bool isGroupChat = (chatType == CHAT_MSG_PARTY || chatType == CHAT_MSG_PARTY_LEADER ||
                        chatType == CHAT_MSG_RAID  || chatType == CHAT_MSG_RAID_LEADER  ||
                        chatType == CHAT_MSG_RAID_WARNING);

    std::vector<Player*> bots = isGroupChat ? PBC_FindGroupBots(sender) : PBC_FindNearbyBots(sender);
    if (bots.empty())
    {
        PBC_Log(PBC_LogLevel::DEBUG, "Chat message event from {} type={} discarded — no bots found ({})",
                 senderName, chatType, isGroupChat ? "group empty" : "none nearby");
        return;
    }

    PBC_Log(PBC_LogLevel::DEBUG, "Chat message event from {} type={} ({} bots): \"{}\"",
             senderName, chatType, bots.size(), msg);

    PBC_EventItem ev;
    ev.type            = PBC_EventType::Normal;
    ev.eventLine       = eventLine;
    ev.histLine        = historyLine;
    ev.chatType        = chatType ? chatType : CHAT_MSG_PARTY;
    ev.canCreateEvents = canCreateEvents;

    PBC_RollBotsForMessage(ev, bots, msg);

    AddTrackedPlayersToEvent(ev, sender);

    PBC_Log(PBC_LogLevel::DEBUG, "Chat from {} type={} -> {}/{} bots will respond",
             senderName, chatType, ev.respondingChars.size(), bots.size());

    PBC_PushEvent(std::move(ev));
}
