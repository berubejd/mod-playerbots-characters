#include "pbc_commands.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "Chat.h"
#include "Config.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ChatCommand.h"
#include "Log.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <sstream>

using namespace Acore::ChatCommands;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static Player* FindTarget(ChatHandler* handler, std::optional<std::string_view> nameArg)
{
    if (!nameArg || nameArg->empty())
    {
        // Console has no session/player — caller must supply a name
        if (!handler->GetSession())
            return nullptr;
        return handler->GetSession()->GetPlayer();
    }
    return ObjectAccessor::FindPlayerByName(std::string(*nameArg));
}

// ---------------------------------------------------------------------------
// .chars reload
// ---------------------------------------------------------------------------
static bool HandleCharsReload(ChatHandler* handler, Optional<std::string_view>)
{
    sConfigMgr->Reload();
    PBC_LoadConfig();
    PBC_LoadPrompts();
    PBC_LoadCharacterCards();
    PBC_LoadCardAdditionsFromDB();
    PBC_LoadCharacterDataFromDB();

    // Reload history (and relationships) from DB safely by posting a
    // HistoryReload event onto the queue.  It will be processed after all
    // currently queued events finish, so no in-flight history writes are lost.
    // PBC_ProcessEventItem handles the relationship reload inside HistoryReload.
    PBC_EventItem ev;
    ev.type = PBC_EventType::HistoryReload;
    PBC_PushEvent(std::move(ev));

    handler->PSendSysMessage("[PBC] Config, prompts, character cards, card additions and character data reloaded. History/relationship reload queued (runs after pending events).");
    return true;
}

// ---------------------------------------------------------------------------
// .chars condense [char_name]
// ---------------------------------------------------------------------------
static bool HandleCharsCondense(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars condense <char_name>");
        return false;
    }
    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }
    if (!target->GetSession() || !target->GetSession()->IsBot())
    {
        handler->PSendSysMessage("[PBC] '{}' is not a playerbot.", target->GetName());
        return false;
    }

    PBC_TriggerCondensation(target);
    handler->PSendSysMessage("[PBC] Condensation triggered for '{}'.", target->GetName());
    return true;
}

// ---------------------------------------------------------------------------
// .chars info [char_name]
// ---------------------------------------------------------------------------
static bool HandleCharsInfo(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars info <char_name>");
        return false;
    }
    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    uint64_t botGuid = target->GetGUID().GetCounter();
    std::string card = PBC_GetCharacterCard(target);

    int addCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
        auto it = g_PBC_CardAdditions.find(botGuid);
        if (it != g_PBC_CardAdditions.end())
            addCount = static_cast<int>(it->second.size());
    }

    int histCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto it = g_PBC_ChatHistory.find(botGuid);
        if (it != g_PBC_ChatHistory.end())
            histCount = static_cast<int>(it->second.size());
    }

    int estimatedTokens = PBC_EstimateHistoryTokens(botGuid);

    int32_t rollMod = 0;
    {
        auto it = g_PBC_RollChanceModifiers.find(botGuid);
        if (it != g_PBC_RollChanceModifiers.end())
            rollMod = it->second;
    }

    handler->PSendSysMessage("[PBC] === {} ===", target->GetName());
    handler->PSendSysMessage("[PBC] Card additions: {}  |  History lines: {}  |  Est. tokens: {}/{}  |  Roll modifier: {:+d}",
        addCount, histCount, estimatedTokens, g_PBC_MaxCtx, rollMod);
    handler->PSendSysMessage("[PBC] Card:\n{}", card);
    return true;
}

