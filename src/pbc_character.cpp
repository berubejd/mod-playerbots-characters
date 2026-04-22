#include "pbc_character.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_utils.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Map.h"
#include "GameTime.h"
#include "DBCStores.h"

#ifdef MOD_WEATHER_VIBE
#include "mod_wv_core.h"
#endif

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
// Race / class / gender helpers
// ---------------------------------------------------------------------------

static std::string RaceStr(uint8_t race)
{
    switch (race)
    {
        case RACE_HUMAN:         return "Human";
        case RACE_ORC:           return "Orc";
        case RACE_DWARF:         return "Dwarf";
        case RACE_NIGHTELF:      return "Night Elf";
        case RACE_UNDEAD_PLAYER: return "Forsaken";
        case RACE_TAUREN:        return "Tauren";
        case RACE_GNOME:         return "Gnome";
        case RACE_TROLL:         return "Troll";
        case RACE_BLOODELF:      return "Blood Elf";
        case RACE_DRAENEI:       return "Draenei";
        default:                 return "Unknown";
    }
}

static std::string ClassStr(uint8_t cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:      return "Warrior";
        case CLASS_PALADIN:      return "Paladin";
        case CLASS_HUNTER:       return "Hunter";
        case CLASS_ROGUE:        return "Rogue";
        case CLASS_PRIEST:       return "Priest";
        case CLASS_DEATH_KNIGHT: return "Death Knight";
        case CLASS_SHAMAN:       return "Shaman";
        case CLASS_MAGE:         return "Mage";
        case CLASS_WARLOCK:      return "Warlock";
        case CLASS_DRUID:        return "Druid";
        default:                 return "Adventurer";
    }
}

static std::string GenderStr(uint8_t gender)
{
    return gender == GENDER_FEMALE ? "female" : "male";
}

// ---------------------------------------------------------------------------
// Game-object-dependent location helpers (main-thread only)
// ---------------------------------------------------------------------------

std::string PBC_BuildPlaceName(Player* player)
{
    uint32_t areaId = player->GetAreaId();
    uint32_t zoneId = player->GetZoneId();
    std::string areaName, zoneName;
    if (AreaTableEntry const* a = sAreaTableStore.LookupEntry(areaId))
        areaName = a->area_name[0];
    if (AreaTableEntry const* z = sAreaTableStore.LookupEntry(zoneId))
        zoneName = z->area_name[0];
    if (!areaName.empty() && !zoneName.empty() && areaName != zoneName)
        return zoneName + ", " + areaName;
    if (!areaName.empty())
        return areaName;
    return zoneName;
}

std::string PBC_BuildFlightDestination(Player* bot)
{
    const std::deque<uint32>& path = bot->m_taxi.GetPath();
    if (!path.empty())
    {
        uint32 finalNodeId = path.back();
        if (TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(finalNodeId))
        {
            if (node->name[0] && node->name[0][0] != '\0')
                return node->name[0];
        }
    }
    return {};
}

std::string PBC_BuildFlightLocationString(Player* bot)
{
    std::string dest = PBC_BuildFlightDestination(bot);
    if (!dest.empty())
        return "You are currently flying to " + dest + ".";
    return "You are currently flying.";
}

std::string PBC_BuildCombatStatusStr(Player* bot)
{
    if (!bot->IsInCombat())
        return "You are not currently in combat.";
    if (Unit* victim = bot->GetVictim())
        return "You are currently fighting " + std::string(victim->GetName()) + ".";
    return "You are currently in combat.";
}

std::string PBC_BuildLosStr(Player* bot)
{
    constexpr float kLosRadius = 40.0f;
    std::vector<std::string> entries;

    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* p = pair.second;
        if (!p || p == bot) continue;
        if (!p->IsInWorld() || p->IsGameMaster()) continue;
        if (p->GetMap() != bot->GetMap()) continue;
        if (!bot->IsWithinDistInMap(p, kLosRadius)) continue;
        if (!bot->IsWithinLOS(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ())) continue;
        entries.push_back(std::string(p->GetName()));
    }

    Map* map = bot->GetMap();
    if (map)
    {
        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* c = pair.second;
            if (!c) continue;
            if (c->GetGUID() == bot->GetGUID()) continue;
            if (!bot->IsWithinDistInMap(c, kLosRadius)) continue;
            if (!bot->IsWithinLOS(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ())) continue;
            if (c->IsPet() || c->IsTotem()) continue;
            entries.push_back(std::string(c->GetName()));
        }
    }

    return PBC_NaturalList(entries);
}

// ---------------------------------------------------------------------------
// Scene helpers
// ---------------------------------------------------------------------------

