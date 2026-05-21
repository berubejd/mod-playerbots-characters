#ifndef MOD_PBC_EVENT_DISPATCH_H
#define MOD_PBC_EVENT_DISPATCH_H

#include <string>
#include <vector>

class Player;
struct PBC_EventItem;

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
// Pick the trigger event line based on the character's last history message.
// Returns the event text (without surrounding asterisks — caller wraps with
// PBC_MakeEventLine).  The logic is:
//   1. Last line is "Narrator: *some time passes*" → random pick from four
//      "you want to …" variants.
//   2. Last line is from Narrator but NOT "some time passes" →
//      "you feel the urge to comment on the last thing that happened".
//   3. Last line is NOT from Narrator and NOT a whisper →
//      "you feel like answering that".
//   4. All other cases (no history, last line is whisper) →
//      "you feel the urge to say something".
// Thread-safe.
// ---------------------------------------------------------------------------
std::string PBC_PickTriggerEventLine(uint64_t botGuid);

// ---------------------------------------------------------------------------
// Dispatch a trigger event for a single character.
// The character always responds (no roll). The event line is picked
// dynamically based on the last history message (see PBC_PickTriggerEventLine)
// and is NOT written to history.
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
// When PBC.TrackPlayerCharacter is enabled, adds all real (non-bot) players in
// the anchor's group (including the anchor itself if it's a real player) to the
// event's silentCharGuids and playerCharGuids.  This ensures player characters
// receive history passively during play sessions.
// ---------------------------------------------------------------------------
void AddTrackedPlayersToEvent(PBC_EventItem& ev, Player* anchor);

#endif // MOD_PBC_EVENT_DISPATCH_H
