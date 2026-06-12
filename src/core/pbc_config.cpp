#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"
#include "Config.h"
#include "World.h"
#include "Common.h"
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

// Global variable definitions

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

std::string g_PBC_PromptsPath = "../../../modules/mod-playerbots-characters/prompts";
std::string g_PBC_CharacterCardsPath = "../../../modules/mod-playerbots-characters/characters";

uint32_t g_PBC_ReplyChanceWhisper   = 100;
uint32_t g_PBC_ReplyChanceMention   = 100;
uint32_t g_PBC_ReplyChanceMessage   = 100;
uint32_t g_PBC_RollPenaltyOnAnswer  = 45;
uint32_t g_PBC_ReplyChanceItem     = 5;
uint32_t g_PBC_ReplyChanceDuel     = 5;
uint32_t g_PBC_ReplyChanceLevelUp  = 5;
uint32_t g_PBC_ReplyChanceHardCombat    = 25;
uint32_t g_PBC_ReplyChanceQuestCompleted = 20;
uint32_t g_PBC_ReplyChanceQuestTaken     = 10;
uint32_t g_PBC_ReplyChanceLocationChanged = 15;

uint32_t g_PBC_LocationChangeDebounceCycles = 5;
uint32_t g_PBC_CombatEndDebounceCycles      = 5;

std::string g_PBC_QuestCompletedSystemPrompt;
std::string g_PBC_QuestCompletedUserPrompt;
std::string g_PBC_QuestTakenSystemPrompt;
std::string g_PBC_QuestTakenUserPrompt;

std::string g_PBC_CombatEndedSystemPrompt;
std::string g_PBC_CombatEndedUserPrompt;

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

std::unordered_map<uint64_t, PBC_HistoryEntry>     g_PBC_History;
std::unordered_map<uint64_t, std::deque<uint64_t>> g_PBC_HistoryOwners;
std::mutex g_PBC_HistoryMutex;

std::unordered_map<uint64_t, std::vector<PBC_MemoryEntry>> g_PBC_Memories;
std::mutex g_PBC_MemoriesMutex;

std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
std::mutex g_PBC_RelationshipsMutex;

std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;
std::mutex g_PBC_DataMutex;

