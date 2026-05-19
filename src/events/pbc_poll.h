#ifndef MOD_PBC_POLL_H
#define MOD_PBC_POLL_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <ctime>

class Player;
class Creature;

// ---------------------------------------------------------------------------
// Party state tracking for location/flight events
// ---------------------------------------------------------------------------
struct PBC_PartyState
{
    bool   inFlight     = false;
    std::string location;              // Last confirmed shared location
    std::string candidateLocation;     // New zone being debounced
    uint32_t locationStableCycles = 0;
};

extern std::unordered_map<uint32_t, PBC_PartyState> g_PBC_PartyStates;
extern std::mutex g_PBC_PartyStateMutex;

// ---------------------------------------------------------------------------
// Per-group combat session tracking
// ---------------------------------------------------------------------------
struct PBC_GroupCombatTracker
{
    bool wasInCombat = false;
    bool wiped = false;
    time_t combatStartTime = 0;
    uint32_t killCount = 0;
    std::map<std::string, uint32_t> killedEnemies;     // name -> count
    std::vector<std::string> notableEnemyNames;         // bosses/rares
    uint32_t deadCount = 0;
    uint32_t partySize = 0;
    uint32_t combatEndCycles = 0;
};

extern std::unordered_map<uint32_t, PBC_GroupCombatTracker> g_PBC_GroupCombatTrackers;

// ---------------------------------------------------------------------------
// Track a creature kill into the group's combat session tracker.
// Accumulates kill data (counts, notable names, HP minimums) for the
// combat-ended event.  No-op if the killer is not in a group or the
// group doesn't contain at least one real player and one bot.
// ---------------------------------------------------------------------------
void PBC_TrackGroupKill(Player* killer, Creature* killed);

// ---------------------------------------------------------------------------
// Poll party flight/location/combat state (called from OnUpdate every 1 second).
// Checks all groups with at least one real player and one bot, dispatches
// events for flight starts, location changes (debounced 5 cycles), and
// combat endings (debounced 5 cycles).
// ---------------------------------------------------------------------------
void PBC_PollPartyState();

#endif // MOD_PBC_POLL_H
