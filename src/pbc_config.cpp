#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_events.h"
#include "pbc_utils.h"
#include "Config.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Chat.h"
#include "Group.h"
#include "SharedDefines.h"

#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Global variable definitions
// ---------------------------------------------------------------------------

bool     g_PBC_Enable              = true;
bool     g_PBC_DebugEnabled        = false;
bool     g_PBC_DebugShowFullRequest = false;
bool     g_PBC_DisplayNarratorEvents = true;

std::string g_PBC_APIType          = "openai";
std::string g_PBC_BaseUrl          = "";
std::string g_PBC_ApiKey           = "";
std::string g_PBC_Model            = "";
int         g_PBC_MaxResponseTokens = 120;
double      g_PBC_Temperature      = 1.0;
std::string g_PBC_ModelExtraParameters;
int         g_PBC_RequestTimeoutSec = 30;

uint32_t    g_PBC_MaxCtx                    = 0;
uint32_t    g_PBC_CondensationPreservedLines = 50;

std::string g_PBC_SystemPrompt;
std::string g_PBC_UserPrompt;
std::string g_PBC_CondensationSystemPrompt;
std::string g_PBC_CondensationUserPrompt;
std::string g_PBC_DefaultCharacterDescription;
std::string g_PBC_CharacterContext;

std::string g_PBC_RelationshipUpdateSystemPrompt;
std::string g_PBC_RelationshipUpdateUserPrompt;
uint32_t    g_PBC_RelationshipUpdateThreshold = 100;

std::string g_PBC_CharacterCardsPath = "../../../modules/mod-playerbots-characters/characters";

uint32_t g_PBC_ReplyChanceWhisper   = 100;
uint32_t g_PBC_ReplyChanceMention   = 100;
uint32_t g_PBC_ReplyChanceMessage   = 100;
uint32_t g_PBC_RollPenaltyOnAnswer  = 40;
uint32_t g_PBC_ReplyChanceItem     = 5;
uint32_t g_PBC_ReplyChanceDuel     = 5;
uint32_t g_PBC_ReplyChanceLevelUp  = 5;
uint32_t g_PBC_ReplyChanceLocation        = 5;
uint32_t g_PBC_ReplyChanceBossKill       = 35;
uint32_t g_PBC_ReplyChanceQuestCompleted = 20;
uint32_t g_PBC_ReplyChanceQuestTaken     = 10;

std::string g_PBC_QuestCompletedSystemPrompt;
std::string g_PBC_QuestCompletedUserPrompt;
std::string g_PBC_QuestTakenSystemPrompt;
std::string g_PBC_QuestTakenUserPrompt;

std::vector<std::string> g_PBC_Blacklist;

std::queue<PBC_PendingAction> g_PBC_PendingActions;
std::mutex                    g_PBC_PendingActionsMutex;

std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
std::mutex                          g_PBC_PendingEventRequestsMutex;

std::queue<PBC_EventItem>  g_PBC_EventQueue;
std::mutex                 g_PBC_EventQueueMutex;
std::atomic<bool>          g_PBC_EventThreadDone{ true };

std::unordered_map<uint64_t, std::deque<std::string>> g_PBC_ChatHistory;
std::mutex g_PBC_HistoryMutex;

std::unordered_map<uint64_t, std::vector<std::string>> g_PBC_CardAdditions;
std::mutex g_PBC_CardMutex;

std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
std::mutex g_PBC_RelationshipsMutex;

std::unordered_map<uint64_t, PBC_LocationState> g_PBC_LocationStates;
std::unordered_map<uint64_t, std::string>        g_PBC_BotLastLocations;
uint32_t g_PBC_LocationPollAccum = 0;

// ---------------------------------------------------------------------------
// PBC_PushEvent
// ---------------------------------------------------------------------------

void PBC_PushEvent(PBC_EventItem item)
{
    std::lock_guard<std::mutex> lock(g_PBC_EventQueueMutex);
    g_PBC_EventQueue.push(std::move(item));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> SplitByComma(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        if (!tok.empty())
            out.push_back(tok);
    }
    return out;
}

