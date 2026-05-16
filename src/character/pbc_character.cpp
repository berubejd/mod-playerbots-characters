#include "pbc_character.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_scene_helpers.h"
#include "pbc_equipment_helpers.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Map.h"

#include <fmt/core.h>
#include <sstream>
#include <mutex>
#include <ctime>
#include <algorithm>

// ---------------------------------------------------------------------------
// PBC_TriggerCondensation  (main-thread only)
//
// Pushes a Condensation event for the given character onto the global event queue.
// The event thread will call PBC_CondenseInline when it processes the item.
// ---------------------------------------------------------------------------
void PBC_TriggerCondensation(Player* bot)
{
    if (!bot) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] TriggerCondensation: queuing condensation for character={}", bot->GetName());

    PBC_EventItem ev;
    ev.type                      = PBC_EventType::Condensation;
    ev.condensationChar          = PBC_SnapshotCharacter(bot);
    ev.condensationSystemPrompt  = g_PBC_CondensationSystemPrompt;
    ev.condensationUserPrompt    = g_PBC_CondensationUserPrompt;

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_SubstituteVars  (main-thread only)
// ---------------------------------------------------------------------------

std::string PBC_SubstituteVars(const std::string& tmpl, Player* bot, const std::string& event,
                                bool expandComposites, bool annotate)
{
    std::string out = tmpl;
    PBC_ExpandNewlineEscapes(out);

    // Helper: replaces {key} with value (or {key}value when annotate=true)
    auto replace = [&](const std::string& key, const std::string& value)
    {
        PBC_ReplaceToken(out, key, annotate ? ("{" + key + "}" + value) : value);
    };

    if (bot)
    {
        replace("char_name",   bot->GetName());
        replace("char_gender", PBC_GenderStr(bot->getGender()));
        replace("char_race",   PBC_RaceStr(bot->getRace()));
        replace("char_class",  PBC_ClassStr(bot->getClass()));
        replace("char_role",   PBC_RoleStr(bot));
        replace("char_level",  std::to_string(bot->GetLevel()));
        { uint32 m = bot->GetMoney(); replace("char_gold", std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"); }

        // Scene (location + travel state + time of day + optional weather)
        replace("scene", PBC_BuildSceneStr(bot));

        // Combat status
        replace("combat_status", PBC_BuildCombatStatusStr(bot));

        // Equipment
        replace("equipment", PBC_BuildEquipmentStr(bot));

        // Group status
        replace("char_group", PBC_BuildGroupStatusStr(bot));

        // Line-of-sight
        replace("char_los", PBC_BuildLosStr(bot));

        replace("nearby_chars", "");

        if (expandComposites)
        {
            replace("character_card", PBC_GetCharacterCard(bot));
            replace("chat_history",   PBC_GetChatHistory(bot->GetGUID().GetCounter()));
            replace("context",        PBC_GetCharacterContext(bot));
        }
    }

    replace("event", event);
    return out;
}

// ---------------------------------------------------------------------------
// PBC_GetCharacterCard  (main-thread only)
// ---------------------------------------------------------------------------

std::string PBC_GetCharacterCard(Player* bot)
{
    const std::string& name = bot->GetName();

    auto it = g_PBC_CharacterCards.find(name);
    if (it != g_PBC_CharacterCards.end())
        return PBC_SubstituteVars(it->second, bot, "", false);
    return PBC_SubstituteVars(g_PBC_DefaultCharacterDescription, bot, "", false);
}

// ---------------------------------------------------------------------------
// PBC_GetMemoriesBlock  (thread-safe)
//
// Builds the [MEMORIES] text block for a character's user prompt.
// Selection: sort all memories by importance DESC, take the most important
// ones until the token budget (g_PBC_MaxMemoriesCtx) is exceeded.
// Output: the selected memories are sorted chronologically (by DB id ASC)
// so the narrative flows naturally.
// ---------------------------------------------------------------------------
std::string PBC_GetMemoriesBlock(uint64_t botGuid)
{
    // We need both the entries and their original positions for chronological
    // ordering. The vector in g_PBC_Memories is always maintained in
    // chronological order (loaded by id ASC from DB, new entries appended at
    // end during condensation), so the vector index is a reliable proxy.
    struct IndexedEntry
    {
        size_t           origIndex;
        PBC_MemoryEntry  entry;
    };

    std::vector<IndexedEntry> entries;
    {
        std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
        auto it = g_PBC_Memories.find(botGuid);
        if (it == g_PBC_Memories.end() || it->second.empty())
            return "";
        entries.reserve(it->second.size());
        for (size_t i = 0; i < it->second.size(); ++i)
            entries.push_back({i, it->second[i]});
    }

    // Sort by importance DESC (most important first) for selection
    std::sort(entries.begin(), entries.end(),
        [](const IndexedEntry& a, const IndexedEntry& b)
        {
            if (a.entry.importance != b.entry.importance)
                return a.entry.importance > b.entry.importance;
            return a.origIndex < b.origIndex; // tie-break: earlier first
        });

    // Select memories that fit within the token budget
    // Token estimation: ~4 chars per token
    const uint32_t budget = g_PBC_MaxMemoriesCtx;
    uint32_t usedTokens = 0;
    std::vector<IndexedEntry> selected;

    for (const auto& ie : entries)
    {
        uint32_t entryTokens = static_cast<uint32_t>(ie.entry.text.size()) / 4 + 1;
        if (usedTokens + entryTokens > budget)
            break;
        usedTokens += entryTokens;
        selected.push_back(ie);
    }

    if (selected.empty())
        return "";

    // Re-sort selected chronologically (by original index ASC) for output
    std::sort(selected.begin(), selected.end(),
        [](const IndexedEntry& a, const IndexedEntry& b)
        {
            return a.origIndex < b.origIndex;
        });

    std::ostringstream oss;
    for (const auto& ie : selected)
        oss << ie.entry.text << "\n";

    std::string result = oss.str();
    // Trim trailing newline
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}

// ---------------------------------------------------------------------------
// PBC_GetCharacterContext  (main-thread only)
// ---------------------------------------------------------------------------

std::string PBC_GetCharacterContext(Player* bot)
{
    return PBC_SubstituteVars(g_PBC_CharacterContext, bot, "", false);
}

// ---------------------------------------------------------------------------
// PBC_GetChatHistory  (thread-safe)
// ---------------------------------------------------------------------------

std::string PBC_GetChatHistory(uint64_t botGuid)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_ChatHistory.find(botGuid);
    if (it == g_PBC_ChatHistory.end() || it->second.empty())
        return "";

    std::ostringstream oss;
    for (const auto& line : it->second)
        oss << line << "\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// PBC_AppendHistory  (thread-safe)
// ---------------------------------------------------------------------------

void PBC_AppendHistory(uint64_t botGuid, const std::string& line)
{
    static const std::string kTimePassesLine = "Narrator: *some time passes*";
    static constexpr time_t  kTimeGapThresholdSec = 300; // 5 minutes

    bool needTimePasses = false;
    bool dedup = false;

    // Collect WS history events inside the lock (with correct 1-based ids),
    // emit them outside the lock to avoid holding g_PBC_HistoryMutex during
    // network I/O.
    std::vector<std::pair<size_t, std::string>> wsHistoryEvents;

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto& hist = g_PBC_ChatHistory[botGuid];

        // Check for time gap before the new line.
        auto timeIt = g_PBC_LastHistoryTime.find(botGuid);
        if (timeIt != g_PBC_LastHistoryTime.end())
        {
            time_t lastTime = timeIt->second;
            time_t now = time(nullptr);
            if (now - lastTime >= kTimeGapThresholdSec)
            {
                // Check if last line is already "some time passes"
                bool lastIsTimePasses = !hist.empty() && hist.back() == kTimePassesLine;

                // Check if last line and current line are both private messages.
                // A private message line starts with "Name (privately to ...", i.e.
                // the first space in the line must be the one before "(privately to".
                auto isPrivateMsg = [](const std::string& s) -> bool {
                    auto privPos = s.find(" (privately to ");
                    if (privPos == std::string::npos || privPos == 0)
                        return false;
                    auto firstSpace = s.find(' ');
                    return firstSpace == privPos;
                };
                bool lastIsPrivate = !hist.empty() && isPrivateMsg(hist.back());
                bool currentIsPrivate = isPrivateMsg(line);

                if (!lastIsTimePasses && !(lastIsPrivate && currentIsPrivate))
                {
                    needTimePasses = true;
                    hist.push_back(kTimePassesLine);
                    wsHistoryEvents.emplace_back(hist.size(), kTimePassesLine);
                }
            }
        }

        // Dedup check
        if (!hist.empty() && hist.back() == line)
            dedup = true;
        else
        {
            hist.push_back(line);
            wsHistoryEvents.emplace_back(hist.size(), line);
        }

        // Update timestamp
        g_PBC_LastHistoryTime[botGuid] = time(nullptr);
    }

    // DB writes outside the lock
    if (needTimePasses)
        DB_InsertHistoryLine(botGuid, kTimePassesLine);
    if (!dedup)
        DB_InsertHistoryLine(botGuid, line);

    // WS notifications outside the lock
    for (const auto& ev : wsHistoryEvents)
        PBC_WsNotifyHistory(botGuid, ev.first, ev.second);
}

// ---------------------------------------------------------------------------
// PBC_EstimateHistoryTokens  (thread-safe)
// ---------------------------------------------------------------------------

int PBC_EstimateHistoryTokens(uint64_t botGuid)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_ChatHistory.find(botGuid);
    if (it == g_PBC_ChatHistory.end()) return 0;

    int total = 0;
    for (const auto& line : it->second)
        total += static_cast<int>(line.size()) / 4 + 1;
    return total;
}

