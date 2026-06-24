#ifndef MOD_PBC_DATABASE_H
#define MOD_PBC_DATABASE_H

#include <string>
#include <vector>
#include <cstdint>

struct PBC_CardEntry;

// ---------------------------------------------------------------------------
// Chat history — normalized schema (mod_pbc_history + mod_pbc_history_owners)
// ---------------------------------------------------------------------------

// Insert one message into mod_pbc_history and link it to one or more owners.
// Returns the new history_id (auto-increment).  The trailing attribution
// arguments are optional (stamped at event ingestion); 0/"" store SQL NULL.
uint64_t DB_InsertHistoryMessage(uint64_t authorGuid, uint8_t type,
                                 const std::string& message,
                                 const std::vector<uint64_t>& ownerGuids,
                                 uint64_t subjectGuid = 0,
                                 const std::string& eventType = "",
                                 const std::string& mood = "");

// Update the attribution columns of an existing history row (mood refine).
void DB_UpdateHistoryMood(uint64_t historyId, const std::string& mood);

// Update the raw message text in mod_pbc_history (affects all owners).
void DB_UpdateHistoryMessage(uint64_t historyId, const std::string& newMessage);

// Remove one character's ownership of a message.
// If removeOrphaned is true and no owners remain, also delete the message.
void DB_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId,
                               bool removeOrphaned = true);

// Remove all ownership rows for a character, then clean orphaned messages.
// Used by condensation and .chars reset.
void DB_RemoveAllHistoryOwnership(uint64_t guid);

// ---------------------------------------------------------------------------
// Character memories
// ---------------------------------------------------------------------------

// Insert a single memory for a character.  The trailing attribution arguments
// are propagated from the source history window at condensation time.
void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance,
                     uint64_t subjectGuid = 0,
                     const std::string& type = "general",
                     const std::string& mood = "");

// Delete all memories for a character (used by .chars reset).
void DB_DeleteMemoriesForCharacter(uint64_t botGuid);

// Delete all memories for every character (used by .chars reset @ALL).
void DB_DeleteAllMemories();

// Update a single memory by DB row id.
void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance);

// Mark a memory as surfaced: used = 1, last_used_at = NOW(). Lifecycle
// bookkeeping so recently-surfaced memories rotate out of selection.
void DB_MarkMemoryUsed(uint64_t memoryId);

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
// Character cards (mod_pbc_cards)
// ---------------------------------------------------------------------------

// Idempotent upsert of a card row (INSERT ... ON DUPLICATE KEY UPDATE).
// Writes every persona field plus provenance/pinned/hash/gen metadata.
void DB_UpsertCard(const PBC_CardEntry& card);

// Set/clear the pinned flag for a single card row.
void DB_SetCardPinned(uint64_t botGuid, bool pinned);

// Load a single card row by GUID into `out`. Returns false if no row exists.
// Authoritative read straight from mod_pbc_cards (does not consult the cache).
bool DB_LoadCard(uint64_t botGuid, PBC_CardEntry& out);

// Authoritative read of every card row (does not consult the cache). Used by
// the viewer browse list so direct-SQL rows not yet hydrated still appear.
std::vector<PBC_CardEntry> DB_LoadAllCards();

// Return up to `limit` recent generated-card premise lines (most recent first),
// used as the anti-collision block fed to generation so a batch of new cards
// doesn't converge on the same origins/hooks.
std::vector<std::string> DB_GetRecentGeneratedSummaries(size_t limit);

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

// Returns true if `column` exists on `table` in the current database.
// Used by loaders/importers to detect a not-yet-applied schema migration so
// they can fail loudly instead of silently wiping in-memory state.
bool DB_TableHasColumn(const std::string& table, const std::string& column);

// Check whether the memories table has any rows.
// Must be called after the DB is available (i.e. on or after OnStartup).
bool DB_MemoriesTableEmpty();

// Check whether the legacy card additions table exists and has any rows.
bool DB_CardAdditionsTableNotEmpty();

// ---------------------------------------------------------------------------
// DB Loader functions (implementations in pbc_database.cpp)
// ---------------------------------------------------------------------------

// Load all chat history from DB into g_PBC_History + g_PBC_HistoryOwners.
void PBC_LoadHistoryFromDB();

// Load all memories from DB into g_PBC_Memories.
void PBC_LoadMemoriesFromDB();

// Load all character data (roll chance modifiers) from DB into g_PBC_RollChanceModifiers.
void PBC_LoadCharacterDataFromDB();

// Load all relationships from DB into g_PBC_Relationships.
void PBC_LoadRelationshipsFromDB();

// Load all character cards from DB into g_PBC_Cards.
void PBC_LoadCardsFromDB();

#endif // MOD_PBC_DATABASE_H