std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

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
    g_PBC_ReplyChanceHardCombat    = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceHardCombat", 25);
    g_PBC_ReplyChanceQuestCompleted = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestCompleted", 20);
    g_PBC_ReplyChanceQuestTaken     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestTaken", 10);
    g_PBC_ReplyChanceLocationChanged = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLocationChanged", 15);

    g_PBC_LocationChangeDebounceCycles = sConfigMgr->GetOption<uint32_t>("PBC.LocationChangeDebounceCycles", 5);
    g_PBC_CombatEndDebounceCycles      = sConfigMgr->GetOption<uint32_t>("PBC.CombatEndDebounceCycles", 5);

    std::string blacklistStr = sConfigMgr->GetOption<std::string>("PBC.Blacklist", "");
    g_PBC_Blacklist = SplitByComma(blacklistStr);

    g_PBC_HttpServerPort         = sConfigMgr->GetOption<int>("PBC.HttpServerPort", 0);
    g_PBC_HttpServerBind         = sConfigMgr->GetOption<std::string>("PBC.HttpServerBind", "127.0.0.1");
    g_PBC_HttpServerTimeout      = sConfigMgr->GetOption<int>("PBC.HttpServerTimeout", 15);
    g_PBC_HttpServerBaseUrl      = sConfigMgr->GetOption<std::string>("PBC.HttpServerBaseUrl", "http://127.0.0.1:8501");
    g_PBC_HttpServerPrivateKey   = sConfigMgr->GetOption<std::string>("PBC.HttpServerPrivateKey", "");
    g_PBC_HttpServerFrontendPath = sConfigMgr->GetOption<std::string>("PBC.HttpServerFrontendPath",
                                                                       "../../../modules/mod-playerbots-characters/frontend/dist");

    if (g_PBC_Enable)
    {
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
                PBC_Log(PBC_LogLevel::PBC_ERROR, "{} is not set. This is a required setting when the module is enabled.", check.key);
                configValid = false;
            }
        }

        if (g_PBC_MaxHistoryCtx == 0)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "PBC.MaxHistoryCtx is not set (or is 0). This is a required setting when the module is enabled.");
            configValid = false;
        }

        if (!configValid)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Required configuration is missing or empty. Module DISABLED — fix your playerbots_characters.conf and reload with .chars reload.");
            g_PBC_Enable = false;
            return;
        }

        if (g_PBC_HttpServerPort > 0 && g_PBC_HttpServerPrivateKey.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "PBC.HttpServerPrivateKey is not set but PBC.HttpServerPort is {}. "
                      "The private key is required for the authorization layer when the HTTP server is enabled. "
                      "HTTP server will NOT start.", g_PBC_HttpServerPort);
        }
    }

    if (g_PBC_Enable && !PBC_LoadPrompts())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Failed to load prompts. Module DISABLED — fix prompt path and reload with .chars reload.");
        g_PBC_Enable = false;
        return;
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "Config: Enable={} APIType='{}' Model='{}' Url='{}' MaxHistoryCtx={} MaxMemoriesCtx={} Timeout={}s "
        "Chances: Whisper={}% Mention={}% Message={}% RollPenalty={}% "
        "Item={}% Duel={}% LevelUp={}% HardCombat={}% QuestCompleted={}% QuestTaken={}%",
        g_PBC_Enable, g_PBC_APIType, g_PBC_Model, g_PBC_BaseUrl, g_PBC_MaxHistoryCtx, g_PBC_MaxMemoriesCtx,
        g_PBC_RequestTimeoutSec,
        g_PBC_ReplyChanceWhisper, g_PBC_ReplyChanceMention,
        g_PBC_ReplyChanceMessage, g_PBC_RollPenaltyOnAnswer,
        g_PBC_ReplyChanceItem,
        g_PBC_ReplyChanceDuel, g_PBC_ReplyChanceLevelUp,
        g_PBC_ReplyChanceHardCombat, g_PBC_ReplyChanceQuestCompleted, g_PBC_ReplyChanceQuestTaken);

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "HTTP Server: Port={} Bind='{}' Timeout={}s BaseUrl='{}' PrivateKey={} FrontendPath='{}'",
        g_PBC_HttpServerPort, g_PBC_HttpServerBind, g_PBC_HttpServerTimeout, g_PBC_HttpServerBaseUrl,
        g_PBC_HttpServerPrivateKey.empty() ? "(not set)" : "(set)", g_PBC_HttpServerFrontendPath);

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "Alt Model: Condensation={} RelationshipUpdate={} APIType='{}' Model='{}' Url='{}' Timeout={}s",
        g_PBC_UseAltModelForCondensation, g_PBC_UseAltModelForRelationshipUpdate,
        g_PBC_AltModelAPIType, g_PBC_AltModel, g_PBC_AltModelBaseUrl, g_PBC_AltModelRequestTimeoutSec);
}

// Try to read a file into target. Returns true if the file exists and is non-empty.
static bool TryReadFile(const std::string& path, std::string& target)
{
    std::ifstream f(path);
    if (!f)
        return false;

    std::stringstream buf;
    buf << f.rdbuf();
    if (buf.str().empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "Prompt file is empty: {}", path);
        return false;
    }

    target = buf.str();
    PBC_NormalizeNewlines(target);
    return true;
}

