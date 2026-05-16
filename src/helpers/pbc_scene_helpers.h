#ifndef MOD_PBC_SCENE_HELPERS_H
#define MOD_PBC_SCENE_HELPERS_H

#include <string>

class Player;

// ---------------------------------------------------------------------------
// Scene / location / time / status helpers  (main-thread only)
//
// All functions require a live Player* and must be called on the main thread.
// They read game-object state (area, map, auras, etc.) and produce
// human-readable strings for prompt substitution.
// ---------------------------------------------------------------------------

// Returns the place name for a player's current ground location,
// e.g. "Lion's Pride Inn (Goldshire, Elwynn Forest)" or just "Elwynn Forest".
std::string PBC_BuildPlaceName(Player* player);

// Returns the top-level zone name for a player's current location,
// e.g. "Elwynn Forest" or "Mulgore".
std::string PBC_BuildZoneName(Player* player);

// Returns the taxi destination name for a flying player, or empty string.
std::string PBC_BuildFlightDestination(Player* bot);

// Returns combat status string, e.g. "You are not currently in combat."
// or "You are currently fighting Onyxia."
std::string PBC_BuildCombatStatusStr(Player* bot);

// Returns the LOS entity list string, e.g. "You see John and Defias Bandit nearby."
std::string PBC_BuildLosStr(Player* bot);

// Returns the full scene description combining location, travel state,
// time of day, and optional weather.  e.g.
// "You are currently on foot in Goldshire (Elwynn Forest), it's afternoon."
std::string PBC_BuildSceneStr(Player* bot);

// Returns the character's combat role string, e.g. "tank", "melee DPS",
// "healer", "ranged DPS", or the class name for hybrids.
std::string PBC_RoleStr(Player* bot);

// Returns the group status string, e.g.
// "You are currently in a group led by John (Male Human Warrior) with the
//  following members: Jane (Female Night Elf Priest)."
std::string PBC_BuildGroupStatusStr(Player* bot);

#endif // MOD_PBC_SCENE_HELPERS_H
