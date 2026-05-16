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

// Returns equipment description string, e.g.
// "You have fine equipment made of leather, and wield two rare daggers, called
//  Death's Sting and Deathstriker."
// When bags are ≥40% full, a bag-space summary is appended, e.g.
// "You have fine equipment made of leather, and wield two rare daggers, called
//  Death's Sting and Deathstriker. Your bags are almost full."
std::string PBC_BuildEquipmentStr(Player* bot);

#endif // MOD_PBC_EQUIPMENT_HELPERS_H