// ---------------------------------------------------------------------------
// .chars reset [char_name]  /  .chars reset @ALL
// ---------------------------------------------------------------------------
static bool HandleCharsReset(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars reset <char_name>  or  chars reset @ALL");
        return false;
    }

    // Handle the @ALL special token: wipe every bot's data.
    if (nameArg && *nameArg == "@ALL")
    {
        DB_DeleteAllHistory();
        DB_DeleteAllCardAdditions();
        DB_DeleteAllRelationships();

        {
            std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
            g_PBC_ChatHistory.clear();
        }
        {
            std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
            g_PBC_CardAdditions.clear();
        }
        {
            std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
            g_PBC_Relationships.clear();
        }

        handler->PSendSysMessage("[PBC] History, card additions and relationships cleared for ALL characters.");
        return true;
    }

    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    uint64_t botGuid = target->GetGUID().GetCounter();

    DB_DeleteHistoryForCharacter(botGuid);
    DB_DeleteCardAdditionsForCharacter(botGuid);
    DB_DeleteRelationshipsForCharacter(botGuid);

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        g_PBC_ChatHistory.erase(botGuid);
    }
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
        g_PBC_CardAdditions.erase(botGuid);
    }
    {
        std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
        g_PBC_Relationships.erase(botGuid);
    }

    handler->PSendSysMessage("[PBC] History, card additions and relationships cleared for '{}'.", target->GetName());
    return true;
}

// ---------------------------------------------------------------------------
// .chars history [char_name] [num=5]
// ---------------------------------------------------------------------------
static bool HandleCharsHistory(ChatHandler* handler, Optional<std::string_view> nameArg,
                                Optional<int32> numArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars history <char_name> [num]");
        return false;
    }

    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    int num = numArg ? static_cast<int>(*numArg) : 5;
    num = std::max(1, std::min(num, 20));

    uint64_t botGuid = target->GetGUID().GetCounter();

    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_ChatHistory.find(botGuid);
    if (it == g_PBC_ChatHistory.end() || it->second.empty())
    {
        handler->PSendSysMessage("[PBC] No history found for '{}'.", target->GetName());
        return true;
    }

    const auto& hist = it->second;
    int total = static_cast<int>(hist.size());
    int start = std::max(0, total - num);

    handler->PSendSysMessage("[PBC] Last {} history entries for '{}' ({} total):",
        total - start, target->GetName(), total);
    for (int i = start; i < total; ++i)
        handler->PSendSysMessage("{}", hist[i]);

    return true;
}

// ---------------------------------------------------------------------------
// .chars relationship <char_name> <target_char_name>
// Outputs char_name's relationship towards target_char_name.
// ---------------------------------------------------------------------------
static bool HandleCharsRelationship(ChatHandler* handler,
                                    std::string_view charNameArg,
                                    std::string_view targetNameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if (charNameArg.empty() || targetNameArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars relationship <char_name> <target_char_name>");
        return false;
    }

    Player* bot = ObjectAccessor::FindPlayerByName(std::string(charNameArg));
    if (!bot)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", charNameArg);
        return false;
    }

    uint64_t botGuid = bot->GetGUID().GetCounter();
    std::string targetName(targetNameArg);

    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    auto botIt = g_PBC_Relationships.find(botGuid);
    if (botIt == g_PBC_Relationships.end())
    {
        handler->PSendSysMessage("[PBC] No relationship data found for '{}'.", bot->GetName());
        return true;
    }

    auto tgtIt = botIt->second.find(targetName);
    if (tgtIt == botIt->second.end())
    {
        handler->PSendSysMessage("[PBC] No relationship entry for '{}' -> '{}'.",
            bot->GetName(), targetName);
        return true;
    }

    handler->PSendSysMessage("[PBC] {}'s relationship with {} (mentions at last update: {}):\n{}",
        bot->GetName(), targetName,
        tgtIt->second.mentionCountAtLastUpdate,
        tgtIt->second.text);
    return true;
}

