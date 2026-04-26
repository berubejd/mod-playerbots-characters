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
extern bool     g_PBC_DebugShowFullRequest;
extern bool     g_PBC_DisplayNarratorEvents;

// ---------------------------------------------------------------------------
// LLM API connection
// ---------------------------------------------------------------------------
extern std::string g_PBC_APIType;          // "openai" or "anthropic"
extern std::string g_PBC_BaseUrl;          // e.g. https://api.deepseek.com/v1
extern std::string g_PBC_ApiKey;           // Bearer token / x-api-key (empty = no auth header)
extern std::string g_PBC_Model;
extern int         g_PBC_MaxResponseTokens;
extern double      g_PBC_Temperature;
extern std::string g_PBC_ModelExtraParameters; // raw JSON merged into every request body
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
extern uint32_t g_PBC_ReplyChanceMessage;
extern uint32_t g_PBC_RollPenaltyOnAnswer;
extern uint32_t g_PBC_ReplyChanceItem;
extern uint32_t g_PBC_ReplyChanceDuel;
extern uint32_t g_PBC_ReplyChanceLevelUp;
extern uint32_t g_PBC_ReplyChanceBossKill;
extern uint32_t g_PBC_ReplyChanceQuestCompleted;
extern uint32_t g_PBC_ReplyChanceQuestTaken;

// ---------------------------------------------------------------------------
// Quest LLM prompts
// ---------------------------------------------------------------------------
extern std::string g_PBC_QuestCompletedSystemPrompt;
extern std::string g_PBC_QuestCompletedUserPrompt;
extern std::string g_PBC_QuestTakenSystemPrompt;
extern std::string g_PBC_QuestTakenUserPrompt;

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
extern int         g_PBC_HttpServerPort;            // 0 = disabled
extern std::string g_PBC_HttpServerBind;            // bind address
extern int         g_PBC_HttpServerTimeout;         // request timeout in seconds
extern std::string g_PBC_HttpServerBaseUrl;         // base URL for external access
extern std::string g_PBC_HttpServerPrivateKey;      // secret key for token encryption (required when port != 0)
extern std::string g_PBC_HttpServerFrontendPath;    // path to frontend static files (relative to server CWD)

// ---------------------------------------------------------------------------
// Blacklist prefixes
// ---------------------------------------------------------------------------
extern std::vector<std::string> g_PBC_Blacklist;

// ---------------------------------------------------------------------------
// Per-character roll chance modifiers (bot_guid -> modifier, range -100..100)
// A positive modifier makes a character more talkative; negative less talkative.
// Applied to every roll chance for the character.
// ---------------------------------------------------------------------------
extern std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;

// ---------------------------------------------------------------------------
// Snapshot of a single character's state, taken on the main thread at the moment
// an event is pushed to the queue.  The event thread works exclusively with
// these snapshots — it never touches live Player* objects.
//
// Includes a mutable local copy of the character's history so the thread can
// append replies and have subsequent characters see them in the same event.
// ---------------------------------------------------------------------------
struct PBC_CharacterSnapshot
{
    ObjectGuid  charObjGuid;
    uint64_t    charGuidRaw = 0;
    std::string charName;

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
    std::string scene;
    std::string charGroup;
    std::string charLos;
    std::string combatStatus;
    std::string equipment;

    // The character's history at the moment of snapshotting.
    // The event thread appends its own replies here locally so subsequent
    // characters in the same event see a fully up-to-date history.
    std::deque<std::string> history;

    // For whisper responses
    ObjectGuid  whisperTargetGuid;
    std::string whisperTargetName;

    // Names of all other party members (including real players, excluding this character).
    // Used for relationship block rendering and mention tracking.
    std::vector<std::string> partyMemberNames;

    // True if at least one group member is a real (non-bot) player.
    // (The term "bot" here refers to the mod-playerbots entity, not the character layer.)
    // Determines whether the [RELATIONSHIPS] block lists all party members
    // or falls back to the "You don't know much about X" whisper default.
    bool hasRealPlayerInGroup = false;
};

// ---------------------------------------------------------------------------
// Event types
// ---------------------------------------------------------------------------
enum class PBC_EventType : uint8_t
{
    // A normal world/chat event: one or more characters may respond.
    Normal,

    // Quest-completion summarization: the worker first calls the LLM with
    // questSystemPrompt/questUserPrompt to generate a narrative summary,
    // then processes each responding character against that summary.
    QuestSummarization,

    // Condensation: summarize one character's history into a card addition and
    // replace history with a short tail.  Used by .chars condense command
    // and by the event thread itself when it detects history overflow.
    Condensation,

    // HistoryReload: reload all character histories from the database, replacing
    // the in-memory maps.  Pushed by .chars reload so the reload happens
    // after all in-flight events have been fully processed.
    HistoryReload,

    // RelationshipUpdate: ask the LLM to update one character's relationship
    // description with a specific target character.  Triggered when the
    // number of new "about target" mentions in the character's history reaches
    // g_PBC_RelationshipUpdateThreshold since the last update.
    RelationshipUpdate,
};

