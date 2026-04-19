#ifndef MOD_PBC_COMMANDS_H
#define MOD_PBC_COMMANDS_H

#include "ScriptMgr.h"
#include "CommandScript.h"

class PBC_CommandScript : public CommandScript
{
public:
    PBC_CommandScript();
    Acore::ChatCommands::ChatCommandTable GetCommands() const override;
};

#endif // MOD_PBC_COMMANDS_H
