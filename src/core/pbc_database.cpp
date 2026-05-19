#include "pbc_database.h"
#include "pbc_config.h"
#include "pbc_log.h"
#include "pbc_utils.h"

#include "DatabaseEnv.h"

#include <string>
#include <cstdint>
#include <ctime>

// ---------------------------------------------------------------------------
// Chat history
// ---------------------------------------------------------------------------

void DB_InsertHistoryLine(uint64_t botGuid, const std::string& message)
{
    std::string escaped = message;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_chat_history (bot_guid, message) VALUES ({}, '{}')",
        botGuid,
        escaped
    );
}

void DB_DeleteHistoryForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_chat_history WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllHistory()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_chat_history");
}

void DB_UpdateHistoryLineByIndex(uint64_t botGuid, size_t index, const std::string& newMessage)
{
    std::string escaped = newMessage;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_chat_history SET message = '{}' "
        "WHERE id = ("
        "  SELECT id FROM ("
        "    SELECT id FROM mod_pbc_chat_history WHERE bot_guid = {} ORDER BY id ASC LIMIT 1 OFFSET {}"
        "  ) AS t"
        ")",
        escaped,
        botGuid,
        index
    );
}

void DB_DeleteHistoryLineByIndex(uint64_t botGuid, size_t index)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_chat_history "
        "WHERE id = ("
        "  SELECT id FROM ("
        "    SELECT id FROM mod_pbc_chat_history WHERE bot_guid = {} ORDER BY id ASC LIMIT 1 OFFSET {}"
        "  ) AS t"
        ")",
        botGuid,
        index
    );
}

// ---------------------------------------------------------------------------
// Character memories
// ---------------------------------------------------------------------------

void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance)
{
    std::string escaped = memoryText;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.DirectExecute(
        "INSERT INTO mod_pbc_memories (bot_guid, memory_text, importance) VALUES ({}, '{}', {})",
        botGuid,
        escaped,
        importance
    );
}

void DB_DeleteMemoriesForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllMemories()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_memories");
}

void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance)
{
    std::string escaped = newText;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_memories SET memory_text = '{}', importance = {} WHERE id = {}",
        escaped,
        static_cast<uint32_t>(importance),
        memoryId
    );
}

void DB_DeleteMemoryById(uint64_t memoryId)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE id = {}",
        memoryId
    );
}

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

void DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier)
{
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_data (bot_guid, roll_chance_modifier) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE roll_chance_modifier = {}",
        botGuid,
        modifier,
        modifier
    );
}

// ---------------------------------------------------------------------------
// Character relationships
// ---------------------------------------------------------------------------

void DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                            const std::string& relationshipText)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = relationshipText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_relationships "
        "  (bot_guid, target_name, relationship_text) "
        "VALUES ({}, '{}', '{}') "
        "ON DUPLICATE KEY UPDATE "
        "  relationship_text = '{}', "
        "  updated_at = CURRENT_TIMESTAMP",
        botGuid,
        escapedName,
        escapedText,
        escapedText
    );
}

void DB_DeleteRelationshipsForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllRelationships()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_relationships");
}

void DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
                                const std::string& newText)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = newText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_relationships SET relationship_text = '{}' "
        "WHERE bot_guid = {} AND target_name = '{}'",
        escapedText,
        botGuid,
        escapedName
    );
}

void DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {} AND target_name = '{}'",
        botGuid,
        escapedName
    );
}

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

bool DB_MemoriesTableEmpty()
{
    QueryResult result = CharacterDatabase.Query("SELECT 1 FROM mod_pbc_memories LIMIT 1");
    return !result;
}

bool DB_CardAdditionsTableNotEmpty()
{
    QueryResult tableCheck = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_character_card_additions'"
    );
    if (!tableCheck || (*tableCheck)[0].Get<uint64_t>() == 0)
        return false;

    QueryResult result = CharacterDatabase.Query("SELECT 1 FROM mod_pbc_character_card_additions LIMIT 1");
    return !!result;
}

// ---------------------------------------------------------------------------
// DB Loader functions (moved from pbc_config.cpp)
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

    PBC_Log(PBC_LogLevel::DEFAULT, "Chat history loaded from DB ({} characters with timestamps).",
             g_PBC_LastHistoryTime.size());
}

void PBC_LoadMemoriesFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT id, bot_guid, memory_text, importance, UNIX_TIMESTAMP(created_at) FROM mod_pbc_memories ORDER BY bot_guid ASC, id ASC"
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
        time_t      createdAt  = static_cast<time_t>((*result)[4].Get<uint64_t>());

        PBC_MemoryEntry entry;
        entry.dbId       = dbId;
        entry.text       = std::move(memText);
        entry.importance = importance;
        entry.createdAt  = PBC_FormatDate(createdAt);
        g_PBC_Memories[botGuid].push_back(std::move(entry));
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::DEFAULT, "Character memories loaded from DB ({} entries).", count);
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
        PBC_Log(PBC_LogLevel::DEFAULT, "Characters data loaded from DB (0 entries).");
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

    PBC_Log(PBC_LogLevel::DEFAULT, "Characters data loaded from DB ({} entries, {} with roll modifier).",
             count, g_PBC_RollChanceModifiers.size());
}

void PBC_LoadRelationshipsFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, target_name, relationship_text, "
        "UNIX_TIMESTAMP(updated_at) FROM mod_pbc_relationships"
    );

    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    g_PBC_Relationships.clear();

    if (!result)
    {
        PBC_Log(PBC_LogLevel::DEFAULT, "Relationships loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t    botGuid    = (*result)[0].Get<uint64_t>();
        std::string targetName = (*result)[1].Get<std::string>();
        std::string relText    = (*result)[2].Get<std::string>();
        time_t      updatedAt  = static_cast<time_t>((*result)[3].Get<uint64_t>());

        auto& entry = g_PBC_Relationships[botGuid][targetName];
        entry.text      = std::move(relText);
        entry.updatedAt = PBC_FormatDateTime(updatedAt);
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::DEFAULT, "Relationships loaded from DB ({} entries).", count);
}
