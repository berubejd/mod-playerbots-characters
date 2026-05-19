#ifndef MOD_PBC_DATABASE_H
#define MOD_PBC_DATABASE_H

#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// Chat history
// ---------------------------------------------------------------------------

// Insert a single history line for a character.
void DB_InsertHistoryLine(uint64_t botGuid, const std::string& message);

// Delete all history rows for a character.
void DB_DeleteHistoryForCharacter(uint64_t botGuid);

// Delete all history rows (used during periodic full-replace save).
void DB_DeleteAllHistory();

// Update a single history message by bot GUID and 0-based index (position
// in the ordered list for that bot).  Uses a subquery to resolve the index
// to the actual DB row id, so no in-memory ID tracking is needed.
void DB_UpdateHistoryLineByIndex(uint64_t botGuid, size_t index, const std::string& newMessage);

// Delete a single history message by bot GUID and 0-based index.
void DB_DeleteHistoryLineByIndex(uint64_t botGuid, size_t index);

// ---------------------------------------------------------------------------
// Character memories
// ---------------------------------------------------------------------------

// Insert a single memory for a character.
void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance);

// Delete all memories for a character (used by .chars reset).
void DB_DeleteMemoriesForCharacter(uint64_t botGuid);

// Delete all memories for every character (used by .chars reset @ALL).
void DB_DeleteAllMemories();

// Update a single memory by DB row id.
void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance);

// Delete a single memory by DB row id.
void DB_DeleteMemoryById(uint64_t memoryId);

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

// Upsert the roll chance modifier for a character.
// modifier must be in range [-100, 100].
void DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier);

// ---------------------------------------------------------------------------
// Character relationships
// ---------------------------------------------------------------------------

// Upsert a relationship description for a character with a named target.
void DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                           const std::string& relationshipText);

// Delete all relationship rows for a character (used by .chars reset).
void DB_DeleteRelationshipsForCharacter(uint64_t botGuid);

// Delete all relationship rows for every character (used by .chars reset @ALL).
void DB_DeleteAllRelationships();

// Update the relationship text for a specific (bot, target) pair.
void DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
                               const std::string& newText);

// Delete a single relationship row for a specific (bot, target) pair.
void DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName);

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

// Check whether the memories table has any rows.
// Must be called after the DB is available (i.e. on or after OnStartup).
bool DB_MemoriesTableEmpty();

// Check whether the legacy card additions table exists and has any rows.
bool DB_CardAdditionsTableNotEmpty();

// ---------------------------------------------------------------------------
// DB Loader functions (implementations in pbc_database.cpp)
// ---------------------------------------------------------------------------

// Load all chat history from DB into g_PBC_ChatHistory.
void PBC_LoadHistoryFromDB();

// Load all memories from DB into g_PBC_Memories.
void PBC_LoadMemoriesFromDB();

// Load all character data (roll chance modifiers) from DB into g_PBC_RollChanceModifiers.
void PBC_LoadCharacterDataFromDB();

// Load all relationships from DB into g_PBC_Relationships.
void PBC_LoadRelationshipsFromDB();

#endif // MOD_PBC_DATABASE_H
