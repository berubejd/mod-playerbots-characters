#ifndef MOD_PBC_CONFIG_H
#define MOD_PBC_CONFIG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include "ObjectGuid.h"
#include "ScriptMgr.h"

// ---------------------------------------------------------------------------
// Module enable / debug
// ---------------------------------------------------------------------------
extern bool     g_PBC_Enable;
extern bool     g_PBC_DebugEnabled;
extern bool     g_PBC_DebugShowFullPrompt;
extern bool     g_PBC_DisplayNarratorEvents;

// ---------------------------------------------------------------------------
// LLM API connection
// ---------------------------------------------------------------------------
extern std::string g_PBC_BaseUrl;          // e.g. https://api.deepseek.com/v1
extern std::string g_PBC_ApiKey;           // Bearer token (empty = no auth header)
extern std::string g_PBC_Model;
extern int         g_PBC_MaxResponseTokens;
extern double      g_PBC_Temperature;
extern double      g_PBC_FrequencyPenalty;
extern double      g_PBC_PresencePenalty;
extern int         g_PBC_RequestTimeoutSec;

// ---------------------------------------------------------------------------
// Context / condensation
// ---------------------------------------------------------------------------
extern uint32_t    g_PBC_MaxCtx;                    // token budget before condensation triggers
extern uint32_t    g_PBC_CondensationPreservedLines; // history lines kept after condensation

// ---------------------------------------------------------------------------
// Prompt templates
// ---------------------------------------------------------------------------
extern std::string g_PBC_SystemPrompt;
extern std::string g_PBC_UserPrompt;
extern std::string g_PBC_CondensationSystemPrompt;
extern std::string g_PBC_CondensationUserPrompt;
extern std::string g_PBC_DefaultCharacterDescription;
extern std::string g_PBC_CharacterContext;

// ---------------------------------------------------------------------------
// Relationship update prompts
// ---------------------------------------------------------------------------
extern std::string g_PBC_RelationshipUpdateSystemPrompt;
extern std::string g_PBC_RelationshipUpdateUserPrompt;

// Number of new "about X" mentions in history that triggers a relationship update for X.
extern uint32_t g_PBC_RelationshipUpdateThreshold;

// ---------------------------------------------------------------------------
// Character card paths
// ---------------------------------------------------------------------------
extern std::string g_PBC_CharacterCardsPath;

// ---------------------------------------------------------------------------
// Reply chances (0-100)
// ---------------------------------------------------------------------------
extern uint32_t g_PBC_ReplyChanceWhisper;
extern uint32_t g_PBC_ReplyChanceMention;
extern uint32_t g_PBC_ReplyChanceQuestion;  // chat (non-whisper) message ending with '?'
extern uint32_t g_PBC_ReplyChanceMessage;
extern uint32_t g_PBC_ReplyChanceItem;
extern uint32_t g_PBC_ReplyChanceDuel;
extern uint32_t g_PBC_ReplyChanceLevelUp;
extern uint32_t g_PBC_ReplyChanceLocation;
extern uint32_t g_PBC_ReplyChanceBossKill;
extern uint32_t g_PBC_ReplyChanceQuestCompletion;

// ---------------------------------------------------------------------------
// Quest completion LLM prompts
// ---------------------------------------------------------------------------
extern std::string g_PBC_QuestCompletionSystemPrompt;
extern std::string g_PBC_QuestCompletionUserPrompt;

// ---------------------------------------------------------------------------
// Blacklist prefixes
// ---------------------------------------------------------------------------
extern std::vector<std::string> g_PBC_Blacklist;

// ---------------------------------------------------------------------------
// Snapshot of a single bot's state, taken on the main thread at the moment
// an event is pushed to the queue.  The event thread works exclusively with
// these snapshots — it never touches live Player* objects.
//
// Includes a mutable local copy of the bot's history so the thread can
// append replies and have subsequent bots see them in the same event.
// ---------------------------------------------------------------------------
struct PBC_BotSnapshot
{
    ObjectGuid  botObjGuid;
    uint64_t    botGuidRaw  = 0;
    std::string botName;

    // --- Pre-rendered prompt fragments (captured on main thread) ---
    std::string characterCard;
    std::string context;

    // Raw template variables for re-rendering per-event user prompts
    std::string charGender;
    std::string charRace;
    std::string charClass;
    std::string charRole;
    std::string charLevel;
    std::string charGold;
    std::string charLocation;
    std::string scene;
    std::string charGroup;
    std::string charLos;
    std::string combatStatus;

    // The bot's history at the moment of snapshotting.
    // The event thread appends its own replies here locally so subsequent
    // bots in the same event see a fully up-to-date history.
    std::deque<std::string> history;

    // For whisper responses
    ObjectGuid  whisperTargetGuid;
    std::string whisperTargetName;

    // Names of all other party members (including real players, excluding this bot).
    // Used for relationship block rendering and mention tracking.
    std::vector<std::string> partyMemberNames;

    // True if at least one group member is a real (non-bot) player.
    // Determines whether the [RELATIONSHIPS] block lists all party members
    // or falls back to the "I don't know much about X" whisper default.
    bool hasRealPlayerInGroup = false;
};

