#ifndef MOD_PBC_CHARACTER_H
#define MOD_PBC_CHARACTER_H

#include <string>
#include "Player.h"
#include "pbc_config.h"

// ---------------------------------------------------------------------------
// Snapshot builder (main-thread only)
//
// Captures all data needed from a live Player* into a PBC_BotSnapshot.
// Must be called on the main thread.  The resulting struct is safe to pass
// to the event thread without further access to game objects.
// ---------------------------------------------------------------------------
PBC_BotSnapshot PBC_SnapshotBot(Player* bot);

// ---------------------------------------------------------------------------
// Prompt builder (thread-safe, uses snapshot only)
//
// Builds the fully-substituted user prompt string for a bot using only data
// stored in its snapshot (including its local history copy).  Safe to call
// from any thread.
// ---------------------------------------------------------------------------
std::string PBC_BuildUserPromptFromSnapshot(const PBC_BotSnapshot& snap,
                                             const std::string& eventLine);

// ---------------------------------------------------------------------------
// Condensation prompt builder (thread-safe, uses snapshot only)
// ---------------------------------------------------------------------------
std::string PBC_BuildCondensationPromptFromSnapshot(const PBC_BotSnapshot& snap,
                                                     const std::string& tmpl);

// ---------------------------------------------------------------------------
// Global history access (thread-safe)
// ---------------------------------------------------------------------------

std::string PBC_GetChatHistory(uint64_t botGuid);
void        PBC_AppendHistory(uint64_t botGuid, const std::string& line);
int         PBC_EstimateHistoryTokens(uint64_t botGuid);

// ---------------------------------------------------------------------------
// Character card / context (main-thread only)
// ---------------------------------------------------------------------------
std::string PBC_GetCharacterCard(Player* bot);
std::string PBC_GetCharacterContext(Player* bot);

// ---------------------------------------------------------------------------
// Full variable substitution (main-thread only)
//
// expandComposites=false skips {character_card}, {chat_history}, {context}
// to prevent infinite recursion when called from card/context builders.
// ---------------------------------------------------------------------------
std::string PBC_SubstituteVars(const std::string& tmpl, Player* bot,
                                const std::string& event = "",
                                bool expandComposites = true);

// ---------------------------------------------------------------------------
// Push a Condensation event for a bot onto the global event queue.
// May be called from the main thread (commands).
// ---------------------------------------------------------------------------
void PBC_TriggerCondensation(Player* bot);

// ---------------------------------------------------------------------------
// Build the relationships block for a bot's prompt (thread-safe).
// Returns a multi-line string (one entry per party member) to be substituted
// into {relationships} in the user prompt template.
// ---------------------------------------------------------------------------
std::string PBC_GetRelationshipsBlock(const PBC_BotSnapshot& snap);

#endif // MOD_PBC_CHARACTER_H
