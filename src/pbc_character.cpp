#include "pbc_character.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_utils.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Bag.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Map.h"
#include "GameTime.h"
#include "DBCStores.h"
#include "SpellInfo.h"
#include "SpellAuraDefines.h"
#include "SpellAuraEffects.h"

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
        return areaName + " (" + zoneName + ")";
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
// Equipment helpers (main-thread only)
// ---------------------------------------------------------------------------

// Returns "a" or "an" based on the first character of the word that follows.
static const char* ArticleFor(const std::string& word)
{
    if (word.empty()) return "a";
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(word[0])));
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an" : "a";
}

static std::string EquipQualityStr(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_UNCOMMON: return "uncommon";
        case ITEM_QUALITY_RARE:     return "rare";
        case ITEM_QUALITY_EPIC:     return "epic";
        case ITEM_QUALITY_LEGENDARY:return "legendary";
        case ITEM_QUALITY_ARTIFACT: return "artifact";
        case ITEM_QUALITY_HEIRLOOM: return "heirloom";
        default:                    return "common";
    }
}

static std::string EquipWeaponTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_WEAPON_AXE:      return "one-handed axe";
        case ITEM_SUBCLASS_WEAPON_AXE2:     return "two-handed axe";
        case ITEM_SUBCLASS_WEAPON_BOW:      return "bow";
        case ITEM_SUBCLASS_WEAPON_GUN:      return "gun";
        case ITEM_SUBCLASS_WEAPON_MACE:     return "one-handed mace";
        case ITEM_SUBCLASS_WEAPON_MACE2:    return "two-handed mace";
        case ITEM_SUBCLASS_WEAPON_POLEARM:  return "polearm";
        case ITEM_SUBCLASS_WEAPON_SWORD:    return "one-handed sword";
        case ITEM_SUBCLASS_WEAPON_SWORD2:   return "two-handed sword";
        case ITEM_SUBCLASS_WEAPON_STAFF:    return "staff";
        case ITEM_SUBCLASS_WEAPON_FIST:     return "fist weapon";
        case ITEM_SUBCLASS_WEAPON_DAGGER:   return "dagger";
        case ITEM_SUBCLASS_WEAPON_THROWN:   return "thrown weapon";
        case ITEM_SUBCLASS_WEAPON_CROSSBOW: return "crossbow";
        case ITEM_SUBCLASS_WEAPON_WAND:     return "wand";
        default:                            return "weapon";
    }
}

// Returns a human-readable off-hand type string (shield, relic, holdable, etc.)
static std::string EquipOffhandTypeStr(ItemTemplate const* tmpl)
{
    if (!tmpl) return "off-hand item";
    if (tmpl->Class == ITEM_CLASS_WEAPON)
        return EquipWeaponTypeStr(tmpl->SubClass);
    if (tmpl->Class == ITEM_CLASS_ARMOR)
    {
        switch (tmpl->SubClass)
        {
            case ITEM_SUBCLASS_ARMOR_SHIELD:  return "shield";
            case ITEM_SUBCLASS_ARMOR_BUCKLER: return "buckler";
            case ITEM_SUBCLASS_ARMOR_LIBRAM:  return "libram";
            case ITEM_SUBCLASS_ARMOR_IDOL:    return "idol";
            case ITEM_SUBCLASS_ARMOR_TOTEM:   return "totem";
            case ITEM_SUBCLASS_ARMOR_SIGIL:   return "sigil";
            default: break;
        }
        if (tmpl->InventoryType == INVTYPE_HOLDABLE)
            return "off-hand focus";
    }
    return "off-hand item";
}

// Builds a phrase like "a rare dagger" or "an epic staff called Devastation".
// For rare+ items the name is included.
static std::string DescribeWeapon(ItemTemplate const* tmpl)
{
    if (!tmpl) return "";
    std::string type = EquipWeaponTypeStr(tmpl->SubClass);
    if (tmpl->Quality >= ITEM_QUALITY_RARE)
    {
        std::string qual = EquipQualityStr(tmpl->Quality);
        return std::string(ArticleFor(qual)) + " " + qual + " " + type + " called " + tmpl->Name1;
    }
    return std::string(ArticleFor(type)) + " " + type;
}

// Builds a phrase for an off-hand item (shield, relic, holdable, or weapon).
static std::string DescribeOffhand(ItemTemplate const* tmpl)
{
    if (!tmpl) return "";
    if (tmpl->Class == ITEM_CLASS_WEAPON)
        return DescribeWeapon(tmpl);
    std::string type = EquipOffhandTypeStr(tmpl);
    if (tmpl->Quality >= ITEM_QUALITY_RARE)
    {
        std::string qual = EquipQualityStr(tmpl->Quality);
        return std::string(ArticleFor(qual)) + " " + qual + " " + type + " called " + tmpl->Name1;
    }
    return std::string(ArticleFor(type)) + " " + type;
}

