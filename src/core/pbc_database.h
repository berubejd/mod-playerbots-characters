#ifndef MOD_PBC_DATABASE_H
#define MOD_PBC_DATABASE_H

// ---------------------------------------------------------------------------
// mod-playerbots-characters database helpers
//
// AzerothCore modules cannot add new entries to the core CharacterDatabase
// prepared-statement enum without patching core files.  We use adhoc queries
// (CharacterDatabase.Execute / CharacterDatabase.Query with raw SQL) for all
// module-specific writes.  This header provides thin wrappers to keep the
// SQL strings in one place.
// ---------------------------------------------------------------------------

#include "DatabaseEnv.h"
#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// Chat history
// ---------------------------------------------------------------------------

// Insert a single history line for a character.
inline void DB_InsertHistoryLine(uint64_t botGuid, const std::string& message)
{
    std::string escaped = message;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_chat_history (bot_guid, message) VALUES ({}, '{}')",
        botGuid,
        escaped
    );
}

// Delete all history rows for a character.
inline void DB_DeleteHistoryForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_chat_history WHERE bot_guid = {}",
        botGuid
    );
}

// Delete all history rows (used during periodic full-replace save).
inline void DB_DeleteAllHistory()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_chat_history");
}

// Update a single history message by bot GUID and 0-based index (position
// in the ordered list for that bot).  Uses a subquery to resolve the index
// to the actual DB row id, so no in-memory ID tracking is needed.
inline void DB_UpdateHistoryLineByIndex(uint64_t botGuid, size_t index, const std::string& newMessage)
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

// Delete a single history message by bot GUID and 0-based index.
inline void DB_DeleteHistoryLineByIndex(uint64_t botGuid, size_t index)
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

// Insert a single memory for a character.
inline void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance)
{
    std::string escaped = memoryText;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_memories (bot_guid, memory_text, importance) VALUES ({}, '{}', {})",
        botGuid,
        escaped,
        importance
    );
}

// Delete all memories for a character (used by .chars reset).
inline void DB_DeleteMemoriesForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE bot_guid = {}",
        botGuid
    );
}

// Delete all memories for every character (used by .chars reset @ALL).
inline void DB_DeleteAllMemories()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_memories");
}

// Update a single memory by DB row id.
inline void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance)
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

// Delete a single memory by DB row id.
inline void DB_DeleteMemoryById(uint64_t memoryId)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE id = {}",
        memoryId
    );
}

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

// Upsert the roll chance modifier for a character.
// modifier must be in range [-100, 100].
inline void DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier)
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

// Upsert a relationship description for a character with a named target.
// Also persists mention_count_at_last_update so server restarts don't
// trigger redundant relationship LLM calls.
inline void DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                                  const std::string& relationshipText,
                                  uint32_t mentionCount)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = relationshipText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_relationships "
        "  (bot_guid, target_name, relationship_text, mention_count_at_last_update) "
        "VALUES ({}, '{}', '{}', {}) "
        "ON DUPLICATE KEY UPDATE "
        "  relationship_text = '{}', "
        "  mention_count_at_last_update = {}, "
        "  updated_at = CURRENT_TIMESTAMP",
        botGuid,
        escapedName,
        escapedText,
        mentionCount,
        escapedText,
        mentionCount
    );
}

// Delete all relationship rows for a character (used by .chars reset).
inline void DB_DeleteRelationshipsForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {}",
        botGuid
    );
}

// Delete all relationship rows for every character (used by .chars reset @ALL).
inline void DB_DeleteAllRelationships()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_relationships");
}

// Update the relationship text for a specific (bot, target) pair.
// Only updates the text; mention_count_at_last_update is preserved.
inline void DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
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

// Delete a single relationship row for a specific (bot, target) pair.
inline void DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {} AND target_name = '{}'",
        botGuid,
        escapedName
    );
}

#endif // MOD_PBC_DATABASE_H