// ---------------------------------------------------------------------------
// PBC_CondenseInline
//
// Runs condensation synchronously inside the event thread for a bot whose
// history has exceeded g_PBC_MaxCtx.  Calls the LLM, writes the card
// addition directly (thread-safe), and resets the in-memory history to a
// short tail.  Does NOT touch any Player* objects.
// ---------------------------------------------------------------------------
static void PBC_CondenseInline(PBC_BotSnapshot& snap,
                                const std::string& sysPrompt,
                                const std::string& userPromptTmpl)
{
    if (sysPrompt.empty() || userPromptTmpl.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] CondenseInline: prompts not configured, skipping for bot={}", snap.botName);
        return;
    }

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] CondenseInline: bot={} history_lines={}", snap.botName, snap.history.size());

    std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(snap, userPromptTmpl);
    PBC_LLMResult res = PBC_CallLLM(sysPrompt, userPrompt, /*maxTokensOverride=*/-1);

    if (!res.success || res.text.empty())
    {
        LOG_WARN("server.loading", "[PBC] CondenseInline: LLM failed for bot={}", snap.botName);
        return;
    }

    // Write card addition
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
        g_PBC_CardAdditions[snap.botGuidRaw].push_back(res.text);
    }
    DB_InsertCardAddition(snap.botGuidRaw, res.text);

    // Keep only the last N lines as the tail (configured via PBC.CondensationPreservedLines)
    const size_t kTailLines = static_cast<size_t>(g_PBC_CondensationPreservedLines);
    std::deque<std::string> tail;
    for (size_t i = snap.history.size() > kTailLines ? snap.history.size() - kTailLines : 0;
         i < snap.history.size(); ++i)
        tail.push_back(snap.history[i]);

    // Reset global history
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        g_PBC_ChatHistory[snap.botGuidRaw] = tail;
    }
    // Delete from DB and re-insert tail
    DB_DeleteHistoryForBot(snap.botGuidRaw);
    for (const auto& line : tail)
        DB_InsertHistoryLine(snap.botGuidRaw, line);

    // Update snapshot's local history to match
    snap.history = tail;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] CondenseInline: condensed bot={} summary_len={} tail_lines={}",
                 snap.botName, res.text.size(), tail.size());
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
static void PBC_ProcessEventItem(PBC_EventItem ev)
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
            "SELECT bot_guid, message FROM mod_pbc_chat_history ORDER BY id ASC"
        );

        {
            std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
            g_PBC_ChatHistory.clear();

            if (result)
            {
                do {
                    uint64_t    botGuid = (*result)[0].Get<uint64_t>();
                    std::string msg     = (*result)[1].Get<std::string>();
                    g_PBC_ChatHistory[botGuid].push_back(std::move(msg));
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
        std::deque<std::string> preCondensationHistory = ev.condensationBot.history;

        PBC_CondenseInline(ev.condensationBot, condenseSysPrompt, condenseUsrTmpl);

        // Condensation is always a trigger for relationship updates for all party
        // members — no threshold check needed here.  The history is about to be
        // wiped, so we must capture relationships now before that context is gone.
        // We pass mention_count=0 to reset the baseline; the normal threshold
        // tracking will accumulate fresh counts from the new post-condensation history.
        {
            const PBC_BotSnapshot& snap = ev.condensationBot;
            if (!g_PBC_RelationshipUpdateSystemPrompt.empty() &&
                !g_PBC_RelationshipUpdateUserPrompt.empty() &&
                !snap.partyMemberNames.empty())
            {
                for (const auto& memberName : snap.partyMemberNames)
                {
                    std::string currentRel;
                    {
                        std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
                        auto botIt = g_PBC_Relationships.find(snap.botGuidRaw);
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
                    PBC_BotSnapshot relSnap = snap;
                    relSnap.history = preCondensationHistory;

                    PBC_EventItem relEv;
                    relEv.type                       = PBC_EventType::RelationshipUpdate;
                    relEv.relationshipBot            = std::move(relSnap);
                    relEv.relationshipTargetName     = memberName;
                    relEv.relationshipTargetInfo     = PBC_BuildTargetInfo(memberName);
                    relEv.relationshipCurrentText    = currentRel;
                    relEv.relationshipMentionTotal   = 0; // reset baseline for post-condensation tracking
                    relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
                    relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

                    PBC_PushEvent(std::move(relEv));

                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading",
                                 "[PBC] Condensation: queuing relationship update for bot={} target={}",
                                 snap.botName, memberName);
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
                LOG_INFO("server.loading", "[PBC] RelationshipUpdate: prompts not configured, skipping for bot={}",
                         ev.relationshipBot.botName);
            g_PBC_EventThreadDone.store(true);
            return;
        }

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] RelationshipUpdate: bot={} target={}",
                     ev.relationshipBot.botName, ev.relationshipTargetName);

        // Build the user prompt: substitute {character_card}, {chat_history},
        // {relationship_target}, {target_current_relationship}.
        std::string userPrompt = ev.relationshipUserPromptTmpl;
        PBC_ExpandNewlineEscapes(userPrompt);

        PBC_ReplaceToken(userPrompt, "character_card",           ev.relationshipBot.characterCard);
        { std::ostringstream histOss; for (const auto& line : ev.relationshipBot.history) histOss << line << "\n"; PBC_ReplaceToken(userPrompt, "chat_history", histOss.str()); }
        PBC_ReplaceToken(userPrompt, "relationship_target",      ev.relationshipTargetInfo);
        PBC_ReplaceToken(userPrompt, "target_current_relationship", ev.relationshipCurrentText);

        PBC_LLMResult res = PBC_CallLLM(ev.relationshipSystemPrompt, userPrompt, /*maxTokensOverride=*/-1);

        if (!res.success || res.text.empty())
        {
            LOG_WARN("server.loading", "[PBC] RelationshipUpdate: LLM failed for bot={} target={}",
                     ev.relationshipBot.botName, ev.relationshipTargetName);
            g_PBC_EventThreadDone.store(true);
            return;
        }

        // Store the updated relationship text and the mention count at time of
        // update, so server restarts don't trigger redundant LLM calls.
        {
            std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
            auto& entry = g_PBC_Relationships[ev.relationshipBot.botGuidRaw][ev.relationshipTargetName];
            entry.text = res.text;
            entry.mentionCountAtLastUpdate = ev.relationshipMentionTotal;
        }
        DB_UpsertRelationship(ev.relationshipBot.botGuidRaw, ev.relationshipTargetName,
                              res.text, ev.relationshipMentionTotal);

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] RelationshipUpdate: updated bot={} target={} mentions={} text=\"{}\"",
                     ev.relationshipBot.botName, ev.relationshipTargetName,
                     ev.relationshipMentionTotal, res.text);

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
        // Fall through to Normal processing
    }

    // -----------------------------------------------------------------------
    // Normal event processing
    // -----------------------------------------------------------------------
    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] ProcessEvent: type={} respondingBots={} silentBots={} event=\"{}\"",
                 static_cast<int>(ev.type), ev.respondingBots.size(), ev.silentBotGuids.size(), ev.eventLine);

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
    // Global history writes for responding bots are deferred until after the
    // loop so that every bot receives the complete, correctly-ordered
    // conversation (histLine + ALL replies) rather than only the replies from
    // bots that preceded it.
    //
    // During the loop we only update each bot's LOCAL snapshot history so the
    // next bot in the chain sees the previous reply when building its prompt.
    std::vector<std::string> completedReplyLines;

    // Post deferred "thinks..." notifications to the main thread so they
    // appear right before the LLM call starts, not at event-creation time.
    for (const PBC_BotSnapshot& snap : ev.respondingBots)
    {
        PBC_PendingAction action;
        action.botGuid             = snap.botObjGuid;
        action.text                = PBC_MakeEventLine(snap.botName + " thinks...");
        action.isThinkNotification = true;

        std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
        g_PBC_PendingActions.push(std::move(action));
    }

    for (PBC_BotSnapshot& snap : ev.respondingBots)
    {
        // Condense inline if over token budget before building the prompt.
        int histTokens = PBC_EstimateHistoryTokens(snap.botGuidRaw);
        if (histTokens > static_cast<int>(g_PBC_MaxCtx))
        {
            PBC_CondenseInline(snap, condenseSysPrompt, condenseUsrTmpl);
            // snap.history is now updated to the condensed tail by CondenseInline.
        }

        // Build user prompt from snapshot (uses snap.history for {chat_history}).
        std::string userPrompt = PBC_BuildUserPromptFromSnapshot(snap, currentEvent);

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] ProcessEvent: calling LLM for bot={} event=\"{}\"",
                     snap.botName, currentEvent);

        PBC_LLMResult res = PBC_CallLLM(sysPrompt, userPrompt);

        if (!res.success || res.text.empty())
        {
            LOG_WARN("server.loading", "[PBC] ProcessEvent: LLM failed/empty for bot={}", snap.botName);
            // Don't advance currentEvent — the next bot reacts to the same event.
            // Global history for this bot is handled in the deferred write below.
            continue;
        }

        // Build the history line for this bot's own reply.
        std::string replyLine;
        if (ev.chatType == CHAT_MSG_WHISPER && !snap.whisperTargetName.empty())
            replyLine = snap.botName + " (privately to " + snap.whisperTargetName + "): " + res.text;
        else
            replyLine = snap.botName + ": " + res.text;

        // Update the snapshot's local history so subsequent bots in the chain
        // see all previous replies when building their prompts.
        // Global history is written in the deferred pass after the loop.
        if (!ev.histLine.empty())
            snap.history.push_back(ev.histLine);
        snap.history.push_back(replyLine);

        // Collect reply for deferred global history write.
        completedReplyLines.push_back(replyLine);

        // Post chat send to main thread.
        {
            PBC_PendingAction action;
            action.botGuid    = snap.botObjGuid;
            action.targetGuid = snap.whisperTargetGuid;
            action.chatType   = ev.chatType;
            action.text       = res.text;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(action));
        }

        // Advance the chain: the next bot reacts to this bot's reply.
        currentEvent = snap.botName + " says: " + res.text;

        // Remember the last successful reply details for the secondary event.
        lastResponderGuid = snap.botGuidRaw;
        lastReplyLine     = replyLine;
        lastEventLine     = currentEvent;

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] ProcessEvent: bot={} replied: \"{}\"", snap.botName, res.text);
    }

    // -----------------------------------------------------------------------
    // Deferred global history write for all responding bots.
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
        for (const PBC_BotSnapshot& snap : ev.respondingBots)
        {
            if (!ev.histLine.empty())
                PBC_AppendHistory(snap.botGuidRaw, ev.histLine);
            for (const auto& replyLine : completedReplyLines)
                PBC_AppendHistory(snap.botGuidRaw, replyLine);
        }
    }

    // -----------------------------------------------------------------------
    // Secondary event: if this event can spawn message events and at least
    // one bot replied, ask the main thread to push a new message event for
    // any other group bots that did NOT participate in this event.  We post
    // exactly ONE request here — after the full responder chain — using the
    // last successful reply as the trigger.  This prevents intermediate
    // replies from generating extra events when multiple bots responded.
    //
    // Note: we do NOT require silentBotGuids to be non-empty.  Bot-specific
    // events (e.g. location) have no silent bots in the original event but
    // other group bots still need to hear the reply.  OnUpdate handles the
    // case where there are no eligible targets gracefully (pushes nothing).
    // -----------------------------------------------------------------------
    if (ev.canCreateEvents && lastResponderGuid != 0)
    {
        // Excluded = every bot that already participated (responders + silent).
        std::unordered_set<uint64_t> excluded;
        for (const auto& rs : ev.respondingBots)
            excluded.insert(rs.botGuidRaw);
        for (uint64_t g : ev.silentBotGuids)
            excluded.insert(g);

        PBC_PendingEventRequest req;
        req.eventLine        = lastEventLine;   // "<LastBot> says: <text>"
        req.histLine         = lastReplyLine;   // "<LastBot>: <text>"
        req.originHistLine   = ev.histLine;     // original trigger (Narrator line, etc.)
        req.chatType         = ev.chatType;
        req.anchorBotGuid    = lastResponderGuid;
        // Collect the GUIDs of all original responders so OnUpdate can pass
        // them as replyOnlyBotGuids on the secondary event — they already have
        // histLine but still need to receive any new replies.
        for (const auto& rs : ev.respondingBots)
            req.originBotGuids.push_back(rs.botGuidRaw);
        req.excludedBotGuids = std::move(excluded);

        // Capture debug values before the move.
        size_t dbgExcluded = req.excludedBotGuids.size();
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
                     ev.silentBotGuids.size(),
                     dbgEvent);
    }

    // -----------------------------------------------------------------------
    // Update silent bots' history.
    // For low-chance events (combat, location, level-up) with no responders
    // we skip this entirely to avoid cluttering history with silent entries.
    // -----------------------------------------------------------------------
    if (ev.skipHistoryIfSilent && ev.respondingBots.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] ProcessEvent: no responders and skipHistoryIfSilent=true — skipping silent history");
    }
    else if (!ev.histLine.empty())
    {
        for (uint64_t guid : ev.silentBotGuids)
            PBC_AppendHistory(guid, ev.histLine);

        // Propagate all responding bots' replies to silent peers so their
        // history stays current.  completedReplyLines holds every reply line
        // in the order they were generated, which is exactly what silent bots
        // need to see.
        if (!completedReplyLines.empty() && ev.chatType != CHAT_MSG_WHISPER)
        {
            for (uint64_t guid : ev.silentBotGuids)
            {
                for (const auto& replyLine : completedReplyLines)
                    PBC_AppendHistory(guid, replyLine);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Propagate replies to replyOnlyBotGuids.
    // These bots already have histLine in their history (they were the
    // original responders that triggered this secondary event) but still need
    // to receive any new replies generated here.  histLine is NOT re-written.
    // -----------------------------------------------------------------------
    if (!completedReplyLines.empty() && !ev.replyOnlyBotGuids.empty()
        && ev.chatType != CHAT_MSG_WHISPER)
    {
        for (uint64_t guid : ev.replyOnlyBotGuids)
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
        !ev.respondingBots.empty())
    {
        for (const PBC_BotSnapshot& snap : ev.respondingBots)
        {
            if (snap.partyMemberNames.empty()) continue;

            // Read the bot's full current history from the global map.
            std::deque<std::string> fullHistory;
            {
                std::lock_guard<std::mutex> lh(g_PBC_HistoryMutex);
                auto hIt = g_PBC_ChatHistory.find(snap.botGuidRaw);
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
                    auto botIt = g_PBC_Relationships.find(snap.botGuidRaw);
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
                PBC_BotSnapshot relSnap = snap;
                relSnap.history         = fullHistory;

                PBC_EventItem relEv;
                relEv.type                       = PBC_EventType::RelationshipUpdate;
                relEv.relationshipBot            = std::move(relSnap);
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
                             "[PBC] MentionTracker: bot={} target={} total_mentions={} new_since_last={} — relationship update queued",
                             snap.botName, memberName, total, newMentions);
            }
        }
    }

    g_PBC_EventThreadDone.store(true);
}

// ---------------------------------------------------------------------------
// PBC_LoadConfig
// ---------------------------------------------------------------------------

void PBC_LoadConfig(bool /*isStartup*/)
{
    g_PBC_Enable              = sConfigMgr->GetOption<bool>("PBC.Enable", true);
    g_PBC_DebugEnabled        = sConfigMgr->GetOption<bool>("PBC.DebugEnabled", false);
    g_PBC_DebugShowFullRequest = sConfigMgr->GetOption<bool>("PBC.DebugShowFullRequest", false);
    g_PBC_DisplayNarratorEvents = sConfigMgr->GetOption<bool>("PBC.DisplayNarratorEvents", true);

    g_PBC_APIType              = sConfigMgr->GetOption<std::string>("PBC.APIType", "openai");
    g_PBC_BaseUrl              = sConfigMgr->GetOption<std::string>("PBC.BaseUrl", "");
    g_PBC_ApiKey               = sConfigMgr->GetOption<std::string>("PBC.ApiKey", "");
    g_PBC_Model               = sConfigMgr->GetOption<std::string>("PBC.Model", "");
    g_PBC_MaxResponseTokens   = sConfigMgr->GetOption<int>("PBC.MaxResponseLength", 120);
    g_PBC_Temperature         = std::round(static_cast<double>(sConfigMgr->GetOption<float>("PBC.Temperature", 1.0f)) * 100.0) / 100.0;
    g_PBC_ModelExtraParameters = sConfigMgr->GetOption<std::string>("PBC.ModelExtraParameters", "");
    g_PBC_RequestTimeoutSec   = sConfigMgr->GetOption<int>("PBC.RequestTimeoutSec", 30);

    g_PBC_MaxCtx                     = sConfigMgr->GetOption<uint32_t>("PBC.MaxCtx", 0);
    g_PBC_CondensationPreservedLines = sConfigMgr->GetOption<uint32_t>("PBC.CondensationPreservedLines", 50);

    g_PBC_SystemPrompt              = sConfigMgr->GetOption<std::string>("PBC.SystemPrompt", "");
    g_PBC_UserPrompt                = sConfigMgr->GetOption<std::string>("PBC.UserPrompt", "");
    g_PBC_CondensationSystemPrompt  = sConfigMgr->GetOption<std::string>("PBC.CondensationSystemPrompt", "");
    g_PBC_CondensationUserPrompt    = sConfigMgr->GetOption<std::string>("PBC.CondensationUserPrompt", "");
    g_PBC_DefaultCharacterDescription = sConfigMgr->GetOption<std::string>("PBC.DefaultCharacterDescription", "");
    g_PBC_CharacterContext          = sConfigMgr->GetOption<std::string>("PBC.CharacterContext", "");

    g_PBC_CharacterCardsPath  = sConfigMgr->GetOption<std::string>("PBC.CharacterCardsPath",
                                    "../../../modules/mod-playerbots-characters/characters");

    g_PBC_ReplyChanceWhisper   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceWhisper", 100);
    g_PBC_ReplyChanceMention   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceMention", 100);
    g_PBC_ReplyChanceMessage   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceMessage", 100);
    g_PBC_RollPenaltyOnAnswer  = sConfigMgr->GetOption<uint32_t>("PBC.RollPenaltyOnAnswer", 40);
    g_PBC_ReplyChanceItem     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceItem", 5);
    g_PBC_ReplyChanceDuel     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceDuel", 5);
    g_PBC_ReplyChanceLevelUp  = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLevelUp", 5);
    g_PBC_ReplyChanceLocation        = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLocation", 5);
    g_PBC_ReplyChanceBossKill       = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceBossKill", 35);
    g_PBC_ReplyChanceQuestCompleted = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestCompleted", 20);
    g_PBC_ReplyChanceQuestTaken     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestTaken", 10);

    g_PBC_QuestCompletedSystemPrompt = sConfigMgr->GetOption<std::string>("PBC.QuestCompletedSystemPrompt", "");
    g_PBC_QuestCompletedUserPrompt   = sConfigMgr->GetOption<std::string>("PBC.QuestCompletedUserPrompt", "");
    g_PBC_QuestTakenSystemPrompt     = sConfigMgr->GetOption<std::string>("PBC.QuestTakenSystemPrompt", "");
    g_PBC_QuestTakenUserPrompt       = sConfigMgr->GetOption<std::string>("PBC.QuestTakenUserPrompt", "");

    g_PBC_RelationshipUpdateSystemPrompt = sConfigMgr->GetOption<std::string>("PBC.RelationshipUpdateSystemPrompt", "");
    g_PBC_RelationshipUpdateUserPrompt   = sConfigMgr->GetOption<std::string>("PBC.RelationshipUpdateUserPrompt", "");
    g_PBC_RelationshipUpdateThreshold    = sConfigMgr->GetOption<uint32_t>("PBC.RelationshipUpdateThreshold", 100);

    std::string blacklistStr = sConfigMgr->GetOption<std::string>("PBC.Blacklist", "");
    g_PBC_Blacklist = SplitByComma(blacklistStr);

    // -----------------------------------------------------------------------
    // Validate required settings when the module is enabled.
    //
    // AzerothCore does NOT fall back to .conf.dist for missing parameters —
    // it uses the C++ default passed to GetOption().  To avoid silent
    // misconfiguration, every parameter that is essential for the module to
    // work correctly must be present and non-empty in the user's .conf file.
    // If any required parameter is missing the module is disabled with a
    // clear error; the server itself keeps running.
    // -----------------------------------------------------------------------
    if (g_PBC_Enable)
    {
        // Each entry: { config key, reference to the loaded value }
        struct RequiredCheck { const char* key; std::string const& value; };
        const RequiredCheck requiredStrings[] = {
            { "PBC.BaseUrl",                        g_PBC_BaseUrl                        },
            { "PBC.Model",                          g_PBC_Model                          },
            { "PBC.SystemPrompt",                   g_PBC_SystemPrompt                   },
            { "PBC.UserPrompt",                     g_PBC_UserPrompt                     },
            { "PBC.CondensationSystemPrompt",       g_PBC_CondensationSystemPrompt       },
            { "PBC.CondensationUserPrompt",         g_PBC_CondensationUserPrompt         },
            { "PBC.DefaultCharacterDescription",    g_PBC_DefaultCharacterDescription    },
            { "PBC.CharacterContext",               g_PBC_CharacterContext               },
            { "PBC.QuestCompletedSystemPrompt",     g_PBC_QuestCompletedSystemPrompt     },
            { "PBC.QuestCompletedUserPrompt",       g_PBC_QuestCompletedUserPrompt       },
            { "PBC.QuestTakenSystemPrompt",         g_PBC_QuestTakenSystemPrompt         },
            { "PBC.QuestTakenUserPrompt",           g_PBC_QuestTakenUserPrompt           },
            { "PBC.RelationshipUpdateSystemPrompt", g_PBC_RelationshipUpdateSystemPrompt },
            { "PBC.RelationshipUpdateUserPrompt",   g_PBC_RelationshipUpdateUserPrompt   },
        };

        bool configValid = true;

        for (auto const& check : requiredStrings)
        {
            if (check.value.empty())
            {
                LOG_ERROR("server.loading", "[PBC] {} is not set. This is a required setting when the module is enabled.", check.key);
                configValid = false;
            }
        }

        if (g_PBC_MaxCtx == 0)
        {
            LOG_ERROR("server.loading", "[PBC] PBC.MaxCtx is not set (or is 0). This is a required setting when the module is enabled.");
            configValid = false;
        }

        if (!configValid)
        {
            LOG_ERROR("server.loading", "[PBC] Required configuration is missing or empty. Module DISABLED — fix your playerbots_characters.conf and reload with .chars reload.");
            g_PBC_Enable = false;
            return;
        }
    }

    LOG_INFO("server.loading",
        "[PBC] Config: Enable={} APIType='{}' Model='{}' Url='{}' MaxCtx={} Timeout={}s "
        "Chances: Whisper={}% Mention={}% Message={}% RollPenalty={}% "
        "Item={}% Duel={}% LevelUp={}% Location={}% BossKill={}% QuestCompleted={}% QuestTaken={}%",
        g_PBC_Enable, g_PBC_APIType, g_PBC_Model, g_PBC_BaseUrl, g_PBC_MaxCtx,
        g_PBC_RequestTimeoutSec,
        g_PBC_ReplyChanceWhisper, g_PBC_ReplyChanceMention,
        g_PBC_ReplyChanceMessage, g_PBC_RollPenaltyOnAnswer,
        g_PBC_ReplyChanceItem,
        g_PBC_ReplyChanceDuel, g_PBC_ReplyChanceLevelUp, g_PBC_ReplyChanceLocation,
        g_PBC_ReplyChanceBossKill, g_PBC_ReplyChanceQuestCompleted, g_PBC_ReplyChanceQuestTaken);
}

// ---------------------------------------------------------------------------
// PBC_LoadCharacterCards
// ---------------------------------------------------------------------------

void PBC_LoadCharacterCards()
{
    g_PBC_CharacterCards.clear();

    std::filesystem::path dir(g_PBC_CharacterCardsPath);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        LOG_WARN("server.loading", "[PBC] Character cards directory not found: {}", g_PBC_CharacterCardsPath);
        return;
    }

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();

        std::string filename = path.filename().string();
        const std::string cardSuffix = ".card.txt";
        if (filename.size() <= cardSuffix.size() ||
            filename.substr(filename.size() - cardSuffix.size()) != cardSuffix)
            continue;

        std::string name = filename.substr(0, filename.size() - cardSuffix.size());

        std::ifstream f(path);
        if (!f) { LOG_WARN("server.loading", "[PBC] Cannot open card file: {}", path.string()); continue; }

        std::stringstream buf;
        buf << f.rdbuf();
        g_PBC_CharacterCards[name] = buf.str();
        ++loaded;

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] Loaded card '{}' ({} chars)", name, g_PBC_CharacterCards[name].size());
    }

    LOG_INFO("server.loading", "[PBC] Loaded {} character card(s) from '{}'", loaded, g_PBC_CharacterCardsPath);
}