// Returns a bag-space summary string for roleplaying purposes.
// Returns empty string when bags are less than ~40% full (nothing noteworthy).
// Gradations: about half full → getting full → almost full → nearly full → completely full.
static std::string BuildBagSpaceStr(Player* bot)
{
    // Backpack: INVENTORY_SLOT_BAG_0, slots INVENTORY_SLOT_ITEM_START..INVENTORY_SLOT_ITEM_END-1
    uint32 backpackTotal = INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START;
    uint32 backpackUsed  = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            backpackUsed++;
    }

    uint32 totalSlots = backpackTotal;
    uint32 usedSlots  = backpackUsed;

    // Equipped bags (slots 19-22)
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        uint32 bagSize = bag->GetBagSize();
        uint32 bagFree = bag->GetFreeSlots();
        totalSlots += bagSize;
        usedSlots  += (bagSize - bagFree);
    }

    float fillPct = (totalSlots > 0) ? (float(usedSlots) / float(totalSlots) * 100.0f) : 0.0f;

    if (fillPct < 40.0f)
        return "";
    if (fillPct <= 60.0f)
        return "Your bags are about half full.";
    if (fillPct <= 80.0f)
        return "Your bags are getting full.";
    if (fillPct <= 95.0f)
        return "Your bags are almost full.";
    if (fillPct < 100.0f)
        return "Your bags are nearly full.";
    return "Your bags are completely full.";
}

