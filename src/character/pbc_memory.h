#ifndef MOD_PBC_MEMORY_H
#define MOD_PBC_MEMORY_H

#include <string>

// ---------------------------------------------------------------------------
// Event category taxonomy.
//
// Stamped onto short-term history rows (mod_pbc_history.event_type) at event
// ingestion, and propagated to long-term memories (mod_pbc_memories.type) at
// condensation.  VARCHAR(32) values — plain strings, no ENUM (so adding a
// category never requires a schema migration).
// ---------------------------------------------------------------------------
namespace PBC_Cat
{
    inline constexpr const char* Chat           = "chat";
    inline constexpr const char* Item           = "item";
    inline constexpr const char* Duel           = "duel";
    inline constexpr const char* LevelUp        = "levelup";
    inline constexpr const char* Combat         = "combat";
    inline constexpr const char* QuestTaken     = "quest_taken";
    inline constexpr const char* QuestCompleted = "quest_completed";
    inline constexpr const char* Location       = "location";
    inline constexpr const char* General        = "general";
}

// ---------------------------------------------------------------------------
// Model-free mood derived from an event category.  Returns a short mood word,
// or "" for neutral categories (chat/location/general).  Used at ingestion and
// as the fallback whenever the optional AI mood pass is disabled.
// ---------------------------------------------------------------------------
std::string PBC_MoodFromCategory(const std::string& category);

struct PBC_EventItem;

// ---------------------------------------------------------------------------
// Process a MoodRefine job (runs on the background worker): asks the model for
// a one-word mood for the event and updates the history row's mood in place.
// No-op when the mood prompts are not configured.
// ---------------------------------------------------------------------------
void PBC_ProcessMoodRefine(PBC_EventItem& ev);

#endif // MOD_PBC_MEMORY_H
