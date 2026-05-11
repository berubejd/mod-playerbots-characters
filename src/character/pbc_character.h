#ifndef MOD_PBC_CHARACTER_H
#define MOD_PBC_CHARACTER_H

#include <string>
#include "Player.h"
#include "pbc_config.h"

// ---------------------------------------------------------------------------
// Snapshot builder (main-thread only)
//
// Captures all data needed from a live Player* into a PBC_CharacterSnapshot.
// Must be called on the main thread.  The resulting struct is safe to pass
// to the event thread without further access to game objects.
// ---------------------------------------------------------------------------
PBC_CharacterSnapshot PBC_SnapshotCharacter(Player* bot);

// ---------------------------------------------------------------------------
// Prompt builder (thread-safe, uses snapshot only)
//
// Builds the fully-substituted user prompt string for a character using only data
// stored in its snapshot (including its local history copy).  Safe to call
// from any thread.
// ---------------------------------------------------------------------------
std::string PBC_BuildUserPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                             const std::string& eventLine);

// ---------------------------------------------------------------------------
// Condensation prompt builder (thread-safe, uses snapshot only)
// ---------------------------------------------------------------------------
std::string PBC_BuildCondensationPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                                     const std::string& tmpl);

// ---------------------------------------------------------------------------
// Global history access (thread-safe)
// ---------------------------------------------------------------------------

std::string PBC_GetChatHistory(uint64_t botGuid);
void        PBC_AppendHistory(uint64_t botGuid, const std::string& line);
int         PBC_EstimateHistoryTokens(uint64_t botGuid);

// ---------------------------------------------------------------------------
// Mutation result (thread-safe, also updates the database)
// ---------------------------------------------------------------------------
enum class PBC_HistoryResult { Ok, NotFound, Desync };

// ---------------------------------------------------------------------------
// History mutation (thread-safe, also updates the database)
//
// index is 0-based, matching the position returned by GET /api/history.
// originalMessage: the current message at the index is compared against this
// value before applying the mutation.  If they differ, Desync is returned
// and no modification is made.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateHistoryLine(uint64_t botGuid, size_t index,
                                        const std::string& newMessage,
                                        const std::string& originalMessage);
PBC_HistoryResult PBC_DeleteHistoryLine(uint64_t botGuid, size_t index,
                                        const std::string& originalMessage);

// ---------------------------------------------------------------------------
// Card addition mutation (thread-safe, also updates the database)
//
// index is 0-based (the API uses 1-based IDs; the HTTP handler converts).
// originalText: the current addition text at the index is compared against this
// value before applying the mutation.  If they differ, Desync is returned
// and no modification is made.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateCardAddition(uint64_t botGuid, size_t index,
                                          const std::string& newText,
                                          const std::string& originalText);
PBC_HistoryResult PBC_DeleteCardAddition(uint64_t botGuid, size_t index,
                                          const std::string& originalText);

// ---------------------------------------------------------------------------
// Relationship mutation (thread-safe, also updates the database)
//
// targetName identifies the relationship entry (the key in the map).
// originalText: the current relationship text is compared against this value
// before applying the mutation.  If they differ, Desync is returned and no
// modification is made.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& newText,
                                          const std::string& originalText);
PBC_HistoryResult PBC_DeleteRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& originalText);

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
//
// annotate=true replaces {key} with {key}value instead of just value,
// leaving the variable name visible in the output so the frontend can
// identify which variable produced each part.  Used only by the API
// context endpoint — LLM requests always use annotate=false.
// ---------------------------------------------------------------------------
std::string PBC_SubstituteVars(const std::string& tmpl, Player* bot,
                                const std::string& event = "",
                                bool expandComposites = true,
                                bool annotate = false);

// ---------------------------------------------------------------------------
// Push a Condensation event for a character onto the global event queue.
// May be called from the main thread (commands).
// ---------------------------------------------------------------------------
void PBC_TriggerCondensation(Player* bot);

// ---------------------------------------------------------------------------
// Build the relationships block for a character's prompt (thread-safe).
// Returns a multi-line string (one entry per party member) to be substituted
// into {relationships} in the user prompt template.
// ---------------------------------------------------------------------------
std::string PBC_GetRelationshipsBlock(const PBC_CharacterSnapshot& snap);

// ---------------------------------------------------------------------------
// Build a target info string for a named player, e.g. "DESEVEN, MALE TAUREN SHAMAN".
// Looks up the player live (safe for read-only access from any thread).
// Falls back to just the uppercased name if the player cannot be found.
// ---------------------------------------------------------------------------
std::string PBC_BuildTargetInfo(const std::string& name);

// ---------------------------------------------------------------------------
// Shared game-object-dependent helpers (main-thread only)
// ---------------------------------------------------------------------------

// Returns the place name for a player's current ground location,
// e.g. "Lion's Pride Inn (Goldshire, Elwynn Forest)" or "Goldshire (Elwynn Forest)" or just "Elwynn Forest".
std::string PBC_BuildPlaceName(Player* player);

// Returns the taxi destination name for a flying player, or empty string.
std::string PBC_BuildFlightDestination(Player* bot);

// Returns combat status string, e.g. "You are not currently in combat."
// or "You are currently fighting Onyxia."
std::string PBC_BuildCombatStatusStr(Player* bot);

// Returns the LOS entity list string, e.g. "You see John and Defias Bandit nearby."
std::string PBC_BuildLosStr(Player* bot);

// Returns equipment description string, e.g.
// "You have fine equipment made of leather, and wield two rare daggers, called Death's Sting and Deathstriker."
// When bags are ≥40% full, a bag-space summary is appended, e.g.
// "You have fine equipment made of leather, and wield two rare daggers, called Death's Sting and Deathstriker. Your bags are almost full."
std::string PBC_BuildEquipmentStr(Player* bot);

#endif // MOD_PBC_CHARACTER_H
