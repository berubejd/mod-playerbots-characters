#ifndef MOD_PBC_CONFIG_H
#define MOD_PBC_CONFIG_H

#include <string>
#include <vector>
#include <map>
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

// Module enable / debug
extern bool     g_PBC_Enable;
extern bool     g_PBC_DebugEnabled;
extern bool     g_PBC_DebugShowFullRequest;
extern bool     g_PBC_DisplayNarratorEvents;
extern bool     g_PBC_TrackPlayerCharacter;
extern bool     g_PBC_CardAdditionsMigrationNeeded;

// LLM API connection
extern std::string g_PBC_APIType;
extern std::string g_PBC_BaseUrl;
extern std::string g_PBC_ApiKey;
extern std::string g_PBC_Model;
extern int         g_PBC_MaxResponseTokens;
extern double      g_PBC_Temperature;
extern std::string g_PBC_ModelExtraParameters;
extern int         g_PBC_RequestTimeoutSec;

// Alternative API (for condensation / relationship updates)
extern bool        g_PBC_UseAltModelForCondensation;
extern bool        g_PBC_UseAltModelForRelationshipUpdate;
extern std::string g_PBC_AltModelAPIType;
extern std::string g_PBC_AltModelBaseUrl;
extern std::string g_PBC_AltModelApiKey;
extern std::string g_PBC_AltModel;
extern int         g_PBC_AltModelMaxResponseTokens;
extern double      g_PBC_AltModelTemperature;
extern std::string g_PBC_AltModelModelExtraParameters;
extern int         g_PBC_AltModelRequestTimeoutSec;

// Context / condensation
extern uint32_t    g_PBC_MaxHistoryCtx;
extern uint32_t    g_PBC_MaxMemoriesCtx;

// Prompt templates
extern std::string g_PBC_SystemPrompt;
extern std::string g_PBC_UserPrompt;
extern std::string g_PBC_CondensationSystemPrompt;
extern std::string g_PBC_CondensationUserPrompt;
extern std::string g_PBC_DefaultCharacterDescription;
extern std::string g_PBC_CharacterContext;

// Relationship update prompts
extern std::string g_PBC_RelationshipUpdateSystemPrompt;
extern std::string g_PBC_RelationshipUpdateUserPrompt;

// Paths
extern std::string g_PBC_PromptsPath;
extern std::string g_PBC_CharacterCardsPath;

// Reply chances (0-100)
extern uint32_t g_PBC_ReplyChanceWhisper;
extern uint32_t g_PBC_ReplyChanceMention;
extern uint32_t g_PBC_ReplyChanceMessage;
extern uint32_t g_PBC_RollPenaltyOnAnswer;
extern uint32_t g_PBC_ReplyChanceItem;
extern uint32_t g_PBC_ReplyChanceDuel;
extern uint32_t g_PBC_ReplyChanceLevelUp;
extern uint32_t g_PBC_ReplyChanceHardCombat;
extern uint32_t g_PBC_ReplyChanceQuestCompleted;
extern uint32_t g_PBC_ReplyChanceQuestTaken;
extern uint32_t g_PBC_ReplyChanceLocationChanged;

extern uint32_t g_PBC_LocationChangeDebounceCycles;
extern uint32_t g_PBC_CombatEndDebounceCycles;

// Quest LLM prompts
extern std::string g_PBC_QuestCompletedSystemPrompt;
extern std::string g_PBC_QuestCompletedUserPrompt;
extern std::string g_PBC_QuestTakenSystemPrompt;
extern std::string g_PBC_QuestTakenUserPrompt;

// Combat LLM prompts
extern std::string g_PBC_CombatEndedSystemPrompt;
extern std::string g_PBC_CombatEndedUserPrompt;

// HTTP server
extern int         g_PBC_HttpServerPort;
extern std::string g_PBC_HttpServerBind;
extern int         g_PBC_HttpServerTimeout;
extern std::string g_PBC_HttpServerBaseUrl;
extern std::string g_PBC_HttpServerPrivateKey;
extern std::string g_PBC_HttpServerFrontendPath;

// Blacklist prefixes
extern std::vector<std::string> g_PBC_Blacklist;

// Per-character roll chance modifiers (bot_guid -> modifier, -100..100)
extern std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;
extern std::mutex g_PBC_DataMutex;

// Snapshot of a single character's state, captured on the main thread.
// The event thread works exclusively with these — never touches live Player*.
struct PBC_CharacterSnapshot
{
    ObjectGuid  charObjGuid;
    uint64_t    charGuidRaw = 0;
    std::string charName;

    // Pre-rendered prompt fragments (captured on main thread)
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
    std::string petInfo;
    std::string charGroup;
    std::string charLos;
    std::string combatStatus;
    std::string equipment;

    // The character's history at the moment of snapshotting.
    // The event thread appends replies locally so subsequent characters
    // see an up-to-date history within the same event.
    std::deque<std::string> history;

    // For whisper responses
    ObjectGuid  whisperTargetGuid;
    std::string whisperTargetName;

    // Names of all other party members (excluding this character).
    std::vector<std::string> partyMemberNames;

    // True if at least one group member is a real (non-bot) player.
    bool hasRealPlayerInGroup = false;
};

// Event types
enum class PBC_EventType : uint8_t
{
    Normal,                 // One or more characters may respond
    QuestSummarization,     // Summarize via LLM, then process responders
    CombatSummarization,    // Summarize via LLM, then process responders
    Condensation,           // Extract memories from history, clear it
    HistoryReload,          // Reload all histories from DB
    RelationshipUpdate,     // Update one character's relationship with a target
    CardAdditionsMigration, // Convert legacy card additions into memories
};