// Load a prompt by name (e.g. "Main.system").  Checks in priority order:
//   1. <prompts_path>/<DBC.Locale>/<name>.custom.txt
//   2. <prompts_path>/<DBC.Locale>/<name>.default.txt
//   3. <prompts_path>/enUS/<name>.custom.txt
//   4. <prompts_path>/enUS/<name>.default.txt
//
// Sets isCustom=true if a .custom.txt file was loaded.
// Returns true on success, false if no prompt file could be loaded.
static bool PBC_GetPrompt(const std::string& promptName,
                          std::string& promptText,
                          bool& isCustom)
{
    namespace fs = std::filesystem;
    isCustom = false;

    // Directories to try: configured locale first, then enUS fallback
    fs::path base(g_PBC_PromptsPath);
    std::string localeName = GetNameByLocaleConstant(sWorld->GetDefaultDbcLocale());

    std::vector<fs::path> dirs;
    if (localeName != "enUS")
    {
        fs::path localeDir = base / localeName;
        if (fs::exists(localeDir) && fs::is_directory(localeDir))
            dirs.push_back(localeDir);
    }
    dirs.push_back(base / "enUS");

    // In each directory, try custom first, then default
    for (auto const& dir : dirs)
    {
        std::string customPath = (dir / (promptName + ".custom.txt")).string();
        if (TryReadFile(customPath, promptText))
        {
            isCustom = true;
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded custom prompt '{}' from '{}' ({} chars)",
                     promptName, dir.string(), promptText.size());
            return true;
        }
    }

    for (auto const& dir : dirs)
    {
        std::string defaultPath = (dir / (promptName + ".default.txt")).string();
        if (TryReadFile(defaultPath, promptText))
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded default prompt '{}' from '{}' ({} chars)",
                     promptName, dir.string(), promptText.size());
            return true;
        }
    }

    PBC_Log(PBC_LogLevel::PBC_ERROR, "Prompt '{}' not found in '{}' or 'enUS' — module disabled.",
             promptName, localeName);
    return false;
}

bool PBC_LoadPrompts()
{
    // Verify enUS fallback directory exists
    namespace fs = std::filesystem;
    fs::path enusDir = fs::path(g_PBC_PromptsPath) / "enUS";
    if (!fs::exists(enusDir) || !fs::is_directory(enusDir))
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Fallback prompts directory not found: {}", enusDir.string());
        return false;
    }

    std::string localeName = GetNameByLocaleConstant(sWorld->GetDefaultDbcLocale());
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loading prompts (DBC.Locale={})", localeName);

    struct PromptEntry { const char* name; std::string& target; };
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
        { "CombatEnded.system",               g_PBC_CombatEndedSystemPrompt        },
        { "CombatEnded.user",                 g_PBC_CombatEndedUserPrompt          },
    };

    bool allOk = true;
    int customCount = 0;

    for (auto const& entry : prompts)
    {
        bool isCustom = false;
        if (!PBC_GetPrompt(entry.name, entry.target, isCustom))
        {
            allOk = false;
        }
        else if (isCustom)
        {
            ++customCount;
        }
    }

    if (!allOk)
        return false;

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loaded {} prompt(s) ({} custom)",
             static_cast<int>(sizeof(prompts) / sizeof(prompts[0])), customCount);
    return true;
}


void PBC_LoadCharacterCards()
{
    g_PBC_CharacterCards.clear();

    std::filesystem::path dir(g_PBC_CharacterCardsPath);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "Character cards directory not found: {}", g_PBC_CharacterCardsPath);
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
        if (!f) { PBC_Log(PBC_LogLevel::PBC_WARNING, "Cannot open card file: {}", path.string()); continue; }

        std::stringstream buf;
        buf << f.rdbuf();
        std::string cardText = buf.str();
        PBC_NormalizeNewlines(cardText);
        g_PBC_CharacterCards[name] = std::move(cardText);
        ++loaded;

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded card '{}' ({} chars)", name, g_PBC_CharacterCards[name].size());
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loaded {} character card(s) from '{}'", loaded, g_PBC_CharacterCardsPath);
}

uint32_t PBC_GetEffectiveChance(uint64_t botGuid, uint32_t baseChance)
{
    std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
    auto it = g_PBC_RollChanceModifiers.find(botGuid);
    if (it == g_PBC_RollChanceModifiers.end())
        return baseChance;
    int32_t effective = static_cast<int32_t>(baseChance) + it->second;
    return static_cast<uint32_t>(std::max(0, std::min(100, effective)));
}


