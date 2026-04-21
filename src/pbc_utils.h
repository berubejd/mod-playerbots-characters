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
// Template substitution helpers
// ---------------------------------------------------------------------------

// Expand literal "\n" escape sequences in-place (config prompts use \n for newlines).
void PBC_ExpandNewlineEscapes(std::string& s);

// Replace all occurrences of {key} with value in s, in-place.
void PBC_ReplaceToken(std::string& s, const std::string& key, const std::string& value);

// ---------------------------------------------------------------------------
// Debug output helpers
// ---------------------------------------------------------------------------

// Truncate a long string for debug logging: if > maxLen symbols, return
// first halfLen ... last halfLen.  Otherwise return the string unchanged.
std::string PBC_TruncateForDebug(const std::string& s, size_t maxLen = 1000, size_t halfLen = 500);

// ---------------------------------------------------------------------------
// String formatting helpers
// ---------------------------------------------------------------------------

// Build a natural-language list: "You see X, Y and Z nearby." or "" if empty.
std::string PBC_NaturalList(const std::vector<std::string>& items);

// Default relationship text for an unknown target.
std::string PBC_DefaultRelationshipText(const std::string& name);

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
