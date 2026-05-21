#ifndef MOD_PBC_GROUP_SCRIPTS_H
#define MOD_PBC_GROUP_SCRIPTS_H

#include "ScriptMgr.h"

class Group;
class Player;
class ObjectGuid;

// ---------------------------------------------------------------------------
// Listens to group events and sends "party" WS notifications so the frontend
// can refresh its account/party data.
// ---------------------------------------------------------------------------
class PBC_GroupEvents : public GroupScript
{
public:
    PBC_GroupEvents();

    void OnAddMember(Group* group, ObjectGuid guid) override;
    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod method, ObjectGuid kicker, const char* reason) override;
    void OnDisband(Group* group) override;
};

#endif // MOD_PBC_GROUP_SCRIPTS_H