// ---------------------------------------------------------------------------
// .chars relationship_update <char_name> <target_char_name>
// Forces an immediate relationship update LLM call for char_name -> target.
// ---------------------------------------------------------------------------
static bool HandleCharsRelationshipUpdate(ChatHandler* handler,
                                          std::string_view charNameArg,
                                          std::string_view targetNameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if (charNameArg.empty() || targetNameArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars relationship_update <char_name> <target_char_name>");
        return false;
    }

    Player* bot = ObjectAccessor::FindPlayerByName(std::string(charNameArg));
    if (!bot)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", charNameArg);
        return false;
    }
    if (!bot->GetSession() || !bot->GetSession()->IsBot())
    {
        handler->PSendSysMessage("[PBC] '{}' is not a playerbot.", bot->GetName());
        return false;
    }

    if (g_PBC_RelationshipUpdateSystemPrompt.empty() || g_PBC_RelationshipUpdateUserPrompt.empty())
    {
        handler->PSendSysMessage("[PBC] Relationship update prompts are not configured.");
        return false;
    }

    std::string targetName(targetNameArg);
    uint64_t botGuid = bot->GetGUID().GetCounter();

    // Read current relationship text
    std::string currentRel;
    {
        std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
        auto botIt = g_PBC_Relationships.find(botGuid);
        if (botIt != g_PBC_Relationships.end())
        {
            auto tgtIt = botIt->second.find(targetName);
            if (tgtIt != botIt->second.end())
                currentRel = tgtIt->second.text;
        }
    }
    if (currentRel.empty())
        currentRel = PBC_DefaultRelationshipText(targetName);

    // Count current mentions of targetName in the bot's full history
    uint32_t total = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto hIt = g_PBC_ChatHistory.find(botGuid);
        if (hIt != g_PBC_ChatHistory.end())
            total = PBC_CountMentions(hIt->second, targetName);
    }

    PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(bot);

    PBC_EventItem relEv;
    relEv.type                       = PBC_EventType::RelationshipUpdate;
    relEv.relationshipChar            = std::move(snap);
    relEv.relationshipTargetName     = targetName;
    relEv.relationshipTargetInfo     = targetName;
    relEv.relationshipCurrentText    = currentRel;
    relEv.relationshipMentionTotal   = total;
    relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
    relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

    PBC_PushEvent(std::move(relEv));

    handler->PSendSysMessage("[PBC] Relationship update queued for '{}' -> '{}'.",
        bot->GetName(), targetName);
    return true;
}

// ---------------------------------------------------------------------------
// .chars roll_modifier [char_name] [roll_modifier]
// Sets or displays the per-character roll chance modifier (-100 to 100).
// ---------------------------------------------------------------------------
static bool HandleCharsRollModifier(ChatHandler* handler,
                                    Optional<std::string_view> nameArg,
                                    Optional<int32> modifierArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    // Need at least a character name
    if (!nameArg || nameArg->empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars roll_modifier <char_name> [roll_modifier]");
        handler->PSendSysMessage("[PBC]   roll_modifier: integer from -100 to 100. Positive = more talkative, negative = less talkative.");
        handler->PSendSysMessage("[PBC]   Omit roll_modifier to display the current value.");
        return false;
    }

    Player* target = ObjectAccessor::FindPlayerByName(std::string(*nameArg));
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", *nameArg);
        return false;
    }

    uint64_t botGuid = target->GetGUID().GetCounter();

    // No modifier argument: display current value
    if (!modifierArg)
    {
        auto it = g_PBC_RollChanceModifiers.find(botGuid);
        int32_t currentMod = (it != g_PBC_RollChanceModifiers.end()) ? it->second : 0;
        handler->PSendSysMessage("[PBC] {}'s roll chance modifier: {:+d}", target->GetName(), currentMod);
        return true;
    }

    int32_t modifier = *modifierArg;
    if (modifier < -100 || modifier > 100)
    {
        handler->PSendSysMessage("[PBC] Roll modifier must be between -100 and 100 (got {}).", modifier);
        return false;
    }

    // Update in-memory map
    if (modifier == 0)
        g_PBC_RollChanceModifiers.erase(botGuid);
    else
        g_PBC_RollChanceModifiers[botGuid] = modifier;

    // Persist to database
    DB_UpsertRollChanceModifier(botGuid, modifier);

    handler->PSendSysMessage("[PBC] {}'s roll chance modifier set to {:+d}.", target->GetName(), modifier);
    return true;
}

