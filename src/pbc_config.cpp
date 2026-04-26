#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_events.h"
#include "pbc_http.h"
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

std::string g_PBC_PromptsPath = "../../../modules/mod-playerbots-characters/prompts";
std::string g_PBC_CharacterCardsPath = "../../../modules/mod-playerbots-characters/characters";

uint32_t g_PBC_ReplyChanceWhisper   = 100;
uint32_t g_PBC_ReplyChanceMention   = 100;
uint32_t g_PBC_ReplyChanceMessage   = 100;
uint32_t g_PBC_RollPenaltyOnAnswer  = 45;
uint32_t g_PBC_ReplyChanceItem     = 5;
uint32_t g_PBC_ReplyChanceDuel     = 5;
uint32_t g_PBC_ReplyChanceLevelUp  = 5;
uint32_t g_PBC_ReplyChanceBossKill       = 35;
uint32_t g_PBC_ReplyChanceQuestCompleted = 20;
uint32_t g_PBC_ReplyChanceQuestTaken     = 10;

std::string g_PBC_QuestCompletedSystemPrompt;
std::string g_PBC_QuestCompletedUserPrompt;
std::string g_PBC_QuestTakenSystemPrompt;
std::string g_PBC_QuestTakenUserPrompt;

std::vector<std::string> g_PBC_Blacklist;

int         g_PBC_HttpServerPort            = 0;
std::string g_PBC_HttpServerBind            = "127.0.0.1";
int         g_PBC_HttpServerTimeout         = 15;
std::string g_PBC_HttpServerBaseUrl         = "http://127.0.0.1:8501";
std::string g_PBC_HttpServerPrivateKey;
std::string g_PBC_HttpServerFrontendPath    = "../../../modules/mod-playerbots-characters/frontend/dist";

std::queue<PBC_PendingAction> g_PBC_PendingActions;
std::mutex                    g_PBC_PendingActionsMutex;

std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
std::mutex                          g_PBC_PendingEventRequestsMutex;

std::queue<PBC_PendingWhisperRequest> g_PBC_PendingWhisperRequests;
std::mutex                            g_PBC_PendingWhisperRequestsMutex;

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

std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;

