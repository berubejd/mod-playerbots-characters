#include "pbc_quest_scripts.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_utils.h"
#include "pbc_quest_helpers.h"
#include "pbc_group_helpers.h"
#include "pbc_event_dispatch.h"
#include "pbc_log.h"

#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "QuestDef.h"
#include "ObjectMgr.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// HandleQuestTaken — internal handler shared by all three script types.
// ---------------------------------------------------------------------------
static void HandleQuestTaken(Player* player, Quest const* quest,
                              std::string const& questGiver,
                              std::string const& questGiverType)
{
    if (!PBC_QuestEventGuard(player) || !quest) return;

    if (g_PBC_QuestTakenSystemPrompt.empty() || g_PBC_QuestTakenUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_LOG_LEVEL_WARNING, "HandleQuestTaken: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = PBC_StripWowTextCodes(quest->GetTitle());
    std::string questDescription    = PBC_StripWowTextCodes(quest->GetDetails());
    std::string questLogDescription = PBC_StripWowTextCodes(quest->GetObjectives());
    std::string questCompletionLog  = PBC_StripWowTextCodes(quest->GetCompletedText());

    PBC_Log(PBC_LogLevel::PBC_LOG_LEVEL_DEBUG, "HandleQuestTaken: leader={} quest='{}' (id={}) giver='{}' type='{}'",
             player->GetName(), questTitle, quest->GetQuestId(), questGiver, questGiverType);

    std::string userPrompt = PBC_SubstituteQuestVars(
        g_PBC_QuestTakenUserPrompt,
        questTitle, questDescription, questLogDescription, questCompletionLog,
        /*rewardText=*/"", questGiver, /*questEnder=*/"", questGiverType, /*questEnderType=*/"");

    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = CHAT_MSG_PARTY;
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestTakenSystemPrompt;
    ev.questUserPrompt    = userPrompt;
    ev.anchorObjGuid      = player->GetGUID();

    if (!PBC_RollGroupBotsIntoEvent(ev, player, g_PBC_ReplyChanceQuestTaken, "quest-taken"))
        return;

    AddTrackedPlayersToEvent(ev, player);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Quest taken — from creature
// ---------------------------------------------------------------------------
PBC_AllCreatureQuestScript::PBC_AllCreatureQuestScript()
    : AllCreatureScript("PBC_AllCreatureQuestScript") {}

bool PBC_AllCreatureQuestScript::CanCreatureQuestAccept(Player* player, Creature* creature, Quest const* quest)
{
    if (creature && quest && player)
    {
        std::string giverName = creature->GetName();
        HandleQuestTaken(player, quest, giverName, "person");
    }
    return false; // don't prevent quest acceptance
}

// ---------------------------------------------------------------------------
// Quest taken — from gameobject
// ---------------------------------------------------------------------------
PBC_AllGameObjectQuestScript::PBC_AllGameObjectQuestScript()
    : AllGameObjectScript("PBC_AllGameObjectQuestScript") {}

bool PBC_AllGameObjectQuestScript::CanGameObjectQuestAccept(Player* player, GameObject* go, Quest const* quest)
{
    if (go && quest && player)
    {
        GameObjectTemplate const* goInfo = go->GetGOInfo();
        std::string giverName = goInfo ? goInfo->name : go->GetName();
        HandleQuestTaken(player, quest, giverName, "object");
    }
    return false; // don't prevent quest acceptance
}

// ---------------------------------------------------------------------------
// Quest taken — from item
// ---------------------------------------------------------------------------
PBC_AllItemQuestScript::PBC_AllItemQuestScript()
    : AllItemScript("PBC_AllItemQuestScript") {}

bool PBC_AllItemQuestScript::CanItemQuestAccept(Player* player, Item* item, Quest const* quest)
{
    if (item && quest && player)
    {
        ItemTemplate const* itemInfo = item->GetTemplate();
        std::string giverName = itemInfo ? itemInfo->Name1 : "Unknown Item";
        HandleQuestTaken(player, quest, giverName, "item");
    }
    return true; // true = allow quest acceptance (AllItemScript convention)
}
