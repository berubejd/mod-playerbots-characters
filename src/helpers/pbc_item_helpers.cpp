#include "pbc_item_helpers.h"
#include "ItemTemplate.h"
#include "SharedDefines.h"

#include <cctype>

// ---------------------------------------------------------------------------
// Article helper
// ---------------------------------------------------------------------------

const char* PBC_ArticleFor(const std::string& word)
{
    if (word.empty()) return "a";
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(word[0])));
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an" : "a";
}

// ---------------------------------------------------------------------------
// Quality
// ---------------------------------------------------------------------------

std::string PBC_ItemQualityStr(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_UNCOMMON: return "uncommon";
        case ITEM_QUALITY_RARE:     return "rare";
        case ITEM_QUALITY_EPIC:     return "epic";
        case ITEM_QUALITY_LEGENDARY:return "legendary";
        case ITEM_QUALITY_ARTIFACT: return "artifact";
        case ITEM_QUALITY_HEIRLOOM: return "heirloom";
        default:                    return "rare";
    }
}

// ---------------------------------------------------------------------------
// Weapon
// ---------------------------------------------------------------------------

std::string PBC_WeaponTypeStr(uint32 subClass)
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
        case ITEM_SUBCLASS_WEAPON_SPEAR:    return "spear";
        default:                            return "weapon";
    }
}

// ---------------------------------------------------------------------------
// Armor
// ---------------------------------------------------------------------------

std::string PBC_ArmorSlotStr(uint32 inventoryType)
{
    switch (inventoryType)
    {
        case INVTYPE_HEAD:       return "helm";
        case INVTYPE_NECK:       return "necklace";
        case INVTYPE_SHOULDERS:  return "shoulders";
        case INVTYPE_BODY:       return "shirt";
        case INVTYPE_CHEST:      return "chest armor";
        case INVTYPE_WAIST:      return "belt";
        case INVTYPE_LEGS:       return "legguards";
        case INVTYPE_FEET:       return "boots";
        case INVTYPE_WRISTS:     return "bracers";
        case INVTYPE_HANDS:      return "gloves";
        case INVTYPE_FINGER:     return "ring";
        case INVTYPE_TRINKET:    return "trinket";
        case INVTYPE_CLOAK:      return "cloak";
        case INVTYPE_TABARD:     return "tabard";
        case INVTYPE_ROBE:       return "robe";
        case INVTYPE_HOLDABLE:   return "off-hand item";
        case INVTYPE_SHIELD:     return "shield";
        case INVTYPE_RELIC:      return "relic";
        default:                 return "armor";
    }
}

std::string PBC_BuildArmorTypeStr(uint32 subClass, uint32 inventoryType)
{
    // Shields, bucklers and relics have their own distinct names
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARMOR_SHIELD:  return "shield";
        case ITEM_SUBCLASS_ARMOR_BUCKLER: return "buckler";
        case ITEM_SUBCLASS_ARMOR_LIBRAM:  return "libram";
        case ITEM_SUBCLASS_ARMOR_IDOL:    return "idol";
        case ITEM_SUBCLASS_ARMOR_TOTEM:   return "totem";
        case ITEM_SUBCLASS_ARMOR_SIGIL:   return "sigil";
        default: break;
    }

    // Get the slot name from inventory type
    std::string slot = PBC_ArmorSlotStr(inventoryType);

    // Cloth / leather / mail / plate: combine material + slot
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARMOR_CLOTH:   return "cloth " + slot;
        case ITEM_SUBCLASS_ARMOR_LEATHER: return "leather " + slot;
        case ITEM_SUBCLASS_ARMOR_MAIL:    return "mail " + slot;
        case ITEM_SUBCLASS_ARMOR_PLATE:   return "plate " + slot;
        default: break;
    }

    // Misc subclass: just use the slot name (rings, trinkets, cloaks, etc.)
    return slot;
}

// ---------------------------------------------------------------------------
// Consumable
// ---------------------------------------------------------------------------

std::string PBC_ConsumableTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_POTION:           return "potion";
        case ITEM_SUBCLASS_ELIXIR:           return "elixir";
        case ITEM_SUBCLASS_FLASK:            return "flask";
        case ITEM_SUBCLASS_SCROLL:           return "scroll";
        case ITEM_SUBCLASS_FOOD:             return "food";
        case ITEM_SUBCLASS_ITEM_ENHANCEMENT: return "item enhancement";
        case ITEM_SUBCLASS_BANDAGE:          return "bandage";
        default:                             return "consumable";
    }
}

// ---------------------------------------------------------------------------
// Gem
// ---------------------------------------------------------------------------

std::string PBC_GemTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_GEM_RED:       return "red gem";
        case ITEM_SUBCLASS_GEM_BLUE:      return "blue gem";
        case ITEM_SUBCLASS_GEM_YELLOW:    return "yellow gem";
        case ITEM_SUBCLASS_GEM_PURPLE:    return "purple gem";
        case ITEM_SUBCLASS_GEM_GREEN:     return "green gem";
        case ITEM_SUBCLASS_GEM_ORANGE:    return "orange gem";
        case ITEM_SUBCLASS_GEM_META:      return "meta gem";
        case ITEM_SUBCLASS_GEM_PRISMATIC: return "prismatic gem";
        default:                          return "gem";
    }
}

// ---------------------------------------------------------------------------
// Recipe
// ---------------------------------------------------------------------------

