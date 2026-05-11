#include "pbc_config.h"
#include "pbc_events.h"
#include "pbc_world.h"
#include "pbc_commands.h"
#include "Log.h"

void Addmod_playerbots_charactersScripts()
{
    LOG_INFO("server.loading", "[PBC] Registering mod-playerbots-characters scripts.");

    new PBC_WorldScript();
    new PBC_PlayerEvents();
    new PBC_AllCreatureQuestScript();
    new PBC_AllGameObjectQuestScript();
    new PBC_AllItemQuestScript();
    new PBC_CommandScript();
}
