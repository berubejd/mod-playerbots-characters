#ifndef MOD_PBC_WORLD_H
#define MOD_PBC_WORLD_H

#include <cstdint>
#include "ScriptMgr.h"

// ---------------------------------------------------------------------------
// World script — startup, shutdown, and per-tick update loop.
// ---------------------------------------------------------------------------

class PBC_WorldScript : public WorldScript
{
public:
    PBC_WorldScript();
    void OnStartup() override;
    void OnShutdown() override;
    void OnUpdate(uint32_t diff) override;
};

#endif // MOD_PBC_WORLD_H