// ---------------------------------------------------------------------------
// PBC_UpdateHistoryLine  (thread-safe)
//
// Updates a single history line at the given 0-based index for a character.
// The current content at the index is compared against originalMessage first
// — a mismatch returns PBC_HistoryResult::Desync without modifying anything.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateHistoryLine(uint64_t botGuid, size_t index,
                                        const std::string& newMessage,
                                        const std::string& originalMessage)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_ChatHistory.find(botGuid);
    if (it == g_PBC_ChatHistory.end() || index >= it->second.size())
        return PBC_HistoryResult::NotFound;

    if (it->second[index] != originalMessage)
        return PBC_HistoryResult::Desync;

    it->second[index] = newMessage;

    // DB write outside the lock — but we need to release first, so capture
    // the parameters before unlocking.  The DB update is best-effort; the
    // in-memory state is authoritative.
    DB_UpdateHistoryLineByIndex(botGuid, index, newMessage);
    return PBC_HistoryResult::Ok;
}

// ---------------------------------------------------------------------------
// PBC_DeleteHistoryLine  (thread-safe)
//
// Deletes a single history line at the given 0-based index for a character.
// The current content at the index is compared against originalMessage first
// — a mismatch returns PBC_HistoryResult::Desync without modifying anything.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_DeleteHistoryLine(uint64_t botGuid, size_t index,
                                        const std::string& originalMessage)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_ChatHistory.find(botGuid);
    if (it == g_PBC_ChatHistory.end() || index >= it->second.size())
        return PBC_HistoryResult::NotFound;

    if (it->second[index] != originalMessage)
        return PBC_HistoryResult::Desync;

    it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));

    DB_DeleteHistoryLineByIndex(botGuid, index);
    return PBC_HistoryResult::Ok;
}


