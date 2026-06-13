#include "pbc_equipment_helpers.h"
#include "pbc_item_helpers.h"
#include "pbc_utils.h"
#include "pbc_locales.h"
#include "Player.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Bag.h"
#include <vector>

// ---------------------------------------------------------------------------
// Equipment quality / off-hand helpers (local to this TU)
// ---------------------------------------------------------------------------

static std::string EquipQualityStr(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_UNCOMMON: return PBC_Localize("uncommon");
        case ITEM_QUALITY_RARE:     return PBC_Localize("rare");
        case ITEM_QUALITY_EPIC:     return PBC_Localize("epic");
        case ITEM_QUALITY_LEGENDARY:return PBC_Localize("legendary");
        case ITEM_QUALITY_ARTIFACT: return PBC_Localize("artifact");
        case ITEM_QUALITY_HEIRLOOM: return PBC_Localize("heirloom");
        default:                    return PBC_Localize("common");
    }
}

// Returns a human-readable type string for any equipped item (shield, relic,
// holdable, or weapon).  Used for both off-hand and ranged-slot items.
static std::string EquipItemTypeStr(ItemTemplate const* tmpl)
{
    if (!tmpl) return PBC_Localize("off-hand item");
    if (tmpl->Class == ITEM_CLASS_WEAPON)
        return PBC_WeaponTypeStr(tmpl->SubClass);
    if (tmpl->Class == ITEM_CLASS_ARMOR)
    {
        switch (tmpl->SubClass)
        {
            case ITEM_SUBCLASS_ARMOR_SHIELD:  return PBC_Localize("shield");
            case ITEM_SUBCLASS_ARMOR_BUCKLER: return PBC_Localize("buckler");
            case ITEM_SUBCLASS_ARMOR_LIBRAM:  return PBC_Localize("libram");
            case ITEM_SUBCLASS_ARMOR_IDOL:    return PBC_Localize("idol");
            case ITEM_SUBCLASS_ARMOR_TOTEM:   return PBC_Localize("totem");
            case ITEM_SUBCLASS_ARMOR_SIGIL:   return PBC_Localize("sigil");
            default: break;
        }
        if (tmpl->InventoryType == INVTYPE_HOLDABLE)
            return PBC_Localize("off-hand focus");
    }
    return PBC_Localize("off-hand item");
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
        return PBC_Localize("Your bags are about half full.");
    if (fillPct <= 80.0f)
        return PBC_Localize("Your bags are getting full.");
    if (fillPct <= 95.0f)
        return PBC_Localize("Your bags are almost full.");
    if (fillPct < 100.0f)
        return PBC_Localize("Your bags are nearly full.");
    return PBC_Localize("Your bags are completely full.");
}

// ---------------------------------------------------------------------------
// PBC_BuildEquipmentStr  (main-thread only)
//
// Builds a single-line human-readable equipment summary:
//
//   You have {quality} equipment made of {material}. Your main weapon is a
//   {rarity} {type}[ called {name}]. In your off-hand you wield a {rarity}
//   {type}[ called {name}]. Your ranged weapon is a {rarity} {type}[ called
//   {name}].
//
// Weapon names are included only for rare+ items.  Sentences are omitted when
// the corresponding slot is empty (e.g. no off-hand, no ranged weapon).
// A bag-space summary is appended when bags are ≥40% full.
// ---------------------------------------------------------------------------

