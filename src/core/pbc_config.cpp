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
bool     g_PBC_CardAdditionsMigrationNeeded = false;

std::string g_PBC_APIType          = "openai";
std::string g_PBC_BaseUrl          = "";
std::string g_PBC_ApiKey           = "";
std::string g_PBC_Model            = "";
int         g_PBC_MaxResponseTokens = 120;
double      g_PBC_Temperature      = 1.0;
std::string g_PBC_ModelExtraParameters;
int         g_PBC_RequestTimeoutSec = 30;

bool        g_PBC_UseAltModelForCondensation      = false;
bool        g_PBC_UseAltModelForRelationshipUpdate = false;
std::string g_PBC_AltModelAPIType                  = "openai";
std::string g_PBC_AltModelBaseUrl;
std::string g_PBC_AltModelApiKey;
std::string g_PBC_AltModel;
int         g_PBC_AltModelMaxResponseTokens        = 0;
double      g_PBC_AltModelTemperature              = 1.0;
std::string g_PBC_AltModelModelExtraParameters;
int         g_PBC_AltModelRequestTimeoutSec         = 30;

uint32_t    g_PBC_MaxHistoryCtx              = 0;
uint32_t    g_PBC_MaxMemoriesCtx             = 8192;

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
uint32_t g_PBC_ReplyChanceLocationChanged = 15;

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

std::queue<PBC_PendingPartyMessageRequest> g_PBC_PendingPartyMessageRequests;
std::mutex                                  g_PBC_PendingPartyMessageRequestsMutex;

std::queue<PBC_PendingTriggerRequest> g_PBC_PendingTriggerRequests;
std::mutex                            g_PBC_PendingTriggerRequestsMutex;

std::queue<PBC_EventItem>  g_PBC_EventQueue;
std::mutex                 g_PBC_EventQueueMutex;
std::atomic<bool>          g_PBC_EventThreadDone{ true };

std::unordered_map<uint64_t, std::deque<std::string>> g_PBC_ChatHistory;
std::mutex g_PBC_HistoryMutex;

std::unordered_map<uint64_t, std::vector<PBC_MemoryEntry>> g_PBC_Memories;
std::mutex g_PBC_MemoriesMutex;

std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
std::mutex g_PBC_RelationshipsMutex;

std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;
std::mutex g_PBC_DataMutex;

std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

std::unordered_map<uint32_t, PBC_PartyState> g_PBC_PartyStates;
std::mutex g_PBC_PartyStateMutex;

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

    // Alt model configuration
    g_PBC_UseAltModelForCondensation      = sConfigMgr->GetOption<bool>("PBC.UseAltModelForCondensation", false);
    g_PBC_UseAltModelForRelationshipUpdate = sConfigMgr->GetOption<bool>("PBC.UseAltModelForRelationshipUpdate", false);
    g_PBC_AltModelAPIType                 = sConfigMgr->GetOption<std::string>("PBC.AltModelAPIType", "openai");
    g_PBC_AltModelBaseUrl                 = sConfigMgr->GetOption<std::string>("PBC.AltModelBaseUrl", "");
    g_PBC_AltModelApiKey                  = sConfigMgr->GetOption<std::string>("PBC.AltModelApiKey", "");
    g_PBC_AltModel                        = sConfigMgr->GetOption<std::string>("PBC.AltModel", "");
    g_PBC_AltModelMaxResponseTokens       = sConfigMgr->GetOption<int>("PBC.AltModelMaxResponseLength", 0);
    g_PBC_AltModelTemperature             = std::round(static_cast<double>(sConfigMgr->GetOption<float>("PBC.AltModelTemperature", 1.0f)) * 100.0) / 100.0;
    g_PBC_AltModelModelExtraParameters    = sConfigMgr->GetOption<std::string>("PBC.AltModelModelExtraParameters", "");
    g_PBC_AltModelRequestTimeoutSec       = sConfigMgr->GetOption<int>("PBC.AltModelRequestTimeoutSec", 30);

    g_PBC_MaxHistoryCtx              = sConfigMgr->GetOption<uint32_t>("PBC.MaxHistoryCtx", 0);
    g_PBC_MaxMemoriesCtx             = sConfigMgr->GetOption<uint32_t>("PBC.MaxMemoriesCtx", 8192);

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
    g_PBC_ReplyChanceLocationChanged = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLocationChanged", 15);

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

        if (g_PBC_MaxHistoryCtx == 0)
        {
            LOG_ERROR("server.loading", "[PBC] PBC.MaxHistoryCtx is not set (or is 0). This is a required setting when the module is enabled.");
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
        "[PBC] Config: Enable={} APIType='{}' Model='{}' Url='{}' MaxHistoryCtx={} MaxMemoriesCtx={} Timeout={}s "
        "Chances: Whisper={}% Mention={}% Message={}% RollPenalty={}% "
        "Item={}% Duel={}% LevelUp={}% BossKill={}% QuestCompleted={}% QuestTaken={}%",
        g_PBC_Enable, g_PBC_APIType, g_PBC_Model, g_PBC_BaseUrl, g_PBC_MaxHistoryCtx, g_PBC_MaxMemoriesCtx,
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

    LOG_INFO("server.loading",
        "[PBC] Alt Model: Condensation={} RelationshipUpdate={} APIType='{}' Model='{}' Url='{}' Timeout={}s",
        g_PBC_UseAltModelForCondensation, g_PBC_UseAltModelForRelationshipUpdate,
        g_PBC_AltModelAPIType, g_PBC_AltModel, g_PBC_AltModelBaseUrl, g_PBC_AltModelRequestTimeoutSec);
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

void PBC_LoadMemoriesFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT id, bot_guid, memory_text, importance FROM mod_pbc_memories ORDER BY bot_guid ASC, id ASC"
    );

    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    g_PBC_Memories.clear();

    if (!result) return;

    size_t count = 0;
    do {
        uint64_t    dbId       = (*result)[0].Get<uint64_t>();
        uint64_t    botGuid    = (*result)[1].Get<uint64_t>();
        std::string memText    = (*result)[2].Get<std::string>();
        uint8_t     importance = static_cast<uint8_t>((*result)[3].Get<uint32_t>());

        PBC_MemoryEntry entry;
        entry.dbId       = dbId;
        entry.text       = std::move(memText);
        entry.importance = importance;
        g_PBC_Memories[botGuid].push_back(std::move(entry));
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", "[PBC] Character memories loaded from DB ({} entries).", count);
}

void PBC_LoadCharacterDataFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, roll_chance_modifier FROM mod_pbc_data"
    );

    std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
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
    std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
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