std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

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
static void PBC_CondenseInline(PBC_CharacterSnapshot& snap,
                                const std::string& sysPrompt,
                                const std::string& userPromptTmpl)
{
    if (sysPrompt.empty() || userPromptTmpl.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] CondenseInline: prompts not configured, skipping for character={}", snap.charName);
        return;
    }

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] CondenseInline: character={} history_lines={}", snap.charName, snap.history.size());

    std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(snap, userPromptTmpl);
    PBC_LLMResult res = PBC_CallLLM(sysPrompt, userPrompt, /*maxTokensOverride=*/-1);

    if (!res.success || res.text.empty())
    {
        LOG_WARN("server.loading", "[PBC] CondenseInline: LLM failed for character={}", snap.charName);
        return;
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

        PBC_CondenseInline(ev.condensationChar, condenseSysPrompt, condenseUsrTmpl);

        // Condensation is always a trigger for relationship updates for all party
        // members — no threshold check needed here.  The history is about to be
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

        PBC_LLMResult res = PBC_CallLLM(ev.relationshipSystemPrompt, userPrompt, /*maxTokensOverride=*/-1);

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
        int histTokens = PBC_EstimateHistoryTokens(snap.charGuidRaw);
        if (histTokens > static_cast<int>(g_PBC_MaxCtx))
        {
            PBC_CondenseInline(snap, condenseSysPrompt, condenseUsrTmpl);
            // snap.history is now updated to the condensed tail by CondenseInline.
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
        // separate in-game message.  For example, the LLM reply
        //   *looks around* Hello! *waves* Goodbye!
        // is posted as four messages:
        //   *looks around* (narrator system message)
        //   Hello!         (regular chat)
        //   *waves*        (narrator system message)
        //   Goodbye!       (regular chat)
        //
        // The full reply (including narrator blocks) is always stored in the
        // character's history as-is.  When PBC.DisplayNarratorEvents is
        // disabled the full reply is sent as a single normal chat message.
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

    g_PBC_PromptsPath = sConfigMgr->GetOption<std::string>("PBC.PromptsPath",
                                    "../../../modules/mod-playerbots-characters/prompts");
    g_PBC_CharacterCardsPath  = sConfigMgr->GetOption<std::string>("PBC.CharacterCardsPath",
                                    "../../../modules/mod-playerbots-characters/characters");

    g_PBC_ReplyChanceWhisper   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceWhisper", 100);
    g_PBC_ReplyChanceMention   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceMention", 100);
    g_PBC_ReplyChanceMessage   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceMessage", 100);
    g_PBC_RollPenaltyOnAnswer  = sConfigMgr->GetOption<uint32_t>("PBC.RollPenaltyOnAnswer", 45);
    g_PBC_ReplyChanceItem     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceItem", 5);
    g_PBC_ReplyChanceDuel     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceDuel", 5);
    g_PBC_ReplyChanceLevelUp  = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLevelUp", 5);
    g_PBC_ReplyChanceBossKill       = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceBossKill", 35);
    g_PBC_ReplyChanceQuestCompleted = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestCompleted", 20);
    g_PBC_ReplyChanceQuestTaken     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestTaken", 10);

    g_PBC_RelationshipUpdateThreshold    = sConfigMgr->GetOption<uint32_t>("PBC.RelationshipUpdateThreshold", 100);

    std::string blacklistStr = sConfigMgr->GetOption<std::string>("PBC.Blacklist", "");
    g_PBC_Blacklist = SplitByComma(blacklistStr);

    g_PBC_HttpServerPort         = sConfigMgr->GetOption<int>("PBC.HttpServerPort", 0);
    g_PBC_HttpServerBind         = sConfigMgr->GetOption<std::string>("PBC.HttpServerBind", "127.0.0.1");
    g_PBC_HttpServerTimeout      = sConfigMgr->GetOption<int>("PBC.HttpServerTimeout", 15);
    g_PBC_HttpServerBaseUrl      = sConfigMgr->GetOption<std::string>("PBC.HttpServerBaseUrl", "http://127.0.0.1:8501");
    g_PBC_HttpServerPrivateKey   = sConfigMgr->GetOption<std::string>("PBC.HttpServerPrivateKey", "");
    g_PBC_HttpServerFrontendPath = sConfigMgr->GetOption<std::string>("PBC.HttpServerFrontendPath",
                                                                       "../../../modules/mod-playerbots-characters/frontend/dist");

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

        // HTTP server private key is required when the HTTP server is enabled
        if (g_PBC_HttpServerPort > 0 && g_PBC_HttpServerPrivateKey.empty())
        {
            LOG_ERROR("server.loading", "[PBC] PBC.HttpServerPrivateKey is not set but PBC.HttpServerPort is {}. "
                      "The private key is required for the authorization layer when the HTTP server is enabled. "
                      "HTTP server will NOT start.", g_PBC_HttpServerPort);
        }
    }

    // Load prompts from files (required for the module to work)
    if (g_PBC_Enable && !PBC_LoadPrompts())
    {
        LOG_ERROR("server.loading", "[PBC] Failed to load prompts. Module DISABLED — fix prompt path and reload with .chars reload.");
        g_PBC_Enable = false;
        return;
    }

    LOG_INFO("server.loading",
        "[PBC] Config: Enable={} APIType='{}' Model='{}' Url='{}' MaxCtx={} Timeout={}s "
        "Chances: Whisper={}% Mention={}% Message={}% RollPenalty={}% "
        "Item={}% Duel={}% LevelUp={}% BossKill={}% QuestCompleted={}% QuestTaken={}%",
        g_PBC_Enable, g_PBC_APIType, g_PBC_Model, g_PBC_BaseUrl, g_PBC_MaxCtx,
        g_PBC_RequestTimeoutSec,
        g_PBC_ReplyChanceWhisper, g_PBC_ReplyChanceMention,
        g_PBC_ReplyChanceMessage, g_PBC_RollPenaltyOnAnswer,
        g_PBC_ReplyChanceItem,
        g_PBC_ReplyChanceDuel, g_PBC_ReplyChanceLevelUp,
        g_PBC_ReplyChanceBossKill, g_PBC_ReplyChanceQuestCompleted, g_PBC_ReplyChanceQuestTaken);

    LOG_INFO("server.loading",
        "[PBC] HTTP Server: Port={} Bind='{}' Timeout={}s BaseUrl='{}' PrivateKey={} FrontendPath='{}'",
        g_PBC_HttpServerPort, g_PBC_HttpServerBind, g_PBC_HttpServerTimeout, g_PBC_HttpServerBaseUrl,
        g_PBC_HttpServerPrivateKey.empty() ? "(not set)" : "(set)", g_PBC_HttpServerFrontendPath);
}

