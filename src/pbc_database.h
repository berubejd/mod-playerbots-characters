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

// Insert a single history line for a bot.
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

// Delete all history rows for a bot.
inline void DB_DeleteHistoryForBot(uint64_t botGuid)
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

// ---------------------------------------------------------------------------
// Character card additions
// ---------------------------------------------------------------------------

// Insert a condensed addition for a bot.
inline void DB_InsertCardAddition(uint64_t botGuid, const std::string& addition)
{
    std::string escaped = addition;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_character_card_additions (bot_guid, addition) VALUES ({}, '{}')",
        botGuid,
        escaped
    );
}

// Delete all card additions for a bot (used by .chars reset).
inline void DB_DeleteCardAdditionsForBot(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_character_card_additions WHERE bot_guid = {}",
        botGuid
    );
}

// Delete all card additions for every bot (used by .chars reset @ALL).
inline void DB_DeleteAllCardAdditions()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_character_card_additions");
}

// ---------------------------------------------------------------------------
// Bot data (location + roll chance modifier)
// ---------------------------------------------------------------------------

// Upsert the last stable location for a bot (preserves roll_chance_modifier).
inline void DB_UpsertBotLocation(uint64_t botGuid, const std::string& location)
{
    std::string escaped = location;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_data (bot_guid, last_location) VALUES ({}, '{}') "
        "ON DUPLICATE KEY UPDATE last_location = '{}'",
        botGuid,
        escaped,
        escaped
    );
}

// Upsert the roll chance modifier for a bot (preserves last_location).
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

// Upsert a relationship description for a bot with a named target.
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

// Delete all relationship rows for a bot (used by .chars reset).
inline void DB_DeleteRelationshipsForBot(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {}",
        botGuid
    );
}

// Delete all relationship rows for every bot (used by .chars reset @ALL).
inline void DB_DeleteAllRelationships()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_relationships");
}

#endif // MOD_PBC_DATABASE_H