// ---------------------------------------------------------------------------
// PBC_UpdateRelationship  (thread-safe)
//
// Updates the relationship text for a specific (bot, target) pair.
// The current text is compared against originalText first — a mismatch
// returns PBC_HistoryResult::Desync without modifying anything.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& newText,
                                          const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    auto charIt = g_PBC_Relationships.find(botGuid);
    if (charIt == g_PBC_Relationships.end())
        return PBC_HistoryResult::NotFound;

    auto relIt = charIt->second.find(targetName);
    if (relIt == charIt->second.end())
        return PBC_HistoryResult::NotFound;

    if (relIt->second.text != originalText)
        return PBC_HistoryResult::Desync;

    relIt->second.text = newText;
    DB_UpdateRelationshipText(botGuid, targetName, newText);
    return PBC_HistoryResult::Ok;
}

// ---------------------------------------------------------------------------
// PBC_DeleteRelationship  (thread-safe)
//
// Deletes the relationship entry for a specific (bot, target) pair.
// The current text is compared against originalText first — a mismatch
// returns PBC_HistoryResult::Desync without modifying anything.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_DeleteRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    auto charIt = g_PBC_Relationships.find(botGuid);
    if (charIt == g_PBC_Relationships.end())
        return PBC_HistoryResult::NotFound;

    auto relIt = charIt->second.find(targetName);
    if (relIt == charIt->second.end())
        return PBC_HistoryResult::NotFound;

    if (relIt->second.text != originalText)
        return PBC_HistoryResult::Desync;

    charIt->second.erase(relIt);
    DB_DeleteRelationship(botGuid, targetName);
    return PBC_HistoryResult::Ok;
}