std::string PBC_BuildEquipmentStr(Player* bot)
{
    // --- Armor assessment ---
    // Slots to consider for armor quality (exclude weapon slots)
    static const uint8 armorSlots[] = {
        EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_SHOULDERS,
        EQUIPMENT_SLOT_BODY, EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS,
        EQUIPMENT_SLOT_HANDS, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
        EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2,
        EQUIPMENT_SLOT_BACK, EQUIPMENT_SLOT_TABARD
    };

    int poorCount = 0, uncommonCount = 0, rareCount = 0, epicCount = 0, legendaryCount = 0;
    int clothCount = 0, leatherCount = 0, mailCount = 0, plateCount = 0;
    int totalArmor = 0;

    for (uint8 slot : armorSlots)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item) continue;
        ItemTemplate const* tmpl = item->GetTemplate();
        if (!tmpl) continue;
        totalArmor++;

        switch (tmpl->Quality)
        {
            case ITEM_QUALITY_POOR:
            case ITEM_QUALITY_NORMAL:   poorCount++;      break;
            case ITEM_QUALITY_UNCOMMON: uncommonCount++;   break;
            case ITEM_QUALITY_RARE:     rareCount++;       break;
            case ITEM_QUALITY_EPIC:     epicCount++;       break;
            case ITEM_QUALITY_LEGENDARY:
            case ITEM_QUALITY_ARTIFACT:
            case ITEM_QUALITY_HEIRLOOM: legendaryCount++;  break;
        }

        if (tmpl->Class == ITEM_CLASS_ARMOR)
        {
            switch (tmpl->SubClass)
            {
                case ITEM_SUBCLASS_ARMOR_CLOTH:   clothCount++;   break;
                case ITEM_SUBCLASS_ARMOR_LEATHER: leatherCount++;  break;
                case ITEM_SUBCLASS_ARMOR_MAIL:    mailCount++;     break;
                case ITEM_SUBCLASS_ARMOR_PLATE:   plateCount++;    break;
            }
        }
    }

    // Build armor quality description
    std::string armorDesc;

    if (totalArmor == 0)
    {
        armorDesc = "You have no armor";
    }
    else
    {
        // Find dominant quality tier; on ties the highest quality wins
        struct Tier { int count; const char* adj; };
        Tier tiers[] = {
            { poorCount,       "simple" },
            { uncommonCount,   "modest" },
            { rareCount,       "fine" },
            { epicCount,       "excellent" },
            { legendaryCount,  "exceptional" }
        };

        int maxCount = 0;
        const char* qualityAdj = "simple";
        for (const auto& t : tiers)
        {
            if (t.count >= maxCount && t.count > 0)
            {
                maxCount = t.count;
                qualityAdj = t.adj;
            }
        }

        // Find dominant armor material (cloth/leather/mail/plate)
        const char* material = nullptr;
        int matMax = 0;
        struct Mat { int count; const char* name; };
        Mat mats[] = { {clothCount, "cloth"}, {leatherCount, "leather"}, {mailCount, "mail"}, {plateCount, "plate"} };
        for (const auto& m : mats)
        {
            if (m.count > matMax)
            {
                matMax = m.count;
                material = m.name;
            }
        }

        armorDesc = "You have " + std::string(qualityAdj) + " equipment";
        if (material && matMax >= 2)
            armorDesc += " made of " + std::string(material);
    }

    // --- Weapon assessment ---
    Item* mainItem  = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    Item* offItem   = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    Item* rangeItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);

    ItemTemplate const* mhT = mainItem  ? mainItem->GetTemplate()  : nullptr;
    ItemTemplate const* ohT = offItem   ? offItem->GetTemplate()   : nullptr;
    ItemTemplate const* rgT = rangeItem ? rangeItem->GetTemplate() : nullptr;

    bool is2H       = mhT && mhT->InventoryType == INVTYPE_2HWEAPON;
    bool mhIsWeapon = mhT && mhT->Class == ITEM_CLASS_WEAPON;
    bool ohIsWeapon = ohT && ohT->Class == ITEM_CLASS_WEAPON;
    bool rgIsWeapon = rgT && rgT->Class == ITEM_CLASS_WEAPON;

    std::string weaponDesc;

    if (!mhT && !ohT && !rgT)
    {
        weaponDesc = "you are unarmed";
    }
    else if (is2H)
    {
        weaponDesc = "you wield " + DescribeWeapon(mhT);
        if (rgIsWeapon)
            weaponDesc += " and carry " + DescribeWeapon(rgT);
    }
    else if (mhT && ohT)
    {
        // Dual wield of same weapon type, both rare+: special phrasing
        if (ohIsWeapon && mhIsWeapon && mhT->SubClass == ohT->SubClass &&
            mhT->Quality >= ITEM_QUALITY_RARE && ohT->Quality >= ITEM_QUALITY_RARE)
        {
            std::string qual = EquipQualityStr(mhT->Quality);
            std::string type = EquipWeaponTypeStr(mhT->SubClass);
            weaponDesc = "you wield two " + qual + " " + type + "s, called " + mhT->Name1 + " and " + ohT->Name1;
        }
        else
        {
            std::string mhDesc = mhIsWeapon ? DescribeWeapon(mhT) : DescribeOffhand(mhT);
            std::string ohDesc = ohIsWeapon ? DescribeWeapon(ohT) : DescribeOffhand(ohT);
            weaponDesc = "you wield " + mhDesc + " and " + ohDesc;
        }
        if (rgIsWeapon)
            weaponDesc += ", and carry " + DescribeWeapon(rgT);
    }
    else if (mhT)
    {
        weaponDesc = "you wield " + (mhIsWeapon ? DescribeWeapon(mhT) : DescribeOffhand(mhT));
        if (rgIsWeapon)
            weaponDesc += " and carry " + DescribeWeapon(rgT);
    }
    else if (ohT)
    {
        weaponDesc = "you carry " + DescribeOffhand(ohT);
    }
    else
    {
        // Only ranged
        weaponDesc = "you carry " + DescribeWeapon(rgT);
    }

    std::string result = armorDesc + ", and " + weaponDesc + ".";

    // Append bag space summary when noteworthy (≥40% full)
    std::string bagSpace = BuildBagSpaceStr(bot);
    if (!bagSpace.empty())
        result += " " + bagSpace;

    return result;
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
                // When indoors, note that the character is sheltered from the weather
                if (!bot->IsOutdoors())
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

        // Scene (location + travel state + time of day + optional weather)
        PBC_ReplaceToken(out, "scene", BuildSceneStr(bot));

        // Combat status
        PBC_ReplaceToken(out, "combat_status", PBC_BuildCombatStatusStr(bot));

        // Equipment
        PBC_ReplaceToken(out, "equipment", PBC_BuildEquipmentStr(bot));

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
    snap.charGender   = GenderStr(bot->getGender());
    snap.charRace     = RaceStr(bot->getRace());
    snap.charClass    = ClassStr(bot->getClass());
    snap.charRole     = RoleStr(bot);
    snap.charLevel    = std::to_string(bot->GetLevel());
    { uint32 m = bot->GetMoney(); snap.charGold = std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"; }

    // Scene (location + travel state + time of day + optional weather)
    snap.scene = BuildSceneStr(bot);

    // Combat status
    snap.combatStatus = PBC_BuildCombatStatusStr(bot);

    // Equipment
    snap.equipment = PBC_BuildEquipmentStr(bot);

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