// ---------------------------------------------------------------------------
// PBC_LoadPrompts
//
// Loads all prompt templates from the directory specified by PBC.PromptsPath.
// For each prompt, tries the .custom.txt version first; if not found, falls
// back to the .default.txt version.  Returns false if any prompt fails to load,
// which should disable the module.
// ---------------------------------------------------------------------------

// Helper: load a single prompt file.  Tries customPath first, then defaultPath.
// Returns true on success, false on failure.  Sets usedCustom if the custom
// file was loaded.
static bool LoadPromptFile(const std::string& customPath,
                           const std::string& defaultPath,
                           std::string& target,
                           bool& usedCustom)
{
    usedCustom = false;

    // Try custom first
    std::ifstream fCustom(customPath);
    if (fCustom)
    {
        std::stringstream buf;
        buf << fCustom.rdbuf();
        if (buf.str().empty())
        {
            LOG_WARN("server.loading", "[PBC] Custom prompt file is empty: {}", customPath);
        }
        else
        {
            target = buf.str();
            usedCustom = true;
            return true;
        }
    }

    // Fall back to default
    std::ifstream fDefault(defaultPath);
    if (!fDefault)
    {
        LOG_ERROR("server.loading", "[PBC] Cannot open prompt file: {}",
                  defaultPath);
        return false;
    }

    std::stringstream buf;
    buf << fDefault.rdbuf();
    if (buf.str().empty())
    {
        LOG_ERROR("server.loading", "[PBC] Default prompt file is empty: {}", defaultPath);
        return false;
    }

    target = buf.str();
    return true;
}

bool PBC_LoadPrompts()
{
    std::filesystem::path dir(g_PBC_PromptsPath);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        LOG_ERROR("server.loading", "[PBC] Prompts directory not found: {}", g_PBC_PromptsPath);
        return false;
    }

    // Each prompt: { filename (without extension), reference to global variable }
    struct PromptEntry { const char* filename; std::string& target; };
    const PromptEntry prompts[] = {
        { "Main.system",                      g_PBC_SystemPrompt                   },
        { "Main.user",                        g_PBC_UserPrompt                     },
        { "Condensation.system",              g_PBC_CondensationSystemPrompt       },
        { "Condensation.user",                g_PBC_CondensationUserPrompt         },
        { "DefaultCharacterDescription",      g_PBC_DefaultCharacterDescription    },
        { "CharacterContext",                  g_PBC_CharacterContext               },
        { "QuestCompleted.system",            g_PBC_QuestCompletedSystemPrompt     },
        { "QuestCompleted.user",              g_PBC_QuestCompletedUserPrompt       },
        { "QuestTaken.system",                g_PBC_QuestTakenSystemPrompt         },
        { "QuestTaken.user",                  g_PBC_QuestTakenUserPrompt           },
        { "RelationshipUpdate.system",        g_PBC_RelationshipUpdateSystemPrompt },
        { "RelationshipUpdate.user",          g_PBC_RelationshipUpdateUserPrompt   },
    };

    bool allOk = true;
    int customCount = 0;

    for (auto const& entry : prompts)
    {
        std::string customPath  = (dir / (std::string(entry.filename) + ".custom.txt")).string();
        std::string defaultPath = (dir / (std::string(entry.filename) + ".default.txt")).string();

        bool usedCustom = false;
        if (!LoadPromptFile(customPath, defaultPath, entry.target, usedCustom))
        {
            allOk = false;
        }
        else if (usedCustom)
        {
            ++customCount;
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Loaded custom prompt '{}' ({} chars)", entry.filename, entry.target.size());
        }
    }

    if (!allOk)
        return false;

    LOG_INFO("server.loading", "[PBC] Loaded {} prompt(s) from '{}' ({} custom)",
             static_cast<int>(sizeof(prompts) / sizeof(prompts[0])), g_PBC_PromptsPath, customCount);
    return true;
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
        "SELECT bot_guid, message, UNIX_TIMESTAMP(timestamp) FROM mod_pbc_chat_history ORDER BY id ASC"
    );

    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    g_PBC_ChatHistory.clear();
    g_PBC_LastHistoryTime.clear();

    if (!result) return;

    do {
        uint64_t    botGuid = (*result)[0].Get<uint64_t>();
        std::string msg     = (*result)[1].Get<std::string>();
        time_t      ts      = static_cast<time_t>((*result)[2].Get<uint64_t>());
        g_PBC_ChatHistory[botGuid].push_back(std::move(msg));
        if (ts > 0)
            g_PBC_LastHistoryTime[botGuid] = ts;
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Chat history loaded from DB ({} characters with timestamps).",
             g_PBC_LastHistoryTime.size());
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

