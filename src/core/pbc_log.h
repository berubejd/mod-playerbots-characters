#ifndef MOD_PBC_LOG_H
#define MOD_PBC_LOG_H

#include <string>
#include <fmt/core.h>
#include "Log.h"

// ---------------------------------------------------------------------------
// Forward declaration — defined in pbc_config.cpp
// ---------------------------------------------------------------------------
extern bool g_PBC_DebugEnabled;

// ---------------------------------------------------------------------------
// PBC logging levels
// ---------------------------------------------------------------------------
enum class PBC_LogLevel
{
    PBC_DEFAULT,   // Startup, event triggers — no level tag in output
    PBC_DEBUG,     // Detailed event tracking, LLM requests — skipped when PBC.DebugEnabled = 0
    PBC_WARNING,   // Weird situations, migration prompts
    PBC_ERROR      // Errors
};

// ---------------------------------------------------------------------------
// PBC_Log — unified logging helper
//
// Format:  [PBC] [LEVEL] message
//          (the [LEVEL] part is omitted for DEFAULT)
//
// - DEBUG messages are silently skipped when g_PBC_DebugEnabled is false.
// - WARNING uses LOG_WARN (yellow in console).
// - ERROR   uses LOG_ERROR (red in console).
// - DEFAULT and DEBUG use LOG_INFO.
//
// Usage:   PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM reply ({} tokens): {}", tokens, text);
// ---------------------------------------------------------------------------
template<typename... Args>
void PBC_Log(PBC_LogLevel level, const char* fmt, Args&&... args)
{
    if (level == PBC_LogLevel::PBC_DEBUG && !g_PBC_DebugEnabled)
        return;

    std::string message = fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...);

    switch (level)
    {
        case PBC_LogLevel::PBC_DEFAULT:
            LOG_INFO("server.loading", "[PBC] {}", message);
            break;
        case PBC_LogLevel::PBC_DEBUG:
            LOG_INFO("server.loading", "[PBC] [DEBUG] {}", message);
            break;
        case PBC_LogLevel::PBC_WARNING:
            LOG_WARN("server.loading", "[PBC] [WARNING] {}", message);
            break;
        case PBC_LogLevel::PBC_ERROR:
            LOG_ERROR("server.loading", "[PBC] [ERROR] {}", message);
            break;
    }
}

#endif // MOD_PBC_LOG_H
