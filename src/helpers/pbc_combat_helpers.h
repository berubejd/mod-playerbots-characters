#ifndef MOD_PBC_COMBAT_HELPERS_H
#define MOD_PBC_COMBAT_HELPERS_H

class Player;
class Creature;

// ---------------------------------------------------------------------------
// Combat tracking helpers  (main-thread only)
//
// Functions for tracking creature kills into per-group combat session
// trackers.  Called from player event hooks before the significance filter.
// ---------------------------------------------------------------------------

// Track a creature kill into the group's combat session tracker.
// Accumulates kill data (counts, notable names, HP minimums) for the
// combat-ended event.  No-op if the killer is not in a group or the
// group doesn't contain at least one real player and one bot.
void PBC_TrackGroupKill(Player* killer, Creature* killed);

#endif // MOD_PBC_COMBAT_HELPERS_H
