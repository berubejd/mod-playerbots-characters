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

// Returns a single-line equipment description string, e.g.
//   "You have fine equipment made of leather. Your main weapons are a rare
//    dagger called Death's Sting and a dagger. Your ranged weapon is a rare
//    bow called Heartstriker."
//
// When only a main-hand weapon is equipped (or the weapon is two-handed),
// singular "weapon" is used:
//   "You have excellent equipment made of plate. Your main weapon is an epic
//    two-handed mace called Devastation."
//
// Weapon names are included only for rare+ items.  Sentences are omitted when
// the corresponding slot is empty (e.g. no off-hand, no ranged weapon).
// When bags are ≥40% full, a bag-space summary is appended, e.g.
//   "You have fine equipment made of leather. Your main weapon is a rare dagger
//    called Death's Sting. Your bags are almost full."
std::string PBC_BuildEquipmentStr(Player* bot);

#endif // MOD_PBC_EQUIPMENT_HELPERS_H
