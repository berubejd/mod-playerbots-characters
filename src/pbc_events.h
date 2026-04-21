#ifndef MOD_PBC_EVENTS_H
#define MOD_PBC_EVENTS_H

#include "ScriptMgr.h"
#include "QuestDef.h"
#include <string>

// Listens to player / world events and feeds them to the bot character system.

class PBC_PlayerEvents : public PlayerScript
{
public:
    PBC_PlayerEvents();

    // Chat events
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                            std::string& msg, Player* receiver) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                            std::string& msg) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                            std::string& msg, Group* group) override;

    // World events bots may react to
    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override;
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override;
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override;
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override;
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
// Dispatch an event to a single specific bot.
// skipHistoryIfSilent=true: if the bot does not respond, the histLine is NOT
// written to its history (avoids noise from frequent low-chance events).
// notifyRealPlayers=false: suppress the narrator system message sent to real
// players in the group (used for location events, which fire per individual
// bot and would be noisy if shown for each one).
// ---------------------------------------------------------------------------
void PBC_DispatchBotEvent(Player* bot, const std::string& eventLine,
                          const std::string& histLine, uint32_t chance,
                          bool skipHistoryIfSilent = false,
                          bool notifyRealPlayers = true);

#endif // MOD_PBC_EVENTS_H
