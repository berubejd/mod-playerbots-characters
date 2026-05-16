#include "pbc_group_helpers.h"
#include "pbc_utils.h"
#include "Player.h"
#include "Group.h"
#include "WorldSession.h"
#include "ObjectAccessor.h"
#include "GridNotifiers.h"
#include "CellImpl.h"

// ---------------------------------------------------------------------------
// PBC_FindGroupBots
// ---------------------------------------------------------------------------

std::vector<Player*> PBC_FindGroupBots(Player* player)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(player)) return bots;

    Group* grp = player->GetGroup();
    if (!grp) return bots;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || member == player) continue;
        if (!member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) continue;
        bots.push_back(member);
    }
    return bots;
}

// ---------------------------------------------------------------------------
// PBC_FindGroupBotsExcluding
// ---------------------------------------------------------------------------

std::vector<Player*> PBC_FindGroupBotsExcluding(Player* player,
    const std::unordered_set<uint64_t>& excludedGuids)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(player)) return bots;

    Group* grp = player->GetGroup();
    if (!grp) return bots;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || member == player) continue;
        if (!member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) continue;
        if (excludedGuids.count(member->GetGUID().GetCounter())) continue;
        bots.push_back(member);
    }
    return bots;
}

// ---------------------------------------------------------------------------
// PBC_FindNearbyBots
// ---------------------------------------------------------------------------

std::vector<Player*> PBC_FindNearbyBots(Player* source, float range)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(source) || !source->GetMap()) return bots;

    auto doWork = [&](Player* p)
    {
        if (!PBC_PTR_VALID(p) || p == source) return;
        if (!p->IsInWorld()) return;
        if (!p->GetSession() || !p->GetSession()->IsBot()) return;
        if (p->IsWithinDist(source, range))
            bots.push_back(p);
    };
    Acore::PlayerDistWorker<decltype(doWork)> worker(source, range, doWork);
    Cell::VisitObjects(source, worker, range);
    return bots;
}