// ---------------------------------------------------------------------------
// .chars apitest [query=hi]
// ---------------------------------------------------------------------------
static bool HandleCharsApiTest(ChatHandler* handler, Optional<std::string_view> queryArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    std::string query = (queryArg && !queryArg->empty()) ? std::string(*queryArg) : "hi";

    handler->PSendSysMessage("[PBC] API test: querying with '{}'...", query);

    PBC_LLMResult result = PBC_CallLLM("Answer in one single short sentence.", query);

    if (result.success)
    {
        handler->PSendSysMessage("[PBC] API test OK ({} tokens): {}", result.tokensUsed, result.text);
    }
    else
    {
        handler->PSendSysMessage("[PBC] API test FAILED — no valid response received. Check server logs for details.");
    }

    return result.success;
}

// ---------------------------------------------------------------------------
// .chars context [char_name]
// ---------------------------------------------------------------------------
static bool HandleCharsContext(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars context <char_name>");
        return false;
    }
    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    std::string context = PBC_GetCharacterContext(target);
    handler->PSendSysMessage("[PBC] === {} context ===\n{}", target->GetName(), context);
    return true;
}

// ---------------------------------------------------------------------------
// .chars web
// ---------------------------------------------------------------------------
static bool HandleCharsWeb(ChatHandler* handler, Optional<std::string_view>)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    // This command only works in-game
    if (!handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] This command is intended to be used in-game only.");
        return false;
    }

    // Check if the HTTP server is running
    if (!PBC_HttpServerIsRunning())
    {
        handler->PSendSysMessage("[PBC] Web interface is not enabled.");
        return false;
    }

    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
    {
        handler->PSendSysMessage("[PBC] Could not retrieve player data.");
        return false;
    }

    uint64_t playerGuid = player->GetGUID().GetCounter();
    std::string otp = PBC_HttpServerGenerateOTP(playerGuid);

    if (otp.empty())
    {
        handler->PSendSysMessage("[PBC] Failed to generate one-time password. Please try again.");
        return false;
    }

    handler->PSendSysMessage("[PBC] To use web-interface, go to {} and input your one-time password: {}",
        g_PBC_HttpServerBaseUrl, otp);
    return true;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

PBC_CommandScript::PBC_CommandScript() : CommandScript("PBC_CommandScript") {}

ChatCommandTable PBC_CommandScript::GetCommands() const
{
    static ChatCommandTable charsSubCommands =
    {
        { "reload",              HandleCharsReload,             SEC_GAMEMASTER, Console::Yes },
        { "condense",            HandleCharsCondense,           SEC_GAMEMASTER, Console::Yes },
        { "info",                HandleCharsInfo,               SEC_GAMEMASTER, Console::Yes },
        { "reset",               HandleCharsReset,              SEC_GAMEMASTER, Console::Yes },
        { "history",             HandleCharsHistory,            SEC_GAMEMASTER, Console::Yes },
        { "relationship",        HandleCharsRelationship,       SEC_GAMEMASTER, Console::Yes },
        { "relationship_update", HandleCharsRelationshipUpdate, SEC_GAMEMASTER, Console::Yes },
        { "roll_modifier",       HandleCharsRollModifier,       SEC_GAMEMASTER, Console::Yes },
        { "context",             HandleCharsContext,            SEC_GAMEMASTER, Console::Yes },
        { "apitest",             HandleCharsApiTest,            SEC_GAMEMASTER, Console::Yes },
        { "web",                 HandleCharsWeb,                SEC_PLAYER,    Console::No  },
    };

    static ChatCommandTable rootTable =
    {
        { "chars", charsSubCommands },
    };

    return rootTable;
}