// ---------------------------------------------------------------------------
// Event types
// ---------------------------------------------------------------------------
enum class PBC_EventType : uint8_t
{
    // A normal world/chat event: one or more bots may respond.
    Normal,

    // Quest-completion summarization: the worker first calls the LLM with
    // questSystemPrompt/questUserPrompt to generate a narrative summary,
    // then processes each responding bot against that summary.
    QuestSummarization,

    // Condensation: summarize one bot's history into a card addition and
    // replace history with a short tail.  Used by .chars condense command
    // and by the event thread itself when it detects history overflow.
    Condensation,

    // HistoryReload: reload all bot histories from the database, replacing
    // the in-memory maps.  Pushed by .chars reload so the reload happens
    // after all in-flight events have been fully processed.
    HistoryReload,

    // RelationshipUpdate: ask the LLM to update one bot's relationship
    // description with a specific target character.  Triggered when the
    // number of new "about target" mentions in the bot's history reaches
    // g_PBC_RelationshipUpdateThreshold since the last update.
    RelationshipUpdate,
};

// ---------------------------------------------------------------------------
// A single unit of work for the event queue.
//
// Pushed from the main thread (game hooks, location poll, commands).
// Popped by OnUpdate — one event at a time.  OnUpdate spawns a thread for
// the front item only when g_PBC_EventThreadDone is true.  The thread
// processes all responding bots sequentially (LLM calls, history writes
// directly — all thread-safe), then posts PBC_PendingActions for any chat
// sends that require main-thread Player* access.  When done it sets
// g_PBC_EventThreadDone = true so the next event can start.
// ---------------------------------------------------------------------------
struct PBC_EventItem
{
    PBC_EventType type = PBC_EventType::Normal;

    // --- Normal / QuestSummarization fields ---

    // The event description (present-tense for [CURRENT EVENT]).
    std::string eventLine;

    // Past-tense description appended to history after processing.
    std::string histLine;

    // Chat channel for bot replies (CHAT_MSG_PARTY, CHAT_MSG_WHISPER, etc.)
    uint32_t    chatType = 0;

    // Bots that rolled to respond — processed one at a time in order.
    // Each bot sees all preceding bots' replies in its local history copy.
    std::vector<PBC_BotSnapshot> respondingBots;

    // GUIDs of bots that did NOT roll — receive histLine in history after
    // all responding bots have been processed.
    // When skipHistoryIfSilent=true and respondingBots is empty,
    // silent bots are skipped entirely (avoids noise from frequent low-chance events).
    std::vector<uint64_t> silentBotGuids;
    bool skipHistoryIfSilent = false;

    // GUIDs of bots that already have histLine in their history (they were the
    // original responders that triggered this secondary event) but still need
    // to receive the completedReplyLines from this event.  histLine is NOT
    // re-written to these bots — only the new replies are appended.
    std::vector<uint64_t> replyOnlyBotGuids;

    // When true, each bot that successfully replies triggers a secondary
    // PBC_PendingEventRequest (a message event) for all other bots in the
    // group that have not yet participated.  Must NOT be set on chat-message
    // events to prevent infinite reply loops.
    bool canCreateEvents = false;

    // --- QuestSummarization extra fields ---
    // The worker calls PBC_CallLLM(questSystemPrompt, questUserPrompt) first
    // and uses the result as both eventLine and histLine for the bots.
    std::string questSystemPrompt;
    std::string questUserPrompt;

    // --- Condensation fields ---
    // The bot whose history should be condensed.
    PBC_BotSnapshot condensationBot;
    // Prompts for the condensation LLM call (copied from config at push time
    // so the worker is not sensitive to a concurrent .chars reload).
    std::string condensationSystemPrompt;
    std::string condensationUserPrompt;

    // --- RelationshipUpdate fields ---
    // The bot whose relationship with a target should be updated.
    PBC_BotSnapshot relationshipBot;
    // Name of the target character (e.g. "Jon").
    std::string     relationshipTargetName;
    // Brief info about the target used in {relationship_target} placeholder,
    // e.g. "JON, MALE ORC WARRIOR".
    std::string     relationshipTargetInfo;
    // The bot's current relationship text with the target (may be the default
    // "I don't know much about X" if no data exists yet).
    std::string     relationshipCurrentText;
    // Total mention count in the full history at the moment this event was
    // pushed.  Stored in the DB after a successful update so server restarts
    // don't trigger redundant calls.
    uint32_t        relationshipMentionTotal = 0;
    // Prompts for the relationship update LLM call.
    std::string     relationshipSystemPrompt;
    std::string     relationshipUserPromptTmpl;
};

// ---------------------------------------------------------------------------
// A chat-send action posted from the event thread back to the main thread.
//
// The event thread handles all history/card writes directly (they are
// thread-safe via mutex).  The only thing that requires main-thread access
// is sending actual in-game chat packets via Player* game objects.
// OnUpdate drains these and executes them in order.
// ---------------------------------------------------------------------------
struct PBC_PendingAction
{
    ObjectGuid  botGuid;
    ObjectGuid  targetGuid;   // Non-empty = whisper target
    uint32_t    chatType = 0;
    std::string text;         // LLM reply text to send; empty = no-op
};