std::string PBC_BuildEquipmentStr(Player* bot)
{
    // --- Armor assessment ------------------------------------------------
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

    // Armor line
    std::string armorLine;

    if (totalArmor == 0)
    {
        armorLine = PBC_Localize("You have no armor.");
    }
    else
    {
        struct Tier { int count; std::string adj; };
        Tier tiers[] = {
            { poorCount,       PBC_Localize("simple") },
            { uncommonCount,   PBC_Localize("modest") },
            { rareCount,       PBC_Localize("fine") },
            { epicCount,       PBC_Localize("excellent") },
            { legendaryCount,  PBC_Localize("exceptional") }
        };

        int maxCount = 0;
        std::string qualityAdj = PBC_Localize("simple");
        for (const auto& t : tiers)
        {
            if (t.count >= maxCount && t.count > 0)
            {
                maxCount = t.count;
                qualityAdj = t.adj;
            }
        }

        std::string material;
        int matMax = 0;
        struct Mat { int count; std::string name; };
        Mat mats[] = {
            {clothCount,   PBC_Localize("cloth")},
            {leatherCount, PBC_Localize("leather")},
            {mailCount,    PBC_Localize("mail")},
            {plateCount,   PBC_Localize("plate")}
        };
        for (const auto& m : mats)
        {
            if (m.count > matMax)
            {
                matMax = m.count;
                material = m.name;
            }
        }

        armorLine = PBC_Localize("You have {0} equipment.", qualityAdj);
        if (!material.empty() && matMax >= 2)
            armorLine = PBC_Localize("You have {0} equipment made of {1}.", qualityAdj, material);
    }

    // --- Weapon assessment -----------------------------------------------
    Item* mainItem  = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    Item* offItem   = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    Item* rangeItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);

    ItemTemplate const* mhT = mainItem  ? mainItem->GetTemplate()  : nullptr;
    ItemTemplate const* ohT = offItem   ? offItem->GetTemplate()   : nullptr;
    ItemTemplate const* rgT = rangeItem ? rangeItem->GetTemplate() : nullptr;

    bool is2H = mhT && mhT->InventoryType == INVTYPE_2HWEAPON;

    std::vector<std::string> lines;
    lines.push_back(armorLine);

    // Main weapon line
    if (mhT)
    {
        std::string rarity = EquipQualityStr(mhT->Quality);
        std::string type   = (mhT->Class == ITEM_CLASS_WEAPON)
                                ? PBC_WeaponTypeStr(mhT->SubClass)
                                : EquipItemTypeStr(mhT);

        if (mhT->Quality >= ITEM_QUALITY_RARE)
            lines.push_back(PBC_Localize("Your main weapon is a {0} {1} called {2}.", rarity, type, PBC_GetItemName(mhT->ItemId)));
        else
            lines.push_back(PBC_Localize("Your main weapon is a {0}.", type));
    }

    // Off-hand line (suppressed when the main-hand weapon is two-handed)
    if (!is2H && ohT)
    {
        std::string rarity = EquipQualityStr(ohT->Quality);
        std::string type   = (ohT->Class == ITEM_CLASS_WEAPON)
                                ? PBC_WeaponTypeStr(ohT->SubClass)
                                : EquipItemTypeStr(ohT);

        if (ohT->Quality >= ITEM_QUALITY_RARE)
            lines.push_back(PBC_Localize("In your off-hand you wield a {0} {1} called {2}.", rarity, type, PBC_GetItemName(ohT->ItemId)));
        else
            lines.push_back(PBC_Localize("In your off-hand you wield a {0}.", type));
    }

    // Ranged weapon / relic line
    if (rgT)
    {
        std::string rarity = EquipQualityStr(rgT->Quality);
        std::string type   = (rgT->Class == ITEM_CLASS_WEAPON)
                                ? PBC_WeaponTypeStr(rgT->SubClass)
                                : EquipItemTypeStr(rgT);

        if (rgT->Quality >= ITEM_QUALITY_RARE)
            lines.push_back(PBC_Localize("Your ranged weapon is a {0} {1} called {2}.", rarity, type, PBC_GetItemName(rgT->ItemId)));
        else
            lines.push_back(PBC_Localize("Your ranged weapon is a {0}.", type));
    }

    // Assemble: single line with sentences separated by spaces
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0) result += " ";
        result += lines[i];
    }

    // Append bag space summary when noteworthy (≥40% full)
    std::string bagSpace = BuildBagSpaceStr(bot);
    if (!bagSpace.empty())
        result += " " + bagSpace;

    return result;
}
