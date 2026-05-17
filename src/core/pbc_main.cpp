#include "pbc_config.h"
#include "pbc_events.h"
#include "pbc_world.h"
#include "pbc_commands.h"
#include "pbc_log.h"

void Addmod_playerbots_charactersScripts()
{
    PBC_Log(PBC_LogLevel::DEFAULT, "Registering mod-playerbots-characters scripts.");

    new PBC_WorldScript();
    new PBC_PlayerEvents();
    new PBC_AllCreatureQuestScript();
    new PBC_AllGameObjectQuestScript();
    new PBC_AllItemQuestScript();
    new PBC_CommandScript();
}
