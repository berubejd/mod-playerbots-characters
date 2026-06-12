#ifndef MOD_PBC_EQUIPMENT_HELPERS_H
#define MOD_PBC_EQUIPMENT_HELPERS_H

#include <string>

class Player;

// ---------------------------------------------------------------------------
// Equipment description helpers  (main-thread only)
//
// All functions require a live Player* and must be called on the main thread.
// They inspect equipped items and produce human-readable equipment descriptions
// for prompt substitution.
// ---------------------------------------------------------------------------

// Returns a multi-line equipment description string, e.g.
//   "You have fine equipment made of leather.\n"
//   "Your main weapon is a rare dagger called Death's Sting.\n"
//   "In your off-hand you wield a common dagger.\n"
//   "Your ranged weapon is a rare bow called Heartstriker."
//
// Weapon names are included only for rare+ items.  Lines are omitted when
// the corresponding slot is empty (e.g. no off-hand, no ranged weapon).
// When bags are ≥40% full, a bag-space summary is appended, e.g.
//   "You have fine equipment made of leather.\n"
//   "Your main weapon is a rare dagger called Death's Sting. Your bags are almost full."
std::string PBC_BuildEquipmentStr(Player* bot);

#endif // MOD_PBC_EQUIPMENT_HELPERS_H
