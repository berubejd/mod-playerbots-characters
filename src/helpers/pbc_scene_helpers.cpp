#include "pbc_scene_helpers.h"
#include "pbc_config.h"
#include "pbc_utils.h"
#include "pbc_wmo_areas.h"
#include "Log.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Map.h"
#include "GameTime.h"
#include "DBCStores.h"
#include "SpellInfo.h"
#include "SpellAuraDefines.h"
#include "SpellAuraEffects.h"
#include "Group.h"

#ifdef MOD_WEATHER_VIBE
#include "mod_wv_core.h"
#endif

#include <ctime>
#include <algorithm>

// ---------------------------------------------------------------------------
// Time-of-day helper
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
// For WEATHER_STATE_FINE (clear sky) returns "the weather is fine" to
// provide reliable context instead of silence.
static char const* WeatherClause(WeatherState s)
{
    switch (s)
    {
        case WEATHER_STATE_FINE:             return "the weather is fine";
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

// ---------------------------------------------------------------------------
// Location helpers
// ---------------------------------------------------------------------------

std::string PBC_BuildPlaceName(Player* player)
{
    uint32_t areaId = player->GetAreaId();

    // Collect the full area hierarchy by walking up the parent chain via zone field.
    // e.g. "Goldshire" -> "Elwynn Forest"
    std::vector<std::string> names;
    uint32_t currentId = areaId;
    int maxDepth = 10; // safety guard against unexpected cycles

    while (currentId != 0 && maxDepth-- > 0)
    {
        AreaTableEntry const* entry = sAreaTableStore.LookupEntry(currentId);
        if (!entry)
            break;

        std::string name = entry->area_name[0];
        if (!name.empty())
            names.push_back(name);

        currentId = entry->zone;
    }

    // Try to get a more specific WMO area name when indoors.
    // The server's WMOAreaTableEntry doesn't load name fields, so we parse
    // the DBC ourselves (pbc_wmo_areas) to get names like "Lion's Pride Inn".
    std::string wmoName;
    if (!g_PBC_WmoAreaNames.empty())
    {
        uint32_t mogpFlags;
        int32_t adtId, rootId, groupId;
        if (player->GetMap()->GetAreaInfo(
                player->GetPhaseMask(),
                player->GetPositionX(),
                player->GetPositionY(),
                player->GetPositionZ(),
                mogpFlags, adtId, rootId, groupId))
        {
            wmoName = PBC_GetWmoAreaName(rootId, adtId, groupId);
        }
    }

    // If we got a WMO name that differs from the first area name, prepend it.
    // e.g. names = ["Goldshire", "Elwynn Forest"], wmoName = "Lion's Pride Inn"
    //      -> "Lion's Pride Inn (Goldshire, Elwynn Forest)"
    if (!wmoName.empty() && (names.empty() || wmoName != names[0]))
    {
        names.insert(names.begin(), wmoName);
    }

    if (names.empty())
        return "Unknown";

    if (names.size() == 1)
        return names[0];

    // Format: "SubZone (Parent, GrandParent, ...)"
    // e.g. "Lion's Pride Inn (Goldshire, Elwynn Forest)"
    std::string result = names[0] + " (";
    for (size_t i = 1; i < names.size(); ++i)
    {
        if (i > 1)
            result += ", ";
        result += names[i];
    }
    result += ")";
    return result;
}

std::string PBC_BuildZoneName(Player* player)
{
    // Walk up the area hierarchy and return the root zone name
    // (the topmost entry whose zone field is 0, or the last name found).
    uint32_t currentId = player->GetAreaId();
    std::string rootName;
    int maxDepth = 10;

    while (currentId != 0 && maxDepth-- > 0)
    {
        AreaTableEntry const* entry = sAreaTableStore.LookupEntry(currentId);
        if (!entry)
            break;

        std::string name = entry->area_name[0];
        if (!name.empty())
            rootName = name;

        currentId = entry->zone;
    }

    return rootName.empty() ? "Unknown" : rootName;
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

// ---------------------------------------------------------------------------
// Combat / LOS helpers
// ---------------------------------------------------------------------------

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
    constexpr float kLosRadius = 30.0f;
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
// Scene builder
// ---------------------------------------------------------------------------

std::string PBC_BuildSceneStr(Player* bot)
{
    std::string timeLabel = TimeOfDayLabel();

    // Build the time/weather suffix (lowercase, no trailing period — it will be
    // appended after a comma inside a larger sentence).
    std::string timeWeather;

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
                timeWeather = "it's " + timeLabel + " and " + std::string(clause);
                // When indoors and weather is not fine, note that the character is sheltered
                if (!bot->IsOutdoors() && it->second.state != WEATHER_STATE_FINE)
                    timeWeather += ", but you are inside and sheltered from the weather";
            }
        }
    }
#endif

    if (timeWeather.empty())
        timeWeather = "it's " + timeLabel;

    // --- Taxi flight ---
    if (bot && bot->IsInFlight())
    {
        std::string dest = PBC_BuildFlightDestination(bot);
        if (!dest.empty())
            return "You are currently flying to " + dest + ", " + timeWeather + ".";
        return "You are currently flying, " + timeWeather + ".";
    }

    // --- Mounted ---
    if (bot && bot->IsMounted())
    {
        std::string place = PBC_BuildPlaceName(bot);
        auto auraEffects = bot->GetAuraEffectsByType(SPELL_AURA_MOUNTED);
        if (!auraEffects.empty())
        {
            SpellInfo const* spellInfo = auraEffects.front()->GetSpellInfo();
            std::string mountName = spellInfo->SpellName[0];
            bool isFlyingMount = (spellInfo->Effects[1].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED ||
                                  spellInfo->Effects[2].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED);
            if (isFlyingMount)
                return "You are currently flying " + mountName + " in " + place + ", " + timeWeather + ".";
            else
                return "You are currently riding " + mountName + " in " + place + ", " + timeWeather + ".";
        }
        // Fallback if aura not found
        return "You are currently riding a mount in " + place + ", " + timeWeather + ".";
    }

    // --- On foot ---
    std::string place = PBC_BuildPlaceName(bot);
    return "You are currently on foot in " + place + ", " + timeWeather + ".";
}

// ---------------------------------------------------------------------------
// Role / group helpers
// ---------------------------------------------------------------------------

std::string PBC_RoleStr(Player* bot)
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

std::string PBC_BuildGroupStatusStr(Player* bot)
{
    if (!bot) return "You are not currently in a group.";

    Group* grp = bot->GetGroup();
    if (!grp) return "You are not currently in a group.";

    ObjectGuid leaderGuid = grp->GetLeaderGUID();

    auto memberInfo = [](Player* member) -> std::string {
        return member->GetName()
             + " (" + PBC_GenderStr(member->getGender())
             + " " + PBC_RaceStr(member->getRace())
             + " " + PBC_ClassStr(member->getClass()) + ")";
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
