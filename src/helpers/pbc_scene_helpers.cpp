#include "pbc_scene_helpers.h"
#include "pbc_config.h"
#include "pbc_utils.h"
#include "pbc_wmo_areas.h"
#include "Log.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Creature.h"
#include "Map.h"
#include "GameTime.h"
#include "DBCStores.h"
#include "SpellInfo.h"
#include "SpellAuraDefines.h"
#include "SpellAuraEffects.h"
#include "Group.h"
#include "Pet.h"
#include "SharedDefines.h"

#ifdef MOD_WEATHER_VIBE
#include "mod_wv_core.h"
#endif

#include <ctime>
#include <algorithm>
#include <unordered_map>

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
    std::unordered_map<std::string, int> nameCounts;

    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* p = pair.second;
        if (!p || p == bot) continue;
        if (!p->IsInWorld() || p->IsGameMaster()) continue;
        if (p->GetMap() != bot->GetMap()) continue;
        if (!bot->IsWithinDistInMap(p, kLosRadius)) continue;
        if (!bot->IsWithinLOS(p->GetPositionX(), p->GetPositionY(), p->GetPositionZ())) continue;
        nameCounts[std::string(p->GetName())]++;
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
            nameCounts[std::string(c->GetName())]++;
        }
    }

    // Build grouped entries, sorted by count ascending (unique NPCs first)
    std::vector<std::pair<std::string, int>> sorted(nameCounts.begin(), nameCounts.end());
    std::sort(sorted.begin(), sorted.end(),
        [](auto const& a, auto const& b) { return a.second < b.second; });

    std::vector<std::string> entries;
    for (auto const& pair : sorted)
    {
        if (pair.second > 1)
            entries.push_back(pair.first + " x" + std::to_string(pair.second));
        else
            entries.push_back(pair.first);
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

// ---------------------------------------------------------------------------
// Pet info helpers
// ---------------------------------------------------------------------------

// Map warlock demon NPC entry to a human-readable demon type name.
static const char* GetDemonTypeName(uint32 entry)
{
    switch (entry)
    {
        case 416:   return "imp";
        case 1860:  return "voidwalker";
        case 1863:  return "succubus";
        case 417:   return "felhunter";
        case 17252: return "felguard";
        default:    return "demon";
    }
}

// Check whether a player is "capable" of having a permanent pet/summon.
// Returns false for classes that never have pets (warrior, rogue, etc.).
// For DK and Mage, returns true only when the required talent/glyph is active.
static bool IsPetCapable(Player* bot)
{
    if (!bot) return false;

    switch (bot->getClass())
    {
        case CLASS_HUNTER:
            return bot->HasSpell(1515); // Tame Beast
        case CLASS_WARLOCK:
            return bot->HasSpell(688)  // Summon Imp
                || bot->HasSpell(697)  // Summon Voidwalker
                || bot->HasSpell(712)  // Summon Succubus
                || bot->HasSpell(691)  // Summon Felhunter
                || bot->HasSpell(30146); // Summon Felguard
        case CLASS_DEATH_KNIGHT:
            return bot->HasAura(52143); // Master of Ghouls
        case CLASS_MAGE:
            return bot->HasAura(70937); // Glyph of Eternal Water
        default:
            return false;
    }
}

// Check whether a player's class *could* eventually have a permanent pet
// (i.e. they are the right class, even if not yet capable).
static bool IsPetClass(Player* bot)
{
    if (!bot) return false;
    switch (bot->getClass())
    {
        case CLASS_HUNTER:
        case CLASS_WARLOCK:
        case CLASS_DEATH_KNIGHT:
        case CLASS_MAGE:
            return true;
        default:
            return false;
    }
}

// Build the "not capable" message for hunters and warlocks who haven't
// learned the required spells yet.
static std::string BuildNotCapableStr(Player* bot)
{
    switch (bot->getClass())
    {
        case CLASS_HUNTER:
            return "You currently don't know how to tame or call a pet.";
        case CLASS_WARLOCK:
            return "You currently don't know how to summon a demon.";
        default:
            return "";
    }
}

// Build the "no pet out" message for capable pet classes.
static std::string BuildNoPetStr(Player* bot)
{
    switch (bot->getClass())
    {
        case CLASS_HUNTER:       return "You currently have no pet at your side.";
        case CLASS_WARLOCK:      return "You currently have no demon at your side.";
        case CLASS_DEATH_KNIGHT: return "You currently have no risen ghoul at your side.";
        case CLASS_MAGE:         return "You currently have no water elemental at your side.";
        default:                 return "";
    }
}

// Get the pet family name for a hunter pet from DBC CreatureFamily.
static std::string GetHunterPetFamilyName(Pet* pet)
{
    CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(pet->GetEntry());
    if (!ct || ct->family == 0)
        return "pet";

    CreatureFamilyEntry const* familyEntry = sCreatureFamilyStore.LookupEntry(ct->family);
    if (!familyEntry || !familyEntry->Name[0])
        return "pet";

    return std::string(familyEntry->Name[0]);
}

// Build the alive-pet string for the owner (pet_info variable).
static std::string BuildAlivePetStr(Player* bot, Pet* pet)
{
    std::string petName = pet->GetName();

    switch (bot->getClass())
    {
        case CLASS_HUNTER:
        {
            std::string family = GetHunterPetFamilyName(pet);
            std::transform(family.begin(), family.end(), family.begin(), ::tolower);

            HappinessState happiness = pet->GetHappinessState();
            switch (happiness)
            {
                case HAPPY:
                    return "Your " + family + " " + petName + " is by your side, happy and alert.";
                case CONTENT:
                    return "Your " + family + " " + petName + " is by your side, content.";
                case UNHAPPY:
                default:
                    return "Your " + family + " " + petName + " is by your side, but seems unhappy.";
            }
        }
        case CLASS_WARLOCK:
        {
            std::string demonType = GetDemonTypeName(pet->GetEntry());
            return "Your " + demonType + " " + petName + " is by your side.";
        }
        case CLASS_DEATH_KNIGHT:
            return "Your risen ghoul " + petName + " is by your side.";
        case CLASS_MAGE:
            return "Your water elemental is by your side.";
        default:
            return "";
    }
}

// Build the dead-pet string for the owner (pet_info variable).
// Only hunters get "seriously wounded" wording for their own pet.
static std::string BuildDeadPetStr(Player* bott, Pet* pet)
{
    switch (bott->getClass())
    {
        case CLASS_HUNTER:
        {
            std::string family = GetHunterPetFamilyName(pet);
            std::transform(family.begin(), family.end(), family.begin(), ::tolower);
            return "Your " + family + " " + pet->GetName() + " is seriously wounded.";
        }
        default:
            // For non-hunters, a dead pet is effectively gone — fall through
            // to the "no pet" message.
            return BuildNoPetStr(bott);
    }
}

std::string PBC_BuildPetInfoStr(Player* bot)
{
    if (!bot) return "";

    // Only handle pet classes
    if (!IsPetClass(bot))
        return "";

    // Check capability
    if (!IsPetCapable(bot))
        return BuildNotCapableStr(bot);

    // Check if pet is out
    Pet* pet = bot->GetPet();
    if (!pet || !pet->IsInWorld())
        return BuildNoPetStr(bot);

    // Ignore temporary summons (cooldown-based, not permanent companions)
    if (pet->isTemporarySummoned())
        return BuildNoPetStr(bot);

    // Check if dead
    if (pet->isDead())
        return BuildDeadPetStr(bot, pet);

    // Alive permanent pet
    return BuildAlivePetStr(bot, pet);
}

// Build the pet info snippet for a group member's pet (char_group_pets).
// This is appended to the group status string for other party members.
static std::string BuildMemberPetSnippet(Player* owner, Pet* pet)
{
    std::string ownerName = owner->GetName();
    std::string petName = pet->GetName();

    switch (owner->getClass())
    {
        case CLASS_HUNTER:
        {
            std::string family = GetHunterPetFamilyName(pet);
            std::transform(family.begin(), family.end(), family.begin(), ::tolower);
            if (pet->isDead())
                return family + " " + petName + " (" + ownerName + "'s pet, seriously wounded)";
            return family + " " + petName + " (" + ownerName + "'s pet)";
        }
        case CLASS_WARLOCK:
        {
            std::string demonType = GetDemonTypeName(pet->GetEntry());
            if (pet->isDead())
                return demonType + " " + petName + " (" + ownerName + "'s demon, seriously wounded)";
            return demonType + " " + petName + " (" + ownerName + "'s demon)";
        }
        case CLASS_DEATH_KNIGHT:
        {
            if (pet->isDead())
                return petName + " (" + ownerName + "'s risen ghoul, seriously wounded)";
            return petName + " (" + ownerName + "'s risen ghoul)";
        }
        case CLASS_MAGE:
        {
            if (pet->isDead())
                return "Water Elemental (" + ownerName + "'s summon, seriously wounded)";
            return "Water Elemental (" + ownerName + "'s summon)";
        }
        default:
            return petName + " (" + ownerName + "'s pet)";
    }
}

std::string PBC_BuildPetInfoForMember(Player* member)
{
    if (!member) return "";

    // Only handle pet classes
    if (!IsPetClass(member))
        return "";

    // Must be capable
    if (!IsPetCapable(member))
        return "";

    Pet* pet = member->GetPet();
    if (!pet || !pet->IsInWorld())
        return "";

    // Ignore temporary summons
    if (pet->isTemporarySummoned())
        return "";

    return BuildMemberPetSnippet(member, pet);
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
    std::string groupPets; // Pet info for other party members

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == bot || !member->IsInWorld()) continue;

        // Collect pet info for this member
        std::string memberPet = PBC_BuildPetInfoForMember(member);
        if (!memberPet.empty())
        {
            if (!groupPets.empty()) groupPets += ", ";
            groupPets += memberPet;
        }

        if (member->GetGUID() == leaderGuid)
            leaderStr = memberInfo(member);
        else
        {
            if (!members.empty()) members += ", ";
            members += memberInfo(member);
        }
    }

    // Build the base group string
    std::string result;
    if (leaderStr.empty() && members.empty())
        result = "You are currently in a group";
    else if (leaderStr.empty())
        result = "You are currently in a group with the following members: " + members;
    else if (members.empty())
        result = "You are currently in a group led by " + leaderStr;
    else
        result = "You are currently in a group led by " + leaderStr
               + " with the following members: " + members;

    // Append pet info before the closing period
    if (!groupPets.empty())
        result += ", " + groupPets;

    result += ".";
    return result;
}