// ---------------------------------------------------------------------------
// DB helpers
// ---------------------------------------------------------------------------

void PBC_LoadHistoryFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, message FROM mod_pbc_chat_history ORDER BY id ASC"
    );

    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    g_PBC_ChatHistory.clear();

    if (!result) return;

    do {
        uint64_t    botGuid = (*result)[0].Get<uint64_t>();
        std::string msg     = (*result)[1].Get<std::string>();
        g_PBC_ChatHistory[botGuid].push_back(std::move(msg));
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Chat history loaded from DB.");
}

void PBC_LoadCardAdditionsFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, addition FROM mod_pbc_character_card_additions ORDER BY bot_guid ASC, created_at ASC"
    );

    std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
    g_PBC_CardAdditions.clear();

    if (!result) return;

    do {
        uint64_t    botGuid  = (*result)[0].Get<uint64_t>();
        std::string addition = (*result)[1].Get<std::string>();
        g_PBC_CardAdditions[botGuid].push_back(std::move(addition));
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Character card additions loaded from DB.");
}

void PBC_SaveCardAdditionsToDB()
{
    // Card additions are written individually during condensation.
    // This stub exists for API completeness.
}

void PBC_LoadBotLocationsFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, last_location FROM mod_pbc_bot_location"
    );

    g_PBC_BotLastLocations.clear();

    if (!result) return;

    do {
        uint64_t    botGuid  = (*result)[0].Get<uint64_t>();
        std::string location = (*result)[1].Get<std::string>();
        g_PBC_BotLastLocations[botGuid] = std::move(location);
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Bot locations loaded from DB ({} entries).", g_PBC_BotLastLocations.size());
}

