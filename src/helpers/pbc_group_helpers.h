#ifndef MOD_PBC_GROUP_HELPERS_H
#define MOD_PBC_GROUP_HELPERS_H

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

class Player;

// ---------------------------------------------------------------------------
// Group / bot-finding helpers  (main-thread only)
//
// All functions require live Player* objects and must be called on the main
// thread.  They walk group membership lists and return vectors of Player*
// for event dispatch and roll logic.
// ---------------------------------------------------------------------------

// Find all bot players in the same group as 'player' (excluding 'player'
// itself).  Returns empty vector if the player is not in a group.
std::vector<Player*> PBC_FindGroupBots(Player* player);

// Find all real (non-bot) players in the same group as 'player' (excluding
// 'player' itself).  Returns empty vector if the player is not in a group.
std::vector<Player*> PBC_FindRealPlayersInGroup(Player* player);

// Find all bot players in the same group as 'player', excluding 'player'
// itself AND any GUIDs in the excluded set.  Returns empty vector if the
// player is not in a group.
std::vector<Player*> PBC_FindGroupBotsExcluding(Player* player,
    const std::unordered_set<uint64_t>& excludedGuids);

// Find nearby bot players around a WorldObject source within the given range.
// Used for say/yell chat events where proximity matters rather than group.
std::vector<Player*> PBC_FindNearbyBots(Player* source, float range = 60.0f);

// Returns true if bot is in a group that contains at least one real (non-bot) player.
bool PBC_BotIsGroupedWithRealPlayer(Player* bot);

#endif // MOD_PBC_GROUP_HELPERS_H
