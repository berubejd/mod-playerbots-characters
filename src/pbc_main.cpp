#include "pbc_config.h"
#include "pbc_events.h"
#include "pbc_commands.h"
#include "Log.h"

void Addmod_playerbots_charactersScripts()
{
    LOG_INFO("server.loading", "[PBC] Registering mod-playerbots-characters scripts.");

    new PBC_WorldScript();
    new PBC_PlayerEvents();
    new PBC_CreatureQuestScript();
    new PBC_GameObjectQuestScript();
    new PBC_ItemQuestScript();
    new PBC_CommandScript();
}