static std::string TimeOfDayLabel()
{
    // Derive time-of-day from the real server clock (UTC).
    time_t rawTime = static_cast<time_t>(GameTime::GetGameTime().count());
    struct tm* t = gmtime(&rawTime);
    int hour = t ? t->tm_hour : 12;

    if (hour >= 0  && hour < 2)  return "early night";
    if (hour >= 2  && hour < 4)  return "night";
    if (hour >= 4  && hour < 6)  return "late night";
    if (hour >= 6  && hour < 8)  return "early morning";
    if (hour >= 8  && hour < 10) return "morning";
    if (hour >= 10 && hour < 12) return "late morning";
    if (hour >= 12 && hour < 14) return "noon";
    if (hour >= 14 && hour < 16) return "afternoon";
    if (hour >= 16 && hour < 18) return "late afternoon";
    if (hour >= 18 && hour < 20) return "early evening";
    if (hour >= 20 && hour < 22) return "late evening";
    return "early night";
}

#ifdef MOD_WEATHER_VIBE
// Returns the weather clause to append after the time-of-day, e.g.
// "it's foggy", "it's raining lightly", "there is a heavy sandstorm".
// Returns nullptr for WEATHER_STATE_FINE (clear sky — no clause appended).
static char const* WeatherClause(WeatherState s)
{
    switch (s)
    {
        case WEATHER_STATE_FINE:             return nullptr;
        case WEATHER_STATE_FOG:              return "it's foggy";
        case WEATHER_STATE_LIGHT_RAIN:       return "it's raining lightly";
        case WEATHER_STATE_MEDIUM_RAIN:      return "it's raining";
        case WEATHER_STATE_HEAVY_RAIN:       return "it's raining heavily";
        case WEATHER_STATE_LIGHT_SNOW:       return "it's snowing lightly";
        case WEATHER_STATE_MEDIUM_SNOW:      return "it's snowing";
        case WEATHER_STATE_HEAVY_SNOW:       return "it's snowing heavily";
        case WEATHER_STATE_LIGHT_SANDSTORM:  return "there is a light sandstorm";
        case WEATHER_STATE_MEDIUM_SANDSTORM: return "there is a sandstorm";
        case WEATHER_STATE_HEAVY_SANDSTORM:  return "there is a heavy sandstorm";
        case WEATHER_STATE_THUNDERS:         return "there is a thunderstorm";
        default:                             return nullptr;
    }
}
#endif

static std::string BuildSceneStr(Player* bot)
{
    std::string timeLabel = TimeOfDayLabel();

#ifdef MOD_WEATHER_VIBE
    if (bot && sWeatherVibeCore.IsEnabled())
    {
        uint32 zoneId = bot->GetZoneId();
        auto const& lastApplied = sWeatherVibeCore.GetLastApplied();
        auto it = lastApplied.find(zoneId);
        if (it != lastApplied.end() && it->second.hasValue)
        {
            char const* clause = WeatherClause(it->second.state);
            if (clause)
            {
                std::string result = "It's currently " + timeLabel + " and " + std::string(clause);
                // When indoors, note that the character is sheltered from the weather
                if (!bot->IsOutdoors())
                    result += ", but you are inside and sheltered from the weather";
                return result + ".";
            }
        }
    }
#endif

    return "It is currently " + timeLabel + ".";
}

static std::string RoleStr(Player* bot)
{
    switch (bot->getClass())
    {
        case CLASS_WARRIOR:      return bot->GetSpec() == 2 ? "tank" : "melee DPS";
        case CLASS_PALADIN:      return "paladin";
        case CLASS_HUNTER:       return "ranged DPS";
        case CLASS_ROGUE:        return "melee DPS";
        case CLASS_PRIEST:       return "healer";
        case CLASS_DEATH_KNIGHT: return "death knight";
        case CLASS_SHAMAN:       return "shaman";
        case CLASS_MAGE:         return "ranged DPS";
        case CLASS_WARLOCK:      return "ranged DPS";
        case CLASS_DRUID:        return "druid";
        default:                 return "adventurer";
    }
}

// ---------------------------------------------------------------------------
// BuildGroupStatusStr  (main-thread only)
//
// Returns the {char_group} string for the given bot, reflecting the party
// leader first (as "Commander") followed by the remaining members.
// ---------------------------------------------------------------------------

static std::string BuildGroupStatusStr(Player* bot)
{
    if (!bot) return "You are not currently in a group.";

    Group* grp = bot->GetGroup();
    if (!grp) return "You are not currently in a group.";

    ObjectGuid leaderGuid = grp->GetLeaderGUID();

    auto memberInfo = [](Player* member) -> std::string {
        return member->GetName()
             + " (" + GenderStr(member->getGender())
             + " " + RaceStr(member->getRace())
             + " " + ClassStr(member->getClass()) + ")";
    };

    std::string leaderStr;
    std::string members;
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsInWorld()) continue;
        if (member->GetGUID() == leaderGuid)
            leaderStr = memberInfo(member);
        else
        {
            if (!members.empty()) members += ", ";
            members += memberInfo(member);
        }
    }

    if (leaderStr.empty() && members.empty())
        return "You are currently in a group.";
    if (leaderStr.empty())
        return "You are currently in a group with the following members: " + members + ".";
    if (members.empty())
        return "You are currently in a group led by " + leaderStr + ".";
    return "You are currently in a group led by " + leaderStr
         + " with the following members: " + members + ".";
}