void PBC_LoadCharacterDataFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, roll_chance_modifier FROM mod_pbc_data"
    );

    g_PBC_RollChanceModifiers.clear();

    if (!result)
    {
        LOG_INFO("server.loading", "[PBC] Characters data loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t botGuid = (*result)[0].Get<uint64_t>();
        int32_t  rollMod = (*result)[1].Get<int32_t>();
        if (rollMod != 0)
            g_PBC_RollChanceModifiers[botGuid] = rollMod;
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Characters data loaded from DB ({} entries, {} with roll modifier).",
             count, g_PBC_RollChanceModifiers.size());
}

// ---------------------------------------------------------------------------
// PBC_GetEffectiveChance
// ---------------------------------------------------------------------------

uint32_t PBC_GetEffectiveChance(uint64_t botGuid, uint32_t baseChance)
{
    auto it = g_PBC_RollChanceModifiers.find(botGuid);
    if (it == g_PBC_RollChanceModifiers.end())
        return baseChance;
    int32_t effective = static_cast<int32_t>(baseChance) + it->second;
    return static_cast<uint32_t>(std::max(0, std::min(100, effective)));
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
    PBC_LoadCharacterDataFromDB();

    g_PBC_EventThreadDone.store(true);

    // Start the HTTP/WS server if a port is configured.
    if (g_PBC_HttpServerPort > 0)
    {
        if (!PBC_HttpServerStart(g_PBC_HttpServerBind, g_PBC_HttpServerPort, g_PBC_HttpServerTimeout))
        {
            LOG_ERROR("server.loading",
                      "[PBC] HTTP server could not be started on {}:{} — treating as disabled. "
                      "The rest of the module continues normally.",
                      g_PBC_HttpServerBind, g_PBC_HttpServerPort);
            g_PBC_HttpServerPort = 0; // treat as disabled
        }
    }
    else
    {
        LOG_INFO("server.loading", "[PBC] HTTP server disabled (PBC.HttpServerPort = 0).");
    }

    LOG_INFO("server.loading", "[PBC] Module started.");
}

void PBC_WorldScript::OnShutdown()
{
    // Stop the HTTP/WS server if it is running.
    if (PBC_HttpServerIsRunning())
    {
        LOG_INFO("server.loading", "[PBC] Stopping HTTP server...");
        PBC_HttpServerStop();
    }

    // History is written through to DB on every PBC_AppendHistory call,
    // so no explicit flush is needed on shutdown.
    LOG_INFO("server.loading", "[PBC] Module shutdown.");
}


