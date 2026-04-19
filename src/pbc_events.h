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
    void OnPlayerEnterCombat(Player* player, Unit* enemy) override;
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override;
};

// ---------------------------------------------------------------------------
// Narrator event formatting helpers
// ---------------------------------------------------------------------------
std::string PBC_MakeEventLine(const std::string& text);
std::string PBC_MakeHistLine(const std::string& text);

// ---------------------------------------------------------------------------
// Dispatch a group event (from any translation unit).
// Builds a PBC_EventItem with snapshots for all bots in anchor's group,
// rolls each bot's chance, and pushes the item onto the global event queue.
// ---------------------------------------------------------------------------
void PBC_DispatchGroupEvent(Player* anchor, const std::string& eventLine,
                             const std::string& histLine, uint32_t chance);

// ---------------------------------------------------------------------------
// Dispatch an event to a single specific bot.
// skipHistoryIfSilent=true: if the bot does not respond, the histLine is NOT
// written to its history (avoids noise from frequent low-chance events).
// ---------------------------------------------------------------------------
void PBC_DispatchBotEvent(Player* bot, const std::string& eventLine,
                          const std::string& histLine, uint32_t chance,
                          bool skipHistoryIfSilent = false);

#endif // MOD_PBC_EVENTS_H