// ---------------------------------------------------------------------------
// A single unit of work for the event queue.
//
// Pushed from the main thread (game hooks, location poll, commands).
// Popped by OnUpdate — one event at a time.  OnUpdate spawns a thread for
// the front item only when g_PBC_EventThreadDone is true.  The thread
// processes all responding characters sequentially (LLM calls, history writes
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

    // Characters that rolled to respond — processed one at a time in order.
    // Each character sees all preceding characters' replies in its local history copy.
    std::vector<PBC_CharacterSnapshot> respondingChars;

    // GUIDs of characters that did NOT roll — receive histLine in history after
    // all responding characters have been processed.
    std::vector<uint64_t> silentCharGuids;

    // GUIDs of characters that already have histLine in their history (they were the
    // original responders that triggered this secondary event) but still need
    // to receive the completedReplyLines from this event.  histLine is NOT
    // re-written to these characters — only the new replies are appended.
    std::vector<uint64_t> replyOnlyCharGuids;

    // When true, each character that successfully replies triggers a secondary
    // PBC_PendingEventRequest (a message event) for all other characters in the
    // group that have not yet participated.  Must NOT be set on chat-message
    // events to prevent infinite reply loops.
    bool canCreateEvents = false;

    // --- QuestSummarization extra fields ---
    // The worker calls PBC_CallLLM(questSystemPrompt, questUserPrompt) first
    // and uses the result as both eventLine and histLine for the bots.
    std::string questSystemPrompt;
    std::string questUserPrompt;

    // ObjectGuid of the player who triggered the quest event (party leader).
    // Used to send the narrator summary message to the correct group after
    // the LLM generates the summary text.
    ObjectGuid  anchorObjGuid;

    // --- Condensation fields ---
    // The character whose history should be condensed.
    PBC_CharacterSnapshot condensationChar;
    // Prompts for the condensation LLM call (copied from config at push time
    // so the worker is not sensitive to a concurrent .chars reload).
    std::string condensationSystemPrompt;
    std::string condensationUserPrompt;

    // --- RelationshipUpdate fields ---
    // The character whose relationship with a target should be updated.
    PBC_CharacterSnapshot relationshipChar;
    // Name of the target character (e.g. "Jon").
    std::string     relationshipTargetName;
    // Brief info about the target used in {relationship_target} placeholder,
    // e.g. "JON, MALE ORC WARRIOR".
    std::string     relationshipTargetInfo;
    // The character's current relationship text with the target (may be the default
    // "You don't know much about X" if no data exists yet).
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
    ObjectGuid  charGuid;
    ObjectGuid  targetGuid;   // Non-empty = whisper target
    uint32_t    chatType = 0;
    std::string text;         // LLM reply text to send; empty = no-op
    bool        isNarratorMessage = false; // true = send as narrator system message instead of chat
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
    // that should be written to all target characters before the secondary event runs.
    // Ensures characters that were not in the original event's silentCharGuids
    // still receive the full conversation context.
    std::string originHistLine;

    // Chat channel to use for character replies.
    uint32_t chatType = 0;

    // GUID of any current group member — used by OnUpdate to find the group.
    uint64_t anchorCharGuid = 0;

    // GUIDs of characters that already participated and should be excluded.
    std::unordered_set<uint64_t> excludedCharGuids;

    // GUIDs of the original responders that triggered this secondary event.
    // They already have histLine in their history but still need to receive
    // any new replies generated by this secondary event.
    std::vector<uint64_t> originCharGuids;
};

extern std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
extern std::mutex                          g_PBC_PendingEventRequestsMutex;

// ---------------------------------------------------------------------------
// A whisper request posted from the HTTP API thread to the main thread.
//
// The main thread (OnUpdate) resolves the target bot GUID to a live Player*,
// takes a snapshot with whisper target info, rolls chance, and pushes a
// proper PBC_EventItem — identical to how an in-game whisper is processed.
// ---------------------------------------------------------------------------
struct PBC_PendingWhisperRequest
{
    std::string eventLine;   // e.g. "PlayerName tells you privately: hello"
    std::string histLine;    // e.g. "PlayerName (privately to you): hello"
    uint64_t    senderGuid = 0;  // GUID of the whispering player
    uint64_t    targetGuid = 0;  // GUID of the target character (bot)
};

extern std::queue<PBC_PendingWhisperRequest> g_PBC_PendingWhisperRequests;
extern std::mutex                            g_PBC_PendingWhisperRequestsMutex;

// ---------------------------------------------------------------------------
// Universal event queue.
//
// Pushed from the main thread (game hooks, commands).
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
// In-memory last message timestamp per character: bot_guid -> time_t
// Used to detect time gaps and insert "some time passes" narrator lines.
// Protected by g_PBC_HistoryMutex.
// ---------------------------------------------------------------------------
extern std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

// ---------------------------------------------------------------------------
// In-memory character card additions: bot_guid -> list of condensed texts
// ---------------------------------------------------------------------------
extern std::unordered_map<uint64_t, std::vector<std::string>> g_PBC_CardAdditions;
extern std::mutex g_PBC_CardMutex;

// ---------------------------------------------------------------------------
// One relationship entry: the LLM-generated text and the total mention count
// in the character's history at the time the relationship was last updated.
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
// Character cards loaded from disk: character name -> card text
// ---------------------------------------------------------------------------
extern std::unordered_map<std::string, std::string> g_PBC_CharacterCards;


// ---------------------------------------------------------------------------
// Push a fully-constructed event item onto the global queue.
// Thread-safe; may be called from any thread (in practice: main thread only).
// ---------------------------------------------------------------------------
void PBC_PushEvent(PBC_EventItem item);

// ---------------------------------------------------------------------------
// Per-character roll chance helper (main-thread only)
// ---------------------------------------------------------------------------

// Returns the effective roll chance for a character after applying its per-character
// modifier.  The result is clamped to [0, 100].
uint32_t PBC_GetEffectiveChance(uint64_t botGuid, uint32_t baseChance);

// ---------------------------------------------------------------------------
// Loader / WorldScript
// ---------------------------------------------------------------------------
void PBC_LoadConfig(bool isStartup = false);
void PBC_LoadCharacterCards();
void PBC_LoadHistoryFromDB();
void PBC_LoadCardAdditionsFromDB();
void PBC_SaveCardAdditionsToDB();
void PBC_LoadCharacterDataFromDB();
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
