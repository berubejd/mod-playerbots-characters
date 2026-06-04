#ifndef MOD_PBC_QUEST_HELPERS_H
#define MOD_PBC_QUEST_HELPERS_H

#include <string>
#include "Define.h"

class Player;
class Quest;

// ---------------------------------------------------------------------------
// Quest-related helper functions
//
// Text sanitisation, NPC/GO name lookups, prompt substitution, and guard
// checks used by the quest-taken and quest-completed event handlers.
// ---------------------------------------------------------------------------

// Strip WoW quest text formatting codes ($b, $N, $G x:y;, etc.).
std::string PBC_StripWowTextCodes(const std::string& text);

// Look up quest starter/ender NPC and GO names from ObjectMgr relations.
// Returns a comma-separated list, or empty string if none found.
std::string PBC_GetQuestStarterNames(uint32 questId);
std::string PBC_GetQuestEnderNames(uint32 questId);

// Determine the source type ("person", "object", "person or object", or "")
// for quest starters/enders from ObjectMgr relations.
std::string PBC_GetQuestStarterType(uint32 questId);
std::string PBC_GetQuestEnderType(uint32 questId);

// Substitute quest-related placeholders in a prompt template string.
std::string PBC_SubstituteQuestVars(const std::string& tmpl,
                                     const std::string& title,
                                     const std::string& description,
                                     const std::string& logDescription,
                                     const std::string& completionLog,
                                     const std::string& rewardText,
                                     const std::string& questGiver,
                                     const std::string& questEnder,
                                     const std::string& questGiverType,
                                     const std::string& questEnderType);

// Common guard checks for quest events.
// Returns true if the event should proceed (module enabled, player valid,
// player is group leader, and at least one real player is in the group).
bool PBC_QuestEventGuard(Player* player);

// Validate that a quest has enough meaningful data to warrant an LLM event.
// Requires a non-empty title and description, plus at least one of giver or
// ender name.  PvP rank quests and other placeholder quests with no real
// quest data are filtered out.
bool PBC_IsQuestValidForEvent(Quest const* quest);

#endif // MOD_PBC_QUEST_HELPERS_H