void PBC_WorldScript::OnUpdate(uint32_t diff)
{
    if (!g_PBC_Enable) return;

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
                if (p->GetGUID().GetCounter() == req.anchorCharGuid)
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
                    if (req.excludedCharGuids.count(guid)) return;
                    targets.push_back(member);
                };

                if (grp)
                {
                    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
                        collectBot(ref->GetSource());
                }
                else
                {
                    // Single-bot case (ungrouped bot).
                    collectBot(anchor);
                }

                if (!targets.empty())
                {
                    // If the original event had a Narrator histLine that these bots
                    // were not part of, write it to their histories now so they
                    // have full context.
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
                    newEv.replyOnlyCharGuids = req.originCharGuids;

                    // Shuffle targets so the penalty doesn't always favour the
                    // same character — same approach as the primary chat handler.
                    std::shuffle(targets.begin(), targets.end(), PBC_GetRNG());

                    // Apply the same penalty logic as the primary chat handler
                    // (HandleChatMessage), but start with the penalty already applied
                    // once — the original responder already "used" a successful roll.
                    uint32 currentChance = g_PBC_ReplyChanceMessage > g_PBC_RollPenaltyOnAnswer
                        ? g_PBC_ReplyChanceMessage - g_PBC_RollPenaltyOnAnswer : 0;

                    for (Player* bot : targets)
                    {
                        if (currentChance == 0)
                        {
                            if (g_PBC_DebugEnabled)
                                LOG_INFO("server.loading",
                                         "[PBC] SecondaryEvent: roll character={} chance=0% -> silent (no chance left)",
                                         bot->GetName());
                            newEv.silentCharGuids.push_back(bot->GetGUID().GetCounter());
                            continue;
                        }

                        uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), currentChance);
                        bool rolled = PBC_RollChance(effectiveChance);
                        if (g_PBC_DebugEnabled)
                            LOG_INFO("server.loading",
                                     "[PBC] SecondaryEvent: roll character={} chance={}% (base={}% mod={}) -> {}",
                                     bot->GetName(), effectiveChance, currentChance,
                                     static_cast<int32_t>(effectiveChance) - static_cast<int32_t>(currentChance),
                                     rolled ? "RESPOND" : "silent");
                        if (rolled)
                        {
                            newEv.respondingChars.push_back(PBC_SnapshotCharacter(bot));
                            currentChance = currentChance > g_PBC_RollPenaltyOnAnswer
                                ? currentChance - g_PBC_RollPenaltyOnAnswer : 0;
                        }
                        else
                            newEv.silentCharGuids.push_back(bot->GetGUID().GetCounter());
                    }

                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading",
                                 "[PBC] OnUpdate: secondary event materialised — "
                                 "targets={} responding={} silent={} event=\"{}\"",
                                 targets.size(),
                                 newEv.respondingChars.size(),
                                 newEv.silentCharGuids.size(),
                                 newEv.eventLine);

                    PBC_PushEvent(std::move(newEv));
                }
            }

            localReqs.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 1b. Drain whisper requests posted from the HTTP API thread.
    //
    //     These are processed identically to in-game whisper events: we find
    //     the target bot, take a snapshot with whisper target info, roll
    //     chance, and push a PBC_EventItem.  The event thread then handles
    //     the LLM call, history writes, and the in-game whisper reply.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingWhisperRequest> localWhispers;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingWhisperRequestsMutex);
            std::swap(localWhispers, g_PBC_PendingWhisperRequests);
        }

        while (!localWhispers.empty())
        {
            PBC_PendingWhisperRequest& wr = localWhispers.front();

            // Find the sender (real player) — must be online, otherwise skip
            Player* sender = ObjectAccessor::FindPlayer(ObjectGuid(wr.senderGuid));
            if (!sender || !sender->IsInWorld())
            {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] API whisper: sender GUID={} is not online, skipping", wr.senderGuid);
                localWhispers.pop();
                continue;
            }

            // Find the target bot
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(wr.targetGuid));
            if (!target || !target->IsInWorld())
            {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] API whisper: target bot GUID={} is not online, skipping", wr.targetGuid);
                localWhispers.pop();
                continue;
            }

            WorldSession* ts = target->GetSession();
            if (!ts || !ts->IsBot())
            {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] API whisper: target GUID={} is not a character, skipping", wr.targetGuid);
                localWhispers.pop();
                continue;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API whisper: player GUID={} -> character {} (chance={}%)",
                         wr.senderGuid, target->GetName(), g_PBC_ReplyChanceWhisper);

            PBC_EventItem ev;
            ev.type      = PBC_EventType::Normal;
            ev.eventLine = wr.eventLine;
            ev.histLine  = wr.histLine;
            ev.chatType  = CHAT_MSG_WHISPER;

            if (PBC_RollChance(PBC_GetEffectiveChance(target->GetGUID().GetCounter(), g_PBC_ReplyChanceWhisper)))
            {
                PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(target);
                snap.whisperTargetGuid = ObjectGuid(wr.senderGuid);
                snap.whisperTargetName = sender->GetName();
                ev.respondingChars.push_back(std::move(snap));
            }
            else
            {
                ev.silentCharGuids.push_back(target->GetGUID().GetCounter());
            }

            PBC_PushEvent(std::move(ev));
            localWhispers.pop();
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
                Player* bot = ObjectAccessor::FindPlayer(action.charGuid);

                // Narrator system message (e.g. "thinks..." notification or a
                // leading *text* block from the LLM reply) — send to all real
                // players in the bot's group.  If the bot is gone, skip it.
                if (action.isNarratorMessage)
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
                        LOG_INFO("server.loading", "[PBC] OnUpdate: sent chat for character={} type={}",
                                 bot->GetName(), ct);
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
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type=Condensation character=\"{}\"",
                                 nextEvent.condensationChar.charName);
                        break;
                    case PBC_EventType::HistoryReload:
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type=HistoryReload");
                        break;
                    case PBC_EventType::RelationshipUpdate:
                        LOG_INFO("server.loading", "[PBC] OnUpdate: spawning event thread for type=RelationshipUpdate character=\"{}\" target=\"{}\"",
                                 nextEvent.relationshipChar.charName, nextEvent.relationshipTargetName);
                        break;
                }
            }

            std::thread([ev = std::move(nextEvent)]() mutable {
                PBC_ProcessEventItem(std::move(ev));
            }).detach();
        }
    }

}