// ---------------------------------------------------------------------------
// PBC_UpdateMemory  (thread-safe)
//
// Updates the text and importance of a single memory identified by its DB row
// id.  The current text is compared against originalText first — a mismatch
// returns PBC_HistoryResult::Desync without modifying anything.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateMemory(uint64_t botGuid, uint64_t memoryId,
                                    const std::string& newText,
                                    uint8_t newImportance,
                                    const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    auto it = g_PBC_Memories.find(botGuid);
    if (it == g_PBC_Memories.end())
        return PBC_HistoryResult::NotFound;

    for (auto& entry : it->second)
    {
        if (entry.dbId != memoryId)
            continue;

        if (entry.text != originalText)
            return PBC_HistoryResult::Desync;

        entry.text       = newText;
        entry.importance = newImportance;
        DB_UpdateMemoryById(memoryId, newText, newImportance);
        return PBC_HistoryResult::Ok;
    }

    return PBC_HistoryResult::NotFound;
}

// ---------------------------------------------------------------------------
// PBC_DeleteMemory  (thread-safe)
//
// Deletes a single memory identified by its DB row id.  The current text is
// compared against originalText first — a mismatch returns
// PBC_HistoryResult::Desync without modifying anything.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_DeleteMemory(uint64_t botGuid, uint64_t memoryId,
                                    const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    auto it = g_PBC_Memories.find(botGuid);
    if (it == g_PBC_Memories.end())
        return PBC_HistoryResult::NotFound;

    for (auto vit = it->second.begin(); vit != it->second.end(); ++vit)
    {
        if (vit->dbId != memoryId)
            continue;

        if (vit->text != originalText)
            return PBC_HistoryResult::Desync;

        it->second.erase(vit);
        DB_DeleteMemoryById(memoryId);
        return PBC_HistoryResult::Ok;
    }

    return PBC_HistoryResult::NotFound;
}

// ---------------------------------------------------------------------------
// Snapshot var substitution helper (thread-safe, uses snapshot only)
//
// Replaces all snapshot-based template variables in the given string.
// Used by both PBC_BuildUserPromptFromSnapshot and
// PBC_BuildCondensationPromptFromSnapshot to avoid duplicating the
// variable list.
// ---------------------------------------------------------------------------
static void ReplaceSnapshotVars(std::string& out, const PBC_CharacterSnapshot& snap,
                                const std::string& eventLine)
{
    // Composite vars
    PBC_ReplaceToken(out, "character_card", snap.characterCard);
    PBC_ReplaceToken(out, "memories",       PBC_GetMemoriesBlock(snap.charGuidRaw));
    PBC_ReplaceToken(out, "context",        snap.context);

    // Chat history from the snapshot's local (thread-local) copy
    { std::ostringstream histOss; for (const auto& line : snap.history) histOss << line << "\n"; PBC_ReplaceToken(out, "chat_history", histOss.str()); }

    // Basic vars
    PBC_ReplaceToken(out, "char_name",     snap.charName);
    PBC_ReplaceToken(out, "char_gender",   snap.charGender);
    PBC_ReplaceToken(out, "char_race",     snap.charRace);
    PBC_ReplaceToken(out, "char_class",    snap.charClass);
    PBC_ReplaceToken(out, "char_role",     snap.charRole);
    PBC_ReplaceToken(out, "char_level",    snap.charLevel);
    PBC_ReplaceToken(out, "char_gold",     snap.charGold);
    PBC_ReplaceToken(out, "scene",         snap.scene);
    PBC_ReplaceToken(out, "char_group",    snap.charGroup);
    PBC_ReplaceToken(out, "char_los",      snap.charLos);
    PBC_ReplaceToken(out, "combat_status", snap.combatStatus);
    PBC_ReplaceToken(out, "equipment",     snap.equipment);
    PBC_ReplaceToken(out, "nearby_chars",  "");  // deprecated, keep for template compat
    PBC_ReplaceToken(out, "event",         eventLine);
}

