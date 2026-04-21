#include "pbc_commands.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
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
    PBC_LoadCharacterCards();
    PBC_LoadCardAdditionsFromDB();

    // Reload history (and relationships) from DB safely by posting a
    // HistoryReload event onto the queue.  It will be processed after all
    // currently queued events finish, so no in-flight history writes are lost.
    // PBC_ProcessEventItem handles the relationship reload inside HistoryReload.
    PBC_EventItem ev;
    ev.type = PBC_EventType::HistoryReload;
    PBC_PushEvent(std::move(ev));

    handler->PSendSysMessage("[PBC] Config, character cards and card additions reloaded. History/relationship reload queued (runs after pending events).");
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

    handler->PSendSysMessage("[PBC] === {} ===", target->GetName());
    handler->PSendSysMessage("[PBC] Card additions: {}  |  History lines: {}  |  Est. tokens: {}/{}",
        addCount, histCount, estimatedTokens, g_PBC_MaxCtx);
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

    DB_DeleteHistoryForBot(botGuid);
    DB_DeleteCardAdditionsForBot(botGuid);
    DB_DeleteRelationshipsForBot(botGuid);

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
        currentRel = "You don't know much about " + targetName + ".";

    // Count current mentions of targetName in the bot's full history
    uint32_t total = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto hIt = g_PBC_ChatHistory.find(botGuid);
        if (hIt != g_PBC_ChatHistory.end())
        {
            for (const auto& line : hIt->second)
            {
                size_t pos = 0;
                while ((pos = line.find(targetName, pos)) != std::string::npos)
                {
                    ++total;
                    pos += targetName.size();
                }
            }
        }
    }

    PBC_BotSnapshot snap = PBC_SnapshotBot(bot);

    PBC_EventItem relEv;
    relEv.type                       = PBC_EventType::RelationshipUpdate;
    relEv.relationshipBot            = std::move(snap);
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
        { "apitest",             HandleCharsApiTest,            SEC_GAMEMASTER, Console::Yes },
    };

    static ChatCommandTable rootTable =
    {
        { "chars", charsSubCommands },
    };

    return rootTable;
}
