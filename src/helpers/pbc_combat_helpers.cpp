#include "pbc_combat_helpers.h"
#include "pbc_config.h"
#include "pbc_utils.h"
#include "Log.h"
#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "WorldSession.h"
#include "GameTime.h"

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

// Returns true for dungeon/raid bosses, world bosses, and rare-elite or
// higher-ranked creatures (unique named rares such as King Mosh).
// Plain ELITE rank (rank 1) is intentionally excluded — there are many
// open-world elite creatures (e.g. Plated Stegodon, Devilsaurs) that spawn
// in packs and are not meaningful story events.
static bool IsSignificantKill(const Creature* killed)
{
    if (!PBC_PTR_VALID(killed)) return false;
    if (killed->IsDungeonBoss() || killed->isWorldBoss()) return true;
    uint32 rank = killed->GetCreatureTemplate()->rank;
    return rank >= CREATURE_ELITE_RAREELITE;
}

// Produces a display string for the killed creature, including its subtitle
// if it has one: "Kel'Thuzad (The Lich's Champion)" or just "Murloc Raider".
static std::string BuildBossLabel(const Creature* killed)
{
    std::string label = killed->GetName();
    const std::string& sub = killed->GetCreatureTemplate()->SubName;
    if (!sub.empty())
        label += " (" + sub + ")";
    return label;
}

// ---------------------------------------------------------------------------
// PBC_TrackGroupKill
// ---------------------------------------------------------------------------

void PBC_TrackGroupKill(Player* killer, Creature* killed)
{
    if (!PBC_PTR_VALID(killer) || !PBC_PTR_VALID(killed)) return;

    Group* grp = killer->GetGroup();
    if (!grp) return;

    // Only track groups with at least one real player and one bot.
    bool hasReal = false, hasBot = false;
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->GetSource();
        if (!m || !m->IsInWorld()) continue;
        WorldSession* ms = m->GetSession();
        if (!PBC_PTR_VALID(ms)) continue;
        if (ms->IsBot()) hasBot = true; else hasReal = true;
    }
    if (!hasReal || !hasBot) return;

    uint32_t grpGuid = grp->GetGUID().GetCounter();

    std::lock_guard<std::mutex> lock(g_PBC_PartyStateMutex);
    PBC_GroupCombatTracker& tracker = g_PBC_GroupCombatTrackers[grpGuid];

    // If combat hasn't started yet (this can happen if the kill happens
    // before the poll detects combat), start the session now.
    if (!tracker.wasInCombat)
    {
        tracker.wasInCombat = true;
        tracker.combatStartTime = GameTime::GetGameTime().count();
        tracker.partySize = grp->GetMembersCount();
    }

    ++tracker.killCount;

    if (IsSignificantKill(killed))
    {
        tracker.notableEnemyNames.push_back(BuildBossLabel(killed));
    }
    else
    {
        // Track ordinary enemy names with counts for the slain list
        std::string name = killed->GetName();
        ++tracker.killedEnemies[name];
    }
}