// ---------------------------------------------------------------------------
// PBC_SnapshotCharacter  (main-thread only)
//
// Captures all live Player* data into a PBC_CharacterSnapshot.  The result is safe
// to hand off to an event thread without further access to game objects.
// ---------------------------------------------------------------------------

PBC_CharacterSnapshot PBC_SnapshotCharacter(Player* bot)
{
    PBC_CharacterSnapshot snap;
    snap.charObjGuid  = bot->GetGUID();
    snap.charGuidRaw  = bot->GetGUID().GetCounter();
    snap.charName     = bot->GetName();

    // Pre-render the character card and context once here so the event thread
    // never needs to call into game data.
    snap.characterCard = PBC_GetCharacterCard(bot);
    snap.context       = PBC_GetCharacterContext(bot);

    // Capture raw template variables
    snap.charGender   = PBC_GenderStr(bot->getGender());
    snap.charRace     = PBC_RaceStr(bot->getRace());
    snap.charClass    = PBC_ClassStr(bot->getClass());
    snap.charRole     = PBC_RoleStr(bot);
    snap.charLevel    = std::to_string(bot->GetLevel());
    { uint32 m = bot->GetMoney(); snap.charGold = std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"; }

    // Scene (location + travel state + time of day + optional weather)
    snap.scene = PBC_BuildSceneStr(bot);

    // Combat status
    snap.combatStatus = PBC_BuildCombatStatusStr(bot);

    // Equipment
    snap.equipment = PBC_BuildEquipmentStr(bot);

    // Group status
    snap.charGroup = PBC_BuildGroupStatusStr(bot);

    // Line-of-sight
    snap.charLos = PBC_BuildLosStr(bot);

    // Capture the current global history into the snapshot's local copy.
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto it = g_PBC_ChatHistory.find(snap.charGuidRaw);
        if (it != g_PBC_ChatHistory.end())
            snap.history = it->second;
    }

    // Capture party member names and whether a real player is in the group.
    {
        snap.partyMemberNames.clear();
        snap.hasRealPlayerInGroup = false;

        if (Group* grp = bot->GetGroup())
        {
            for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsInWorld() || member == bot) continue;

                WorldSession* ms = member->GetSession();
                if (!ms) continue;

                snap.partyMemberNames.push_back(member->GetName());
                if (!ms->IsBot())
                    snap.hasRealPlayerInGroup = true;
            }
        }
    }

    return snap;
}

// ---------------------------------------------------------------------------
// PBC_BuildTargetInfo  (main-thread only; safe to call from event thread as
// a read-only ObjectAccessor pass if called carefully, but here we assume
// it is called from the event thread where we do a best-effort lookup via
// ObjectAccessor::FindPlayerByName which is thread-safe for reads).
//
// Returns e.g. "JOHN, MALE TAUREN SHAMAN" if the player is online,
// or just "JOHN" as a fallback.
// ---------------------------------------------------------------------------

std::string PBC_BuildTargetInfo(const std::string& name)
{
    // Upper-case the name
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    Player* p = ObjectAccessor::FindPlayerByName(name);
    if (!p)
        return upper;

    std::string gender = p->getGender() == GENDER_FEMALE ? "FEMALE" : "MALE";
    std::string race   = PBC_RaceStr(p->getRace());
    std::transform(race.begin(), race.end(), race.begin(), ::toupper);
    std::string cls    = PBC_ClassStr(p->getClass());
    std::transform(cls.begin(), cls.end(), cls.begin(), ::toupper);

    return upper + ", " + gender + " " + race + " " + cls;
}