void PBC_LoadRelationshipsFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, target_name, relationship_text, mention_count_at_last_update "
        "FROM mod_pbc_relationships"
    );

    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    g_PBC_Relationships.clear();

    if (!result)
    {
        LOG_INFO("server.loading", "[PBC] Relationships loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t    botGuid    = (*result)[0].Get<uint64_t>();
        std::string targetName = (*result)[1].Get<std::string>();
        std::string relText    = (*result)[2].Get<std::string>();
        uint32_t    mentions   = (*result)[3].Get<uint32_t>();

        auto& entry = g_PBC_Relationships[botGuid][targetName];
        entry.text                    = std::move(relText);
        entry.mentionCountAtLastUpdate = mentions;
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Relationships loaded from DB ({} entries).", count);
}

// ---------------------------------------------------------------------------
// PBC_WorldScript
// ---------------------------------------------------------------------------

PBC_WorldScript::PBC_WorldScript() : WorldScript("PBC_WorldScript") {}

void PBC_WorldScript::OnStartup()
{
    PBC_LoadConfig(true);

    if (!g_PBC_Enable)
    {
        LOG_INFO("server.loading", "[PBC] Module is disabled, skipping initialization.");
        return;
    }

    PBC_LoadCharacterCards();
    PBC_LoadCardAdditionsFromDB();
    PBC_LoadHistoryFromDB();
    PBC_LoadRelationshipsFromDB();
    PBC_LoadBotLocationsFromDB();

    g_PBC_EventThreadDone.store(true);

    LOG_INFO("server.loading", "[PBC] Module started.");
}

void PBC_WorldScript::OnShutdown()
{
    // History is written through to DB on every PBC_AppendHistory call,
    // so no explicit flush is needed on shutdown.
    LOG_INFO("server.loading", "[PBC] Module shutdown.");
}


void PBC_WorldScript::OnUpdate(uint32_t diff)
{
    if (!g_PBC_Enable) return;

    g_PBC_LocationPollAccum += diff;

    static uint32_t s_tickTimer = 0;
    if (s_tickTimer > diff)
    {
        s_tickTimer -= diff;
        return;
    }
    s_tickTimer = 500; // 500 ms gate

    // ---------------------------------------------------------------------------
    // 1. Drain secondary event requests posted by the event thread.
    //    The worker thread cannot do Player* lookups or take snapshots, so it
    //    posts a lightweight PBC_PendingEventRequest and we resolve it here.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingEventRequest> localReqs;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingEventRequestsMutex);
            std::swap(localReqs, g_PBC_PendingEventRequests);
        }

        while (!localReqs.empty())
        {
            PBC_PendingEventRequest& req = localReqs.front();

            // Find the anchor bot to locate the group.
            Player* anchor = nullptr;
            for (auto const& pair : ObjectAccessor::GetPlayers())
            {
                Player* p = pair.second;
                if (!p || !p->IsInWorld()) continue;
                WorldSession* s = p->GetSession();
                if (!s || !s->IsBot()) continue;
                if (p->GetGUID().GetCounter() == req.anchorBotGuid)
                {
                    anchor = p;
                    break;
                }
            }

            if (anchor)
            {
                // Collect all group bots that are not in the excluded set.
                Group* grp = anchor->GetGroup();
                std::vector<Player*> targets;

                auto collectBot = [&](Player* member)
                {
                    if (!member || !member->IsInWorld()) return;
                    WorldSession* ms = member->GetSession();
                    if (!ms || !ms->IsBot()) return;
                    uint64_t guid = member->GetGUID().GetCounter();
                    if (req.excludedBotGuids.count(guid)) return;
                    targets.push_back(member);
                };

                if (grp)
                {
                    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
                        collectBot(ref->GetSource());
                }
                else
                {
                    // Single-bot case (e.g. location events).
                    collectBot(anchor);
                }

                if (!targets.empty())
                {
                    // If the original event had a Narrator / location histLine that
                    // these bots were not part of (e.g. single-bot location events),
                    // write it to their histories now so they have full context.
                    if (!req.originHistLine.empty())
                    {
                        for (Player* bot : targets)
                            PBC_AppendHistory(bot->GetGUID().GetCounter(), req.originHistLine);
                    }

                    PBC_EventItem newEv;
                    newEv.type             = PBC_EventType::Normal;
                    newEv.eventLine        = req.eventLine;
                    newEv.histLine         = req.histLine;
                    newEv.chatType         = req.chatType;
                    newEv.canCreateEvents  = false; // message events never spawn further events
                    // Original responders already have histLine; they only need
                    // to receive any new replies produced by this secondary event.
                    newEv.replyOnlyBotGuids = req.originBotGuids;

                    for (Player* bot : targets)
                    {
                        bool rolled = PBC_RollChance(g_PBC_ReplyChanceMessage);
                        if (g_PBC_DebugEnabled)
                            LOG_INFO("server.loading",
                                     "[PBC] SecondaryEvent: roll bot={} chance={}% -> {}",
                                     bot->GetName(), g_PBC_ReplyChanceMessage,
                                     rolled ? "RESPOND" : "silent");
                        if (rolled)
                        {
                            newEv.respondingBots.push_back(PBC_SnapshotBot(bot));
                        }
                        else
                            newEv.silentBotGuids.push_back(bot->GetGUID().GetCounter());
                    }

                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading",
                                 "[PBC] OnUpdate: secondary event materialised — "
                                 "targets={} responding={} silent={} event=\"{}\"",
                                 targets.size(),
                                 newEv.respondingBots.size(),
                                 newEv.silentBotGuids.size(),
                                 newEv.eventLine);

                    PBC_PushEvent(std::move(newEv));
                }
            }

            localReqs.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 2. Drain completed chat-send actions from the event thread.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingAction> local;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            std::swap(local, g_PBC_PendingActions);
        }
        while (!local.empty())
        {
            PBC_PendingAction& action = local.front();

            if (!action.text.empty())
            {
                Player* bot = ObjectAccessor::FindPlayer(action.botGuid);

                // "thinks..." narrator notification — send to all real players
                // in the bot's group.  If the bot is gone, just skip it.
                if (action.isThinkNotification)
                {
                    if (bot && bot->IsInWorld())
                        PBC_NotifyRealPlayersInGroup(bot, action.text);
                    local.pop();
                    continue;
                }

                if (bot && bot->IsInWorld())
                {
                    uint32_t ct = action.chatType;

                    if (ct == CHAT_MSG_WHISPER && !action.targetGuid.IsEmpty())
                    {
                        Player* target = ObjectAccessor::FindPlayer(action.targetGuid);
                        if (target)
                            bot->Whisper(action.text, LANG_UNIVERSAL, target);
                    }
                    else if (ct == CHAT_MSG_PARTY || ct == CHAT_MSG_PARTY_LEADER)
                    {
                        Group* grp = bot->GetGroup();
                        if (grp)
                        {
                            // Bots are never party leaders — always send as CHAT_MSG_PARTY
                            // regardless of whether the original message was CHAT_MSG_PARTY_LEADER.
                            WorldPacket data;
                            ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, LANG_UNIVERSAL, bot, nullptr, action.text);
                            grp->BroadcastPacket(&data, false, grp->GetMemberGroup(bot->GetGUID()));
                        }
                        else
                        {
                            bot->Say(action.text, LANG_UNIVERSAL);
                        }
                    }
                    else if (ct == CHAT_MSG_RAID || ct == CHAT_MSG_RAID_LEADER || ct == CHAT_MSG_RAID_WARNING)
                    {
                        Group* grp = bot->GetGroup();
                        if (grp)
                        {
                            WorldPacket data;
                            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, LANG_UNIVERSAL, bot, nullptr, action.text);
                            grp->BroadcastPacket(&data, false);
                        }
                        else
                        {
                            bot->Say(action.text, LANG_UNIVERSAL);
                        }
                    }
                    else if (ct == CHAT_MSG_YELL)
                    {
                        bot->Yell(action.text, LANG_UNIVERSAL);
                    }
                    else
                    {
                        bot->Say(action.text, LANG_UNIVERSAL);
                    }

                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading", "[PBC] OnUpdate: sent chat for bot={} type={} text=\"{}\"",
                                 bot->GetName(), ct, action.text);
                }
            }

            local.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 3. Spawn next event thread if the previous one has finished.
    // ---------------------------------------------------------------------------
    if (g_PBC_EventThreadDone.load())
    {
        PBC_EventItem nextEvent;
        bool hasEvent = false;

        {
            std::lock_guard<std::mutex> lock(g_PBC_EventQueueMutex);
            if (!g_PBC_EventQueue.empty())
            {
                nextEvent = std::move(g_PBC_EventQueue.front());
                g_PBC_EventQueue.pop();
                hasEvent = true;
            }
        }

        if (hasEvent)
        {
            g_PBC_EventThreadDone.store(false);

            if (g_PBC_DebugEnabled)
            {
                switch (nextEvent.type)
                {
                    case PBC_EventType::Normal:
                    case PBC_EventType::QuestSummarization:
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type={} event=\"{}\"",
                                 static_cast<int>(nextEvent.type), nextEvent.eventLine);
                        break;
                    case PBC_EventType::Condensation:
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type=Condensation bot=\"{}\"",
                                 nextEvent.condensationBot.botName);
                        break;
                    case PBC_EventType::HistoryReload:
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type=HistoryReload");
                        break;
                    case PBC_EventType::RelationshipUpdate:
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type=RelationshipUpdate bot=\"{}\" target=\"{}\"",
                                 nextEvent.relationshipBot.botName, nextEvent.relationshipTargetName);
                        break;
                }
            }

            std::thread([ev = std::move(nextEvent)]() mutable {
                PBC_ProcessEventItem(std::move(ev));
            }).detach();
        }
    }

    // ---------------------------------------------------------------------------
    // 4. Location polling — runs every 10 seconds.
    // ---------------------------------------------------------------------------
    static constexpr uint32_t kPollIntervalMs = 10000;
    static constexpr int       kStableCycles  = 3;

    if (g_PBC_LocationPollAccum >= kPollIntervalMs)
    {
        g_PBC_LocationPollAccum -= kPollIntervalMs;

        std::unordered_set<uint64_t> activeBotGuids;

        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (!player || !player->IsInWorld()) continue;
            WorldSession* sess = player->GetSession();
            if (!sess || sess->IsBot()) continue;

            Group* grp = player->GetGroup();
            if (!grp) continue;

            for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsInWorld()) continue;
                WorldSession* ms = member->GetSession();
                if (!ms || !ms->IsBot()) continue;

                uint64_t botGuid = member->GetGUID().GetCounter();
                if (!activeBotGuids.insert(botGuid).second) continue;

                // Determine the bot's current location string.
                // For taxi flights we use the destination name as the stable
                // location key so the event fires once per flight (not every
                // poll tick as the ground zone changes mid-air).
                // The event line also includes the current ground zone for
                // extra flavour ("flying to X, passing Y").
                bool inFlight = member->IsInFlight();
                std::string flightDest;   // non-empty only when inFlight
                std::string groundZone;   // current ground zone/area (flight only)
                std::string location;
                if (inFlight)
                {
                    flightDest = PBC_BuildFlightDestination(member);
                    if (flightDest.empty()) continue; // destination unknown — skip
                    groundZone = PBC_BuildPlaceName(member); // may be empty
                    location = "flying to " + flightDest;
                }
                else
                {
                    location = PBC_BuildPlaceName(member);
                    if (location.empty()) continue;
                }

                PBC_LocationState& state = g_PBC_LocationStates[botGuid];

                if (state.lastLocation.empty() && state.firedLocation.empty())
                {
                    auto dbIt = g_PBC_BotLastLocations.find(botGuid);
                    if (dbIt != g_PBC_BotLastLocations.end())
                        state.firedLocation = dbIt->second;
                }

                if (location != state.lastLocation)
                {
                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading",
                                 "[PBC] LocationPoll: bot={} moved to '{}' (was '{}'), resetting cycles",
                                 member->GetName(), location, state.lastLocation);
                    state.lastLocation  = location;
                    state.stableCycles  = 1;
                }
                else
                {
                    state.stableCycles++;

                    if (g_PBC_DebugEnabled && state.stableCycles <= kStableCycles)
                        LOG_INFO("server.loading",
                                 "[PBC] LocationPoll: bot={} stable at '{}' cycles={}/{}",
                                 member->GetName(), location, state.stableCycles, kStableCycles);

                    if (state.stableCycles >= kStableCycles)
                    {
                        if (state.firedLocation == location)
                        {
                            // Already fired for this location — log once when we first hit the threshold.
                            if (g_PBC_DebugEnabled && state.stableCycles == kStableCycles)
                                LOG_INFO("server.loading",
                                         "[PBC] LocationPoll: bot={} stable at '{}' — already fired, skipping",
                                         member->GetName(), location);
                        }
                        else
                        {
                            state.firedLocation = location;
                            g_PBC_BotLastLocations[botGuid] = location;
                            DB_UpsertBotLocation(botGuid, location);

                            if (g_PBC_DebugEnabled)
                                LOG_INFO("server.loading",
                                         "[PBC] LocationPoll: firing location event for bot={} location='{}' chance={}%",
                                         member->GetName(), location, g_PBC_ReplyChanceLocation);

                            std::string eventLine, histLine;
                            if (inFlight)
                            {
                                std::string evText = "You are currently flying to " + flightDest;
                                if (!groundZone.empty())
                                    evText += ", passing " + groundZone;
                                eventLine = PBC_MakeEventLine(evText);
                                histLine  = PBC_MakeHistLine("You started flying to " + flightDest);
                            }
                            else
                            {
                                eventLine = PBC_MakeEventLine("You have just entered " + location);
                                histLine  = PBC_MakeHistLine("You visited " + location);
                            }

                            PBC_DispatchBotEvent(member,
                                eventLine,
                                histLine,
                                g_PBC_ReplyChanceLocation,
                                /*skipHistoryIfSilent=*/true,
                                /*notifyRealPlayers=*/false);
                        }
                    }
                }
            }
        }

        // Prune location state for bots that are no longer tracked.
        for (auto it = g_PBC_LocationStates.begin(); it != g_PBC_LocationStates.end(); )
        {
            if (activeBotGuids.find(it->first) == activeBotGuids.end())
                it = g_PBC_LocationStates.erase(it);
            else
                ++it;
        }
    }
}
