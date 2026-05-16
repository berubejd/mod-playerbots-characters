#ifndef MOD_PBC_UTILS_H
#define MOD_PBC_UTILS_H

#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <random>

// ---------------------------------------------------------------------------
// mod-playerbots-characters shared utility functions
//
// Pure string/template helpers and RNG — no game-object dependencies.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Pointer sanity guard
//
// On a 64-bit system, valid heap/stack pointers are always well above the
// first 64 KiB of address space.  Values in that range are garbage that the
// playerbots module occasionally passes to hooks during bot packet processing.
// ---------------------------------------------------------------------------
#define PBC_PTR_VALID(p) (reinterpret_cast<uintptr_t>(p) > 0xFFFFu)

// ---------------------------------------------------------------------------
// Template substitution helpers
// ---------------------------------------------------------------------------

// Expand literal "\n" escape sequences in-place (config prompts use \n for newlines).
void PBC_ExpandNewlineEscapes(std::string& s);

// Replace all occurrences of {key} with value in s, in-place.
void PBC_ReplaceToken(std::string& s, const std::string& key, const std::string& value);

// Remove any remaining {token} patterns in s, replacing them with "" and
// logging a warning for each unique unknown token found.
void PBC_CleanUnknownTokens(std::string& s);

// ---------------------------------------------------------------------------
// Debug output helpers
// ---------------------------------------------------------------------------

// Truncate a long string for debug logging: if > maxLen symbols, return
// first headLen ... last tailLen.  Otherwise return the string unchanged.
std::string PBC_TruncateForDebug(const std::string& s, size_t maxLen = 1000, size_t headLen = 100, size_t tailLen = 900);

// Replace curly braces with parentheses so the string is safe to pass
// through AzerothCore's fmt-based LOG macros (which double-format).
std::string PBC_SanitizeForFmt(const std::string& s);

// ---------------------------------------------------------------------------
// Chat / message helpers
// ---------------------------------------------------------------------------

// Strip WoW item/object link markup, keeping only the display name.
std::string PBC_SanitizeChatMessage(const std::string& msg);

// Case-insensitive check: does msg contain charName?
bool PBC_MentionsCharacter(const std::string& msg, const std::string& charName);

// ---------------------------------------------------------------------------
// Group helpers (require Player* — implementation in pbc_utils.cpp)
// ---------------------------------------------------------------------------

class Player;

// Returns true if bot is in a group that contains at least one real (non-bot) player.
bool PBC_BotIsGroupedWithRealPlayer(Player* bot);

// ---------------------------------------------------------------------------
// String formatting helpers
// ---------------------------------------------------------------------------

// Build a natural-language list: "You see X, Y and Z nearby." or "" if empty.
std::string PBC_NaturalList(const std::vector<std::string>& items);

// Default relationship text for an unknown target.
std::string PBC_DefaultRelationshipText(const std::string& name);

// ---------------------------------------------------------------------------
// Enum-to-string helpers (require SharedDefines.h in implementation)
// ---------------------------------------------------------------------------

// Convert class ID to human-readable name (e.g. CLASS_WARRIOR → "Warrior").
std::string PBC_ClassStr(uint8_t cls);

// Convert race ID to human-readable name (e.g. RACE_HUMAN → "Human").
std::string PBC_RaceStr(uint8_t race);

// Convert gender ID to human-readable name (GENDER_FEMALE → "female", else "male").
std::string PBC_GenderStr(uint8_t gender);

// ---------------------------------------------------------------------------
// Mention counting
// ---------------------------------------------------------------------------

// Count total occurrences of name in history lines (deque<string>).
uint32_t PBC_CountMentions(const std::deque<std::string>& history, const std::string& name);

// ---------------------------------------------------------------------------
// RNG
// ---------------------------------------------------------------------------

// Thread-local Mersenne Twister — seeded from multiple entropy sources.
std::mt19937& PBC_GetRNG();

// Roll 0-99 and return true if result < chance.
bool PBC_RollChance(uint32_t chance);

#endif // MOD_PBC_UTILS_H
