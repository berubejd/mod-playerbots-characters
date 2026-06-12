#include "pbc_equipment_helpers.h"
#include "pbc_item_helpers.h"
#include "pbc_utils.h"
#include "pbc_locales.h"
#include "Player.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Bag.h"
#include <fmt/core.h>

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

// Returns a human-readable off-hand type string (shield, relic, holdable, etc.)
static std::string EquipOffhandTypeStr(ItemTemplate const* tmpl)
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

// Builds a phrase like "a rare dagger" or "an epic staff called Devastation".
// For rare+ items the name is included.
static std::string DescribeWeapon(ItemTemplate const* tmpl)
{
    if (!tmpl) return "";
    std::string type = PBC_WeaponTypeStr(tmpl->SubClass);
    if (tmpl->Quality >= ITEM_QUALITY_RARE)
    {
        std::string qual = EquipQualityStr(tmpl->Quality);
        return std::string(PBC_ArticleFor(qual)) + " " + qual + " " + type + " called " + PBC_GetItemName(tmpl->ItemId);
    }
    return std::string(PBC_ArticleFor(type)) + " " + type;
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
        return std::string(PBC_ArticleFor(qual)) + " " + qual + " " + type + " called " + PBC_GetItemName(tmpl->ItemId);
    }
    return std::string(PBC_ArticleFor(type)) + " " + type;
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
// ---------------------------------------------------------------------------

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
        armorDesc = PBC_Localize("You have no armor");
    }
    else
    {
        // Find dominant quality tier; on ties the highest quality wins
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

        // Find dominant armor material (cloth/leather/mail/plate)
        std::string material;
        int matMax = 0;
        struct Mat { int count; std::string name; };
        Mat mats[] = {
            {clothCount, PBC_Localize("cloth")},
            {leatherCount, PBC_Localize("leather")},
            {mailCount, PBC_Localize("mail")},
            {plateCount, PBC_Localize("plate")}
        };
        for (const auto& m : mats)
        {
            if (m.count > matMax)
            {
                matMax = m.count;
                material = m.name;
            }
        }

        armorDesc = PBC_Localize("You have {0} equipment", qualityAdj);
        if (!material.empty() && matMax >= 2)
            armorDesc += PBC_Localize(" made of {0}", material);
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
        weaponDesc = PBC_Localize("you are unarmed");
    }
    else if (is2H)
    {
        weaponDesc = PBC_Localize("you wield ") + DescribeWeapon(mhT);
        if (rgIsWeapon)
            weaponDesc += PBC_Localize(" and carry ") + DescribeWeapon(rgT);
    }
    else if (mhT && ohT)
    {
        // Dual wield of same weapon type, both rare+: special phrasing
        if (ohIsWeapon && mhIsWeapon && mhT->SubClass == ohT->SubClass &&
            mhT->Quality >= ITEM_QUALITY_RARE && ohT->Quality >= ITEM_QUALITY_RARE)
        {
            std::string qual = EquipQualityStr(mhT->Quality);
            std::string type = PBC_WeaponTypeStr(mhT->SubClass);
            weaponDesc = PBC_Localize("you wield two {0} {1}s, called {2} and {3}", qual, type, PBC_GetItemName(mhT->ItemId), PBC_GetItemName(ohT->ItemId));
        }
        else
        {
            std::string mhDesc = mhIsWeapon ? DescribeWeapon(mhT) : DescribeOffhand(mhT);
            std::string ohDesc = ohIsWeapon ? DescribeWeapon(ohT) : DescribeOffhand(ohT);
            weaponDesc = PBC_Localize("you wield ") + mhDesc + PBC_Localize(" and ") + ohDesc;
        }
        if (rgIsWeapon)
            weaponDesc += PBC_Localize(", and carry ") + DescribeWeapon(rgT);
    }
    else if (mhT)
    {
        weaponDesc = PBC_Localize("you wield ") + (mhIsWeapon ? DescribeWeapon(mhT) : DescribeOffhand(mhT));
        if (rgIsWeapon)
            weaponDesc += PBC_Localize(" and carry ") + DescribeWeapon(rgT);
    }
    else if (ohT)
    {
        weaponDesc = PBC_Localize("you carry ") + DescribeOffhand(ohT);
    }
    else
    {
        // Only ranged
        weaponDesc = PBC_Localize("you carry ") + DescribeWeapon(rgT);
    }

    std::string result = armorDesc + PBC_Localize(", and ") + weaponDesc + ".";

    // Append bag space summary when noteworthy (≥40% full)
    std::string bagSpace = BuildBagSpaceStr(bot);
    if (!bagSpace.empty())
        result += " " + bagSpace;

    return result;
}
