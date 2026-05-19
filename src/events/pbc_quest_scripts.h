#ifndef MOD_PBC_QUEST_SCRIPTS_H
#define MOD_PBC_QUEST_SCRIPTS_H

#include "AllCreatureScript.h"
#include "AllGameObjectScript.h"
#include "AllItemScript.h"
#include <string>

class Player;
class Creature;
class GameObject;
class Item;
struct Quest const;

// Captures quest-accept events from all creatures.
class PBC_AllCreatureQuestScript : public AllCreatureScript
{
public:
    PBC_AllCreatureQuestScript();
    bool CanCreatureQuestAccept(Player* player, Creature* creature, Quest const* quest) override;
};

// Captures quest-accept events from all gameobjects.
class PBC_AllGameObjectQuestScript : public AllGameObjectScript
{
public:
    PBC_AllGameObjectQuestScript();
    bool CanGameObjectQuestAccept(Player* player, GameObject* go, Quest const* quest) override;
};

// Captures quest-accept events from all items.
class PBC_AllItemQuestScript : public AllItemScript
{
public:
    PBC_AllItemQuestScript();
    bool CanItemQuestAccept(Player* player, Item* item, Quest const* quest) override;
};

#endif // MOD_PBC_QUEST_SCRIPTS_H
