#include "pbc_quest_helpers.h"
#include "pbc_utils.h"
#include "pbc_config.h"
#include "ObjectMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "Group.h"
#include "WorldSession.h"
#include "Log.h"

// ---------------------------------------------------------------------------
// Strip WoW quest text formatting codes.
// $b / $B  = line break
// $N / $n  = player name placeholder
// $R / $r  = player race placeholder
// $C / $c  = player class placeholder
// $G x:y;  = gender-conditional text
// ---------------------------------------------------------------------------
std::string PBC_StripWowTextCodes(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '$' && i + 1 < text.size())
        {
            char next = text[i + 1];
            // $b / $B -> newline
            if (next == 'b' || next == 'B')
            {
                result += '\n';
                ++i;
                continue;
            }
            // $N / $n / $R / $r / $C / $c -> skip placeholder
            if (next == 'N' || next == 'n' || next == 'R' || next == 'r' ||
                next == 'C' || next == 'c')
            {
                ++i;
                continue;
            }
            // $G x:y; -> skip entire conditional
            if (next == 'G' || next == 'g')
            {
                i += 2; // skip '$G'
                while (i < text.size() && text[i] != ';')
                    ++i;
                continue;
            }
        }
        result += text[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Look up quest starter/ender NPC names from ObjectMgr relations.
// Returns a comma-separated list of creature/GO names for the given quest ID.
// ---------------------------------------------------------------------------

std::string PBC_GetQuestStarterNames(uint32 questId)
{
    std::string result;
    auto* relMap = sObjectMgr->GetCreatureQuestRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(it->first);
            if (cInfo)
            {
                if (!result.empty()) result += ", ";
                result += cInfo->Name;
            }
        }
    }
    // Also check gameobject starters
    auto* goRelMap = sObjectMgr->GetGOQuestRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(it->first);
            if (goInfo)
            {
                if (!result.empty()) result += ", ";
                result += goInfo->name;
            }
        }
    }
    return result;
}

std::string PBC_GetQuestEnderNames(uint32 questId)
{
    std::string result;
    auto* relMap = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(it->first);
            if (cInfo)
            {
                if (!result.empty()) result += ", ";
                result += cInfo->Name;
            }
        }
    }
    // Also check gameobject enders
    auto* goRelMap = sObjectMgr->GetGOQuestInvolvedRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(it->first);
            if (goInfo)
            {
                if (!result.empty()) result += ", ";
                result += goInfo->name;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Determine the source type of quest starters/enders from ObjectMgr relations.
// Returns "person" if only creatures, "object" if only gameobjects,
// "person or object" if both, or "" if neither.
// ---------------------------------------------------------------------------

std::string PBC_GetQuestStarterType(uint32 questId)
{
    bool hasCreature = false, hasGO = false;
    auto* relMap = sObjectMgr->GetCreatureQuestRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId) { hasCreature = true; break; }
    }
    auto* goRelMap = sObjectMgr->GetGOQuestRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId) { hasGO = true; break; }
    }
    if (hasCreature && hasGO) return "person or object";
    if (hasCreature) return "person";
    if (hasGO) return "object";
    return "";
}

std::string PBC_GetQuestEnderType(uint32 questId)
{
    bool hasCreature = false, hasGO = false;
    auto* relMap = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId) { hasCreature = true; break; }
    }
    auto* goRelMap = sObjectMgr->GetGOQuestInvolvedRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId) { hasGO = true; break; }
    }
    if (hasCreature && hasGO) return "person or object";
    if (hasCreature) return "person";
    if (hasGO) return "object";
    return "";
}

// ---------------------------------------------------------------------------
// String substitution for quest prompt placeholders.
// ---------------------------------------------------------------------------
std::string PBC_SubstituteQuestVars(const std::string& tmpl,
                                     const std::string& title,
                                     const std::string& description,
                                     const std::string& logDescription,
                                     const std::string& completionLog,
                                     const std::string& rewardText,
                                     const std::string& questGiver,
                                     const std::string& questEnder,
                                     const std::string& questGiverType,
                                     const std::string& questEnderType)
{
    std::string result = tmpl;
    PBC_ReplaceToken(result, "quest_title",          title);
    PBC_ReplaceToken(result, "quest_description",    description);
    PBC_ReplaceToken(result, "quest_log_description", logDescription);
    PBC_ReplaceToken(result, "quest_completion_log",  completionLog);
    PBC_ReplaceToken(result, "quest_reward_text",    rewardText);
    PBC_ReplaceToken(result, "quest_giver",          questGiver);
    PBC_ReplaceToken(result, "quest_ender",          questEnder);
    PBC_ReplaceToken(result, "quest_giver_type",     questGiverType);
    PBC_ReplaceToken(result, "quest_ender_type",     questEnderType);
    return result;
}

// ---------------------------------------------------------------------------
// Common guard checks for quest events.
// Returns true if the event should proceed.
// ---------------------------------------------------------------------------
bool PBC_QuestEventGuard(Player* player)
{
    if (!g_PBC_Enable) return false;
    if (!PBC_PTR_VALID(player)) return false;

    Group* grp = player->GetGroup();
    if (!grp) return false;
    if (grp->GetLeaderGUID() != player->GetGUID()) return false;

    WorldSession* sess = player->GetSession();
    bool leaderIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    if (!leaderIsReal && !PBC_BotIsGroupedWithRealPlayer(player)) return false;

    return true;
}