// A single unit of work for the event queue.
struct PBC_EventItem
{
    PBC_EventType type = PBC_EventType::Normal;

    // Normal / QuestSummarization / CombatSummarization fields
    std::string eventLine;          // Present-tense for [CURRENT EVENT]
    std::string histLine;           // Past-tense appended to history after processing
    uint32_t    chatType = 0;       // Chat channel for bot replies
    std::vector<PBC_CharacterSnapshot> respondingChars;  // Rolled to respond
    std::vector<uint64_t> silentCharGuids;               // Receive histLine only
    std::vector<uint64_t> playerCharGuids;               // Real players receiving history

    // For whisper events
    std::string whisperSenderName;
    std::string whisperTargetName;

    // GUIDs that already have histLine but still need new replies
    std::vector<uint64_t> replyOnlyCharGuids;
    bool canCreateEvents = false;   // If true, triggers secondary events

    // QuestSummarization extra fields
    std::string questSystemPrompt;
    std::string questUserPrompt;

    // CombatSummarization extra fields
    std::string combatSystemPrompt;
    std::string combatUserPrompt;

    // Player who triggered the quest event
    ObjectGuid  anchorObjGuid;

    // Condensation fields
    PBC_CharacterSnapshot condensationChar;
    std::string condensationSystemPrompt;
    std::string condensationUserPrompt;

    // RelationshipUpdate fields
    PBC_CharacterSnapshot relationshipChar;
    std::string     relationshipTargetName;
    std::string     relationshipTargetInfo;
    std::string     relationshipCurrentText;
    std::string     relationshipSystemPrompt;
    std::string     relationshipUserPromptTmpl;

    // CardAdditionsMigration fields
    std::string     migrationCondensationSystemPrompt;
    std::string     migrationCondensationUserPromptTmpl;
};

// Chat-send action posted from event thread to main thread.
struct PBC_PendingAction
{
    ObjectGuid  charGuid;
    ObjectGuid  targetGuid;     // Non-empty = whisper target
    uint32_t    chatType = 0;
    std::string text;           // Empty = no-op
    bool        isNarratorMessage = false;  // Send as narrator system message
};

extern std::queue<PBC_PendingAction> g_PBC_PendingActions;
extern std::mutex                    g_PBC_PendingActionsMutex;

// Secondary event request from event thread to main thread.
struct PBC_PendingEventRequest
{
    std::string eventLine;
    std::string histLine;
    std::string originHistLine;  // Original trigger's histLine for context
    uint32_t chatType = 0;
    uint64_t anchorCharGuid = 0;
    std::unordered_set<uint64_t> excludedCharGuids;
    std::vector<uint64_t> originCharGuids;
    std::vector<uint64_t> playerCharGuids;
};

extern std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
extern std::mutex                          g_PBC_PendingEventRequestsMutex;

// Whisper request from HTTP API thread to main thread.
struct PBC_PendingWhisperRequest
{
    std::string message;
    uint64_t    senderGuid = 0;
    uint64_t    targetGuid = 0;
};

extern std::queue<PBC_PendingWhisperRequest> g_PBC_PendingWhisperRequests;
extern std::mutex                            g_PBC_PendingWhisperRequestsMutex;

// Party message request from HTTP API thread to main thread.
struct PBC_PendingPartyMessageRequest
{
    std::string senderName;
    uint64_t    senderGuid = 0;
    std::string message;
};

extern std::queue<PBC_PendingPartyMessageRequest> g_PBC_PendingPartyMessageRequests;
extern std::mutex                                  g_PBC_PendingPartyMessageRequestsMutex;

// Trigger request from HTTP API thread to main thread.
struct PBC_PendingTriggerRequest
{
    uint64_t targetGuid = 0;
};

extern std::queue<PBC_PendingTriggerRequest> g_PBC_PendingTriggerRequests;
extern std::mutex                            g_PBC_PendingTriggerRequestsMutex;

// Universal event queue. Drained by OnUpdate — one event at a time.
extern std::queue<PBC_EventItem>  g_PBC_EventQueue;
extern std::mutex                 g_PBC_EventQueueMutex;

// True when no event thread is running.
extern std::atomic<bool> g_PBC_EventThreadDone;

// In-memory chat history: bot_guid -> ordered list of pre-formatted lines
extern std::unordered_map<uint64_t, std::deque<std::string>> g_PBC_ChatHistory;
extern std::mutex g_PBC_HistoryMutex;

// Last message timestamp per character for time-gap detection
extern std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

// In-memory character memories
struct PBC_MemoryEntry
{
    uint64_t    dbId = 0;
    std::string text;
    uint8_t     importance = 5;
    std::string createdAt;       // YYYY-MM-DD
};

extern std::unordered_map<uint64_t, std::vector<PBC_MemoryEntry>> g_PBC_Memories;
extern std::mutex g_PBC_MemoriesMutex;

// One relationship entry
struct PBC_RelationshipEntry
{
    std::string text;
    std::string updatedAt;      // YYYY-MM-DD hh:ii:ss
};

// In-memory relationships: bot_guid -> (target_name -> entry)
extern std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
extern std::mutex g_PBC_RelationshipsMutex;

// Character cards loaded from disk: name -> card text
extern std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

void PBC_PushEvent(PBC_EventItem item);

// Per-character roll chance helper (main-thread only)
uint32_t PBC_GetEffectiveChance(uint64_t botGuid, uint32_t baseChance);

// Loader / WorldScript
void PBC_LoadConfig(bool isStartup = false);
bool PBC_LoadPrompts();
void PBC_LoadCharacterCards();

#endif // MOD_PBC_CONFIG_H
