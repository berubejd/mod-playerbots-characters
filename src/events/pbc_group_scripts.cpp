#include "pbc_group_scripts.h"
#include "pbc_config.h"
#include "pbc_http.h"
#include "pbc_log.h"

#include "CharacterCache.h"
#include "Group.h"
#include "ObjectGuid.h"
#include "Player.h"

#include <unordered_set>

// ---------------------------------------------------------------------------
// Helper: send "party" WS notification to every unique account in a group.
// ---------------------------------------------------------------------------
static void NotifyGroupAccounts(Group* group)
{
    if (!group) return;

    std::unordered_set<uint32_t> accountIds;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld())
            continue;

        uint64_t guid = member->GetGUID().GetCounter();
        uint32_t accountId = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(guid));
        if (accountId != 0)
            accountIds.insert(accountId);
    }

    for (uint32_t accountId : accountIds)
        PBC_WsNotifyAccount(accountId, "party");

    if (!accountIds.empty())
        PBC_Log(PBC_LogLevel::DEBUG, "WS: party event sent to {} account(s)", accountIds.size());
}

// ---------------------------------------------------------------------------
// PBC_GroupEvents
// ---------------------------------------------------------------------------

PBC_GroupEvents::PBC_GroupEvents() : GroupScript("PBC_GroupEvents",
{
    GROUPHOOK_ON_ADD_MEMBER,
    GROUPHOOK_ON_REMOVE_MEMBER,
    GROUPHOOK_ON_DISBAND,
}) {}

void PBC_GroupEvents::OnAddMember(Group* group, ObjectGuid /*guid*/)
{
    if (!g_PBC_Enable) return;
    NotifyGroupAccounts(group);
}

void PBC_GroupEvents::OnRemoveMember(Group* group, ObjectGuid /*guid*/, RemoveMethod /*method*/, ObjectGuid /*kicker*/, const char* /*reason*/)
{
    if (!g_PBC_Enable) return;
    NotifyGroupAccounts(group);
}

void PBC_GroupEvents::OnDisband(Group* group)
{
    if (!g_PBC_Enable) return;
    NotifyGroupAccounts(group);
}
