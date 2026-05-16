#ifndef MOD_PBC_EVENTS_H
#define MOD_PBC_EVENTS_H

#include "ScriptMgr.h"
#include "AllCreatureScript.h"
#include "AllGameObjectScript.h"
#include "AllItemScript.h"
#include "QuestDef.h"
#include "pbc_config.h"
#include "pbc_group_helpers.h"
#include "pbc_combat_helpers.h"
#include <string>

// Listens to player / world events and feeds them to the character system.

class PBC_PlayerEvents : public PlayerScript
{
public:
    PBC_PlayerEvents();

    // Chat events
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg, Player* receiver) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg, Group* group) override;

    // World events bots may react to
    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid lootguid) override;
    void OnPlayerQuestRewardItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count, RollVote voteType, Roll* roll) override;
    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override;
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override;
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override;
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override;
    void OnPlayerJustDied(Player* player) override;
};

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

// ---------------------------------------------------------------------------
// Narrator event formatting helpers
// ---------------------------------------------------------------------------
std::string PBC_MakeEventLine(const std::string& text);
std::string PBC_MakeHistLine(const std::string& text);

// ---------------------------------------------------------------------------
// Send a narrator system message to all real (non-bot) players in the
// same group as 'anchor' (or to anchor alone if ungrouped).
// No-op when PBC.DisplayNarratorEvents is disabled.
// ---------------------------------------------------------------------------
void PBC_NotifyRealPlayersInGroup(Player* anchor, const std::string& eventLine);

// ---------------------------------------------------------------------------
// Dispatch a group event (from any translation unit).
// Builds a PBC_EventItem with snapshots for all bots in anchor's group,
// rolls each bot's chance, and pushes the item onto the global event queue.
// notifyRealPlayers=false: suppress the narrator system message sent to real
// players in the group (used for combat events, which can be very frequent).
// ---------------------------------------------------------------------------
void PBC_DispatchGroupEvent(Player* anchor, const std::string& eventLine,
                             const std::string& histLine, uint32_t chance,
                             bool notifyRealPlayers = true);

// ---------------------------------------------------------------------------
// Roll bots with decaying penalty.  Does NOT shuffle — caller should shuffle
// the bot list first if randomised order is desired.
// Fills ev.respondingChars and ev.silentCharGuids.
// debugLabel is used in log messages to identify the roll context.
// ---------------------------------------------------------------------------
void PBC_RollBotsWithPenalty(PBC_EventItem& ev,
                              const std::vector<Player*>& bots,
                              uint32_t baseChance,
                              const char* debugLabel = "Roll");

// ---------------------------------------------------------------------------
// Convenience wrapper: finds group bots for 'player', shuffles them, and
// rolls with penalty into the provided event item.  Returns true if any bots
// were found (regardless of roll outcomes), false if the player has no group
// bots — in which case the caller should abort the event.
// ---------------------------------------------------------------------------
bool PBC_RollGroupBotsIntoEvent(PBC_EventItem& ev, Player* player,
                                 uint32_t chance, const char* debugLabel = "event");

// ---------------------------------------------------------------------------
// Full message roll logic: checks for mentions, sorts mentioned bots by
// position in the message, rolls mentioned bots at mention chance, then
// non-mentioned bots at a reduced chance with decaying penalty.
// Handles shuffling internally.
// ---------------------------------------------------------------------------
void PBC_RollBotsForMessage(PBC_EventItem& ev,
                             const std::vector<Player*>& bots,
                             const std::string& message);

// ---------------------------------------------------------------------------
// Dispatch a whisper event from sender to target bot.
// Rolls chance, takes snapshot with whisper target info, pushes PBC_EventItem.
// Used by both in-game whisper hook and HTTP API whisper endpoint.
// ---------------------------------------------------------------------------
void PBC_DispatchWhisperEvent(Player* sender, Player* target, const std::string& msg);

// ---------------------------------------------------------------------------
// Dispatch a trigger event for a single character.
// The character always responds (no roll). The event line is
// "*you feel the urge to say something*" and is NOT written to history.
// Chat type is PARTY if the character is in a group, SAY otherwise.
// ---------------------------------------------------------------------------
void PBC_DispatchTriggerEvent(Player* bot);

// ---------------------------------------------------------------------------
// Dispatch a party/raid chat message event from sender to their group bots.
// Finds group bots, rolls chances (mention-aware), pushes PBC_EventItem.
// senderNameOverride: when non-empty, replaces sender->GetName() (used by API
// where the "sender" may be a name string rather than the actual Player* name).
// chatType: defaults to CHAT_MSG_PARTY; pass CHAT_MSG_RAID etc. for raid chat.
// canCreateEvents: defaults to true; set false to prevent secondary events.
// ---------------------------------------------------------------------------
void PBC_DispatchPartyMessageEvent(Player* sender, const std::string& msg,
                                    const std::string& senderNameOverride = "",
                                    uint32_t chatType = 0,
                                    bool canCreateEvents = true);

// ---------------------------------------------------------------------------
// Poll party flight/location state (called from OnUpdate every 5 seconds).
// Checks all groups with at least one real player and one bot, dispatches
// events for flight starts and location changes.
// ---------------------------------------------------------------------------
void PBC_PollPartyState();

// ---------------------------------------------------------------------------
// Process a single event item (runs in a detached thread).
// ---------------------------------------------------------------------------
void PBC_ProcessEventItem(PBC_EventItem ev);

#endif // MOD_PBC_EVENTS_H