// ---------------------------------------------------------------------------
// PBC_SubstituteVars  (main-thread only)
// ---------------------------------------------------------------------------

std::string PBC_SubstituteVars(const std::string& tmpl, Player* bot, const std::string& event,
                                bool expandComposites)
{
    std::string out = tmpl;
    PBC_ExpandNewlineEscapes(out);

    if (bot)
    {
        PBC_ReplaceToken(out, "char_name",   bot->GetName());
        PBC_ReplaceToken(out, "char_gender", GenderStr(bot->getGender()));
        PBC_ReplaceToken(out, "char_race",   RaceStr(bot->getRace()));
        PBC_ReplaceToken(out, "char_class",  ClassStr(bot->getClass()));
        PBC_ReplaceToken(out, "char_role",   RoleStr(bot));
        PBC_ReplaceToken(out, "char_level",  std::to_string(bot->GetLevel()));
        { uint32 m = bot->GetMoney(); PBC_ReplaceToken(out, "char_gold", std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"); }

        // Location
        std::string location;
        if (bot->IsInFlight())
            location = PBC_BuildFlightLocationString(bot);
        else
            location = "You are currently in " + PBC_BuildPlaceName(bot) + ".";
        PBC_ReplaceToken(out, "char_location", location);

        // Scene (time of day + optional weather)
        PBC_ReplaceToken(out, "scene", BuildSceneStr(bot));

        // Combat status
        PBC_ReplaceToken(out, "combat_status", PBC_BuildCombatStatusStr(bot));

        // Group status
        PBC_ReplaceToken(out, "char_group", BuildGroupStatusStr(bot));

        // Line-of-sight
        PBC_ReplaceToken(out, "char_los", PBC_BuildLosStr(bot));

        PBC_ReplaceToken(out, "nearby_chars", "");

        if (expandComposites)
        {
            PBC_ReplaceToken(out, "character_card", PBC_GetCharacterCard(bot));
            PBC_ReplaceToken(out, "chat_history",   PBC_GetChatHistory(bot->GetGUID().GetCounter()));
            PBC_ReplaceToken(out, "context",        PBC_GetCharacterContext(bot));
        }
    }

    PBC_ReplaceToken(out, "event", event);
    return out;
}

// ---------------------------------------------------------------------------
// PBC_GetCharacterCard  (main-thread only)
// ---------------------------------------------------------------------------

std::string PBC_GetCharacterCard(Player* bot)
{
    const std::string& name = bot->GetName();

    std::string base;
    auto it = g_PBC_CharacterCards.find(name);
    if (it != g_PBC_CharacterCards.end())
        base = PBC_SubstituteVars(it->second, bot, "", false);
    else
        base = PBC_SubstituteVars(g_PBC_DefaultCharacterDescription, bot, "", false);

    uint64_t guid = bot->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardMutex);
        auto addit = g_PBC_CardAdditions.find(guid);
        if (addit != g_PBC_CardAdditions.end() && !addit->second.empty())
        {
            base += "\n\n";
            for (const auto& add : addit->second)
                base += add + "\n";
        }
    }

    return base;
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
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto& hist = g_PBC_ChatHistory[botGuid];
        if (!hist.empty() && hist.back() == line)
            return;
        hist.push_back(line);
    }
    DB_InsertHistoryLine(botGuid, line);
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
    PBC_ReplaceToken(out, "char_location", snap.charLocation);
    PBC_ReplaceToken(out, "scene",         snap.scene);
    PBC_ReplaceToken(out, "char_group",    snap.charGroup);
    PBC_ReplaceToken(out, "char_los",      snap.charLos);
    PBC_ReplaceToken(out, "combat_status", snap.combatStatus);
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
    snap.charGender   = GenderStr(bot->getGender());
    snap.charRace     = RaceStr(bot->getRace());
    snap.charClass    = ClassStr(bot->getClass());
    snap.charRole     = RoleStr(bot);
    snap.charLevel    = std::to_string(bot->GetLevel());
    { uint32 m = bot->GetMoney(); snap.charGold = std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"; }

    // Scene (time of day + optional weather)
    snap.scene = BuildSceneStr(bot);

    // Location
    if (bot->IsInFlight())
        snap.charLocation = PBC_BuildFlightLocationString(bot);
    else
        snap.charLocation = "You are currently in " + PBC_BuildPlaceName(bot) + ".";

    // Combat status
    snap.combatStatus = PBC_BuildCombatStatusStr(bot);

    // Group status
    snap.charGroup = BuildGroupStatusStr(bot);

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
    std::string race   = RaceStr(p->getRace());
    std::transform(race.begin(), race.end(), race.begin(), ::toupper);
    std::string cls    = ClassStr(p->getClass());
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