extern std::queue<PBC_PendingAction> g_PBC_PendingActions;
extern std::mutex                    g_PBC_PendingActionsMutex;

// ---------------------------------------------------------------------------
// A request from the event thread to the main thread to push a new message
// event.  The main thread (OnUpdate) resolves bot GUIDs to live Player*,
// takes snapshots, rolls chances, and pushes a proper PBC_EventItem.
//
// This avoids doing Player* lookups in the event thread and keeps snapshot
// creation safely on the main thread.
// ---------------------------------------------------------------------------
struct PBC_PendingEventRequest
{
    // The message to use as eventLine/histLine for the new event.
    // eventLine = "<BotName> says: <reply>", histLine = "<BotName>: <reply>"
    std::string eventLine;
    std::string histLine;

    // Optional: the original triggering event's histLine (e.g. a Narrator line)
    // that should be written to all target bots before the secondary event runs.
    // Ensures bots that were not in the original event's silentBotGuids (e.g.
    // single-bot location events) still receive the full conversation context.
    std::string originHistLine;

    // Chat channel to use for bot replies.
    uint32_t chatType = 0;

    // GUID of any current group member — used by OnUpdate to find the group.
    uint64_t anchorBotGuid = 0;

    // GUIDs of bots that already participated and should be excluded.
    std::unordered_set<uint64_t> excludedBotGuids;

    // GUIDs of the original responders that triggered this secondary event.
    // They already have histLine in their history but still need to receive
    // any new replies generated by this secondary event.
    std::vector<uint64_t> originBotGuids;
};

extern std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
extern std::mutex                          g_PBC_PendingEventRequestsMutex;

// ---------------------------------------------------------------------------
// Universal event queue.
//
// Pushed from the main thread (game hooks, location poll, commands).
// Drained by OnUpdate — one event at a time.  OnUpdate spawns a thread for
// the front item only when g_PBC_EventThreadDone is true.
// ---------------------------------------------------------------------------
extern std::queue<PBC_EventItem>  g_PBC_EventQueue;
extern std::mutex                 g_PBC_EventQueueMutex;

// True when no event thread is running (initialised true at startup).
// Written only by the event thread (sets to true when done).
// Read and reset to false by OnUpdate when it spawns the next thread.
extern std::atomic<bool> g_PBC_EventThreadDone;

// ---------------------------------------------------------------------------
// In-memory chat history: bot_guid -> ordered list of pre-formatted lines
// ---------------------------------------------------------------------------
extern std::unordered_map<uint64_t, std::deque<std::string>> g_PBC_ChatHistory;
extern std::mutex g_PBC_HistoryMutex;

// ---------------------------------------------------------------------------
// In-memory character card additions: bot_guid -> list of condensed texts
// ---------------------------------------------------------------------------
extern std::unordered_map<uint64_t, std::vector<std::string>> g_PBC_CardAdditions;
extern std::mutex g_PBC_CardMutex;

// ---------------------------------------------------------------------------
// One relationship entry: the LLM-generated text and the total mention count
// in the bot's history at the time the relationship was last updated.
// ---------------------------------------------------------------------------
struct PBC_RelationshipEntry
{
    std::string text;
    uint32_t    mentionCountAtLastUpdate = 0;
};

// ---------------------------------------------------------------------------
// In-memory relationships: bot_guid -> (target_name -> entry)
// Protected by g_PBC_RelationshipsMutex.
// ---------------------------------------------------------------------------
extern std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
extern std::mutex g_PBC_RelationshipsMutex;

// ---------------------------------------------------------------------------
// Character cards loaded from disk: bot name -> card text
// ---------------------------------------------------------------------------
extern std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

// ---------------------------------------------------------------------------
// Per-bot location polling state.
// ---------------------------------------------------------------------------
struct PBC_LocationState
{
    std::string lastLocation;
    int         stableCycles  = 0;
    std::string firedLocation;
};

extern std::unordered_map<uint64_t, PBC_LocationState> g_PBC_LocationStates;
extern std::unordered_map<uint64_t, std::string>        g_PBC_BotLastLocations;
extern uint32_t g_PBC_LocationPollAccum;

// ---------------------------------------------------------------------------
// Push a fully-constructed event item onto the global queue.
// Thread-safe; may be called from any thread (in practice: main thread only).
// ---------------------------------------------------------------------------
void PBC_PushEvent(PBC_EventItem item);

// ---------------------------------------------------------------------------
// Loader / WorldScript
// ---------------------------------------------------------------------------
void PBC_LoadConfig();
void PBC_LoadCharacterCards();
void PBC_LoadHistoryFromDB();
void PBC_LoadCardAdditionsFromDB();
void PBC_SaveCardAdditionsToDB();
void PBC_LoadBotLocationsFromDB();
void PBC_LoadRelationshipsFromDB();

class PBC_WorldScript : public WorldScript
{
public:
    PBC_WorldScript();
    void OnStartup() override;
    void OnShutdown() override;
    void OnUpdate(uint32_t diff) override;
};

#endif // MOD_PBC_CONFIG_H