// ---------------------------------------------------------------------------
// PBC_GetRelationshipsBlock  (thread-safe)
//
// Builds the [RELATIONSHIPS] text block for a character's user prompt.
// Every entry uses the format:
//   "Your relationship with <name>: <description>"
//
// Two scenarios:
//
// 1. Character is NOT in a group with a real player (hasRealPlayerInGroup == false):
//    Only the whispering player's relationship line is emitted (or the
//    fallback if they are unknown).
//
// 2. Character IS in a group with a real player (hasRealPlayerInGroup == true):
//    One line per party member (excluding this bot). If the whisper target
//    is not already a party member (i.e. an outside player whispering in),
//    their relationship line is appended as well.
// ---------------------------------------------------------------------------

std::string PBC_GetRelationshipsBlock(const PBC_CharacterSnapshot& snap)
{
    // Read all relationship entries for this character under a single lock.
    std::unordered_map<std::string, std::string> relTexts;
    {
        std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
        auto charIt = g_PBC_Relationships.find(snap.charGuidRaw);
        if (charIt != g_PBC_Relationships.end())
        {
            for (const auto& kv : charIt->second)
                relTexts[kv.first] = kv.second.text;
        }
    }

    auto emitRelationship = [&](std::ostringstream& oss, const std::string& name)
    {
        auto it = relTexts.find(name);
        if (it != relTexts.end() && !it->second.empty())
            oss << "Your relationship with " << name << ": " << it->second << "\n";
        else
            oss << "Your relationship with " << name << ": " << PBC_DefaultRelationshipText(name) << "\n";
    };

    if (!snap.hasRealPlayerInGroup)
    {
        // Solo whisper: only emit the relationship with the whispering player.
        if (snap.whisperTargetName.empty())
            return "";

        std::ostringstream oss;
        emitRelationship(oss, snap.whisperTargetName);
        std::string result = oss.str();
        if (!result.empty() && result.back() == '\n')
            result.pop_back();
        return result;
    }

    // Group scenario: emit one line per party member.
    if (snap.partyMemberNames.empty() && snap.whisperTargetName.empty())
        return "";

    std::ostringstream oss;
    for (const auto& memberName : snap.partyMemberNames)
        emitRelationship(oss, memberName);

    // If the whisper came from a player outside the group, add them too.
    if (!snap.whisperTargetName.empty())
    {
        bool alreadyListed = std::find(snap.partyMemberNames.begin(),
                                       snap.partyMemberNames.end(),
                                       snap.whisperTargetName) != snap.partyMemberNames.end();
        if (!alreadyListed)
            emitRelationship(oss, snap.whisperTargetName);
    }

    std::string result = oss.str();
    // Trim trailing newline
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}

// ---------------------------------------------------------------------------
// PBC_BuildUserPromptFromSnapshot  (thread-safe)
//
// Builds a fully-substituted user prompt using only data in the snapshot.
// The snapshot's local history copy is used for {chat_history}, which means
// any replies posted to history by earlier bots in the same event are visible.
// ---------------------------------------------------------------------------

std::string PBC_BuildUserPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                             const std::string& eventLine)
{
    std::string out = g_PBC_UserPrompt;
    PBC_ExpandNewlineEscapes(out);

    // Substitute all snapshot vars (composites, basic vars, event)
    ReplaceSnapshotVars(out, snap, eventLine);

    // Relationships block (only in the main user prompt, not condensation)
    PBC_ReplaceToken(out, "relationships", PBC_GetRelationshipsBlock(snap));

    return out;
}

// ---------------------------------------------------------------------------
// PBC_BuildCondensationPromptFromSnapshot  (thread-safe)
//
// Builds the user prompt for the condensation LLM call using the snapshot's
// local history.  All {chat_history} references are fulfilled from the
// snapshot rather than the global history map.
// ---------------------------------------------------------------------------

std::string PBC_BuildCondensationPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                                     const std::string& tmpl)
{
    std::string out = tmpl;
    PBC_ExpandNewlineEscapes(out);

    // Substitute all snapshot vars with empty event
    ReplaceSnapshotVars(out, snap, "");

    return out;
}