std::string PBC_RecipeTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_BOOK:                   return "book";
        case ITEM_SUBCLASS_LEATHERWORKING_PATTERN: return "leatherworking pattern";
        case ITEM_SUBCLASS_TAILORING_PATTERN:      return "tailoring pattern";
        case ITEM_SUBCLASS_ENGINEERING_SCHEMATIC:  return "engineering schematic";
        case ITEM_SUBCLASS_BLACKSMITHING:          return "blacksmithing plans";
        case ITEM_SUBCLASS_COOKING_RECIPE:         return "cooking recipe";
        case ITEM_SUBCLASS_ALCHEMY_RECIPE:         return "alchemy recipe";
        case ITEM_SUBCLASS_FIRST_AID_MANUAL:       return "first aid manual";
        case ITEM_SUBCLASS_ENCHANTING_FORMULA:     return "enchanting formula";
        case ITEM_SUBCLASS_FISHING_MANUAL:         return "fishing manual";
        case ITEM_SUBCLASS_JEWELCRAFTING_RECIPE:   return "jewelcrafting recipe";
        default:                                   return "recipe";
    }
}

// ---------------------------------------------------------------------------
// Trade goods
// ---------------------------------------------------------------------------

std::string PBC_TradeGoodsTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_TRADE_GOODS:       return "trade goods";
        case ITEM_SUBCLASS_PARTS:             return "engineering parts";
        case ITEM_SUBCLASS_EXPLOSIVES:        return "explosives";
        case ITEM_SUBCLASS_DEVICES:           return "device";
        case ITEM_SUBCLASS_JEWELCRAFTING:     return "jewelcrafting material";
        case ITEM_SUBCLASS_CLOTH:             return "cloth";
        case ITEM_SUBCLASS_LEATHER:           return "leather";
        case ITEM_SUBCLASS_METAL_STONE:       return "metal and stone";
        case ITEM_SUBCLASS_MEAT:              return "meat";
        case ITEM_SUBCLASS_HERB:              return "herb";
        case ITEM_SUBCLASS_ELEMENTAL:         return "elemental item";
        case ITEM_SUBCLASS_TRADE_GOODS_OTHER: return "trade goods";
        case ITEM_SUBCLASS_ENCHANTING:        return "enchanting material";
        case ITEM_SUBCLASS_MATERIAL:          return "material";
        default:                              return "trade goods";
    }
}

// ---------------------------------------------------------------------------
// Projectile
// ---------------------------------------------------------------------------

std::string PBC_ProjectileTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARROW:  return "arrow";
        case ITEM_SUBCLASS_BULLET: return "bullet";
        default:                   return "ammunition";
    }
}

// ---------------------------------------------------------------------------
// Container
// ---------------------------------------------------------------------------

std::string PBC_ContainerTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_CONTAINER:               return "bag";
        case ITEM_SUBCLASS_SOUL_CONTAINER:          return "soul bag";
        case ITEM_SUBCLASS_HERB_CONTAINER:          return "herb bag";
        case ITEM_SUBCLASS_ENCHANTING_CONTAINER:    return "enchanting bag";
        case ITEM_SUBCLASS_ENGINEERING_CONTAINER:   return "engineering bag";
        case ITEM_SUBCLASS_GEM_CONTAINER:           return "gem bag";
        case ITEM_SUBCLASS_MINING_CONTAINER:        return "mining bag";
        case ITEM_SUBCLASS_LEATHERWORKING_CONTAINER:return "leatherworking bag";
        case ITEM_SUBCLASS_INSCRIPTION_CONTAINER:   return "inscription bag";
        default:                                    return "bag";
    }
}

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

std::string PBC_KeyTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_KEY:      return "key";
        case ITEM_SUBCLASS_LOCKPICK: return "lockpick";
        default:                     return "key";
    }
}

// ---------------------------------------------------------------------------
// Quiver
// ---------------------------------------------------------------------------

std::string PBC_QuiverTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_QUIVER:     return "quiver";
        case ITEM_SUBCLASS_AMMO_POUCH: return "ammo pouch";
        default:                       return "quiver";
    }
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

std::string PBC_MiscTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_JUNK:          return "junk item";
        case ITEM_SUBCLASS_JUNK_REAGENT:  return "reagent";
        case ITEM_SUBCLASS_JUNK_PET:      return "pet";
        case ITEM_SUBCLASS_JUNK_HOLIDAY:  return "holiday item";
        case ITEM_SUBCLASS_JUNK_OTHER:    return "miscellaneous item";
        case ITEM_SUBCLASS_JUNK_MOUNT:    return "mount";
        default:                          return "item";
    }
}

// ---------------------------------------------------------------------------
// Composite phrase builder
// ---------------------------------------------------------------------------

std::string PBC_BuildItemPhrase(ItemTemplate const* tmpl)
{
    if (!tmpl) return "an item";

    std::string quality = PBC_ItemQualityStr(tmpl->Quality);
    std::string type;

    switch (tmpl->Class)
    {
        case ITEM_CLASS_WEAPON:      type = PBC_WeaponTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_ARMOR:       type = PBC_BuildArmorTypeStr(tmpl->SubClass, tmpl->InventoryType); break;
        case ITEM_CLASS_CONSUMABLE:  type = PBC_ConsumableTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_GEM:         type = PBC_GemTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_RECIPE:      type = PBC_RecipeTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_TRADE_GOODS: type = PBC_TradeGoodsTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_PROJECTILE:  type = PBC_ProjectileTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_CONTAINER:   type = PBC_ContainerTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_KEY:         type = PBC_KeyTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_QUIVER:      type = PBC_QuiverTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_MISC:        type = PBC_MiscTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_QUEST:       type = "quest item"; break;
        case ITEM_CLASS_REAGENT:     type = "reagent"; break;
        case ITEM_CLASS_GLYPH:       type = "glyph"; break;
        default:                     type = "item"; break;
    }

    return std::string(PBC_ArticleFor(quality)) + " " + quality + " " + type;
}
