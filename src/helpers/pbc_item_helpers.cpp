#include "pbc_item_helpers.h"
#include "pbc_locales.h"
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
        case ITEM_QUALITY_UNCOMMON: return PBC_Localize("uncommon");
        case ITEM_QUALITY_RARE:     return PBC_Localize("rare");
        case ITEM_QUALITY_EPIC:     return PBC_Localize("epic");
        case ITEM_QUALITY_LEGENDARY:return PBC_Localize("legendary");
        case ITEM_QUALITY_ARTIFACT: return PBC_Localize("artifact");
        case ITEM_QUALITY_HEIRLOOM: return PBC_Localize("heirloom");
        default:                    return PBC_Localize("rare");
    }
}

// ---------------------------------------------------------------------------
// Weapon
// ---------------------------------------------------------------------------

std::string PBC_WeaponTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_WEAPON_AXE:      return PBC_Localize("one-handed axe");
        case ITEM_SUBCLASS_WEAPON_AXE2:     return PBC_Localize("two-handed axe");
        case ITEM_SUBCLASS_WEAPON_BOW:      return PBC_Localize("bow");
        case ITEM_SUBCLASS_WEAPON_GUN:      return PBC_Localize("gun");
        case ITEM_SUBCLASS_WEAPON_MACE:     return PBC_Localize("one-handed mace");
        case ITEM_SUBCLASS_WEAPON_MACE2:    return PBC_Localize("two-handed mace");
        case ITEM_SUBCLASS_WEAPON_POLEARM:  return PBC_Localize("polearm");
        case ITEM_SUBCLASS_WEAPON_SWORD:    return PBC_Localize("one-handed sword");
        case ITEM_SUBCLASS_WEAPON_SWORD2:   return PBC_Localize("two-handed sword");
        case ITEM_SUBCLASS_WEAPON_STAFF:    return PBC_Localize("staff");
        case ITEM_SUBCLASS_WEAPON_FIST:     return PBC_Localize("fist weapon");
        case ITEM_SUBCLASS_WEAPON_DAGGER:   return PBC_Localize("dagger");
        case ITEM_SUBCLASS_WEAPON_THROWN:   return PBC_Localize("thrown weapon");
        case ITEM_SUBCLASS_WEAPON_CROSSBOW: return PBC_Localize("crossbow");
        case ITEM_SUBCLASS_WEAPON_WAND:     return PBC_Localize("wand");
        case ITEM_SUBCLASS_WEAPON_SPEAR:    return PBC_Localize("spear");
        default:                            return PBC_Localize("weapon");
    }
}

// ---------------------------------------------------------------------------
// Armor
// ---------------------------------------------------------------------------

std::string PBC_ArmorSlotStr(uint32 inventoryType)
{
    switch (inventoryType)
    {
        case INVTYPE_HEAD:       return PBC_Localize("helm");
        case INVTYPE_NECK:       return PBC_Localize("necklace");
        case INVTYPE_SHOULDERS:  return PBC_Localize("shoulders");
        case INVTYPE_BODY:       return PBC_Localize("shirt");
        case INVTYPE_CHEST:      return PBC_Localize("chest armor");
        case INVTYPE_WAIST:      return PBC_Localize("belt");
        case INVTYPE_LEGS:       return PBC_Localize("legguards");
        case INVTYPE_FEET:       return PBC_Localize("boots");
        case INVTYPE_WRISTS:     return PBC_Localize("bracers");
        case INVTYPE_HANDS:      return PBC_Localize("gloves");
        case INVTYPE_FINGER:     return PBC_Localize("ring");
        case INVTYPE_TRINKET:    return PBC_Localize("trinket");
        case INVTYPE_CLOAK:      return PBC_Localize("cloak");
        case INVTYPE_TABARD:     return PBC_Localize("tabard");
        case INVTYPE_ROBE:       return PBC_Localize("robe");
        case INVTYPE_HOLDABLE:   return PBC_Localize("off-hand item");
        case INVTYPE_SHIELD:     return PBC_Localize("shield");
        case INVTYPE_RELIC:      return PBC_Localize("relic");
        default:                 return PBC_Localize("armor");
    }
}

std::string PBC_BuildArmorTypeStr(uint32 subClass, uint32 inventoryType)
{
    // Shields, bucklers and relics have their own distinct names
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARMOR_SHIELD:  return PBC_Localize("shield");
        case ITEM_SUBCLASS_ARMOR_BUCKLER: return PBC_Localize("buckler");
        case ITEM_SUBCLASS_ARMOR_LIBRAM:  return PBC_Localize("libram");
        case ITEM_SUBCLASS_ARMOR_IDOL:    return PBC_Localize("idol");
        case ITEM_SUBCLASS_ARMOR_TOTEM:   return PBC_Localize("totem");
        case ITEM_SUBCLASS_ARMOR_SIGIL:   return PBC_Localize("sigil");
        default: break;
    }

    // Get the slot name from inventory type
    std::string slot = PBC_ArmorSlotStr(inventoryType);

    // Cloth / leather / mail / plate: combine material + slot
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARMOR_CLOTH:   return PBC_Localize("cloth") + " " + slot;
        case ITEM_SUBCLASS_ARMOR_LEATHER: return PBC_Localize("leather") + " " + slot;
        case ITEM_SUBCLASS_ARMOR_MAIL:    return PBC_Localize("mail") + " " + slot;
        case ITEM_SUBCLASS_ARMOR_PLATE:   return PBC_Localize("plate") + " " + slot;
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
        case ITEM_SUBCLASS_POTION:           return PBC_Localize("potion");
        case ITEM_SUBCLASS_ELIXIR:           return PBC_Localize("elixir");
        case ITEM_SUBCLASS_FLASK:            return PBC_Localize("flask");
        case ITEM_SUBCLASS_SCROLL:           return PBC_Localize("scroll");
        case ITEM_SUBCLASS_FOOD:             return PBC_Localize("food");
        case ITEM_SUBCLASS_ITEM_ENHANCEMENT: return PBC_Localize("item enhancement");
        case ITEM_SUBCLASS_BANDAGE:          return PBC_Localize("bandage");
        default:                             return PBC_Localize("consumable");
    }
}

// ---------------------------------------------------------------------------
// Gem
// ---------------------------------------------------------------------------

std::string PBC_GemTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_GEM_RED:       return PBC_Localize("red gem");
        case ITEM_SUBCLASS_GEM_BLUE:      return PBC_Localize("blue gem");
        case ITEM_SUBCLASS_GEM_YELLOW:    return PBC_Localize("yellow gem");
        case ITEM_SUBCLASS_GEM_PURPLE:    return PBC_Localize("purple gem");
        case ITEM_SUBCLASS_GEM_GREEN:     return PBC_Localize("green gem");
        case ITEM_SUBCLASS_GEM_ORANGE:    return PBC_Localize("orange gem");
        case ITEM_SUBCLASS_GEM_META:      return PBC_Localize("meta gem");
        case ITEM_SUBCLASS_GEM_PRISMATIC: return PBC_Localize("prismatic gem");
        default:                          return PBC_Localize("gem");
    }
}

// ---------------------------------------------------------------------------
// Recipe
// ---------------------------------------------------------------------------

std::string PBC_RecipeTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_BOOK:                   return PBC_Localize("book");
        case ITEM_SUBCLASS_LEATHERWORKING_PATTERN: return PBC_Localize("leatherworking pattern");
        case ITEM_SUBCLASS_TAILORING_PATTERN:      return PBC_Localize("tailoring pattern");
        case ITEM_SUBCLASS_ENGINEERING_SCHEMATIC:  return PBC_Localize("engineering schematic");
        case ITEM_SUBCLASS_BLACKSMITHING:          return PBC_Localize("blacksmithing plans");
        case ITEM_SUBCLASS_COOKING_RECIPE:         return PBC_Localize("cooking recipe");
        case ITEM_SUBCLASS_ALCHEMY_RECIPE:         return PBC_Localize("alchemy recipe");
        case ITEM_SUBCLASS_FIRST_AID_MANUAL:       return PBC_Localize("first aid manual");
        case ITEM_SUBCLASS_ENCHANTING_FORMULA:     return PBC_Localize("enchanting formula");
        case ITEM_SUBCLASS_FISHING_MANUAL:         return PBC_Localize("fishing manual");
        case ITEM_SUBCLASS_JEWELCRAFTING_RECIPE:   return PBC_Localize("jewelcrafting recipe");
        default:                                   return PBC_Localize("recipe");
    }
}

// ---------------------------------------------------------------------------
// Trade goods
// ---------------------------------------------------------------------------

std::string PBC_TradeGoodsTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_TRADE_GOODS:       return PBC_Localize("trade goods");
        case ITEM_SUBCLASS_PARTS:             return PBC_Localize("engineering parts");
        case ITEM_SUBCLASS_EXPLOSIVES:        return PBC_Localize("explosives");
        case ITEM_SUBCLASS_DEVICES:           return PBC_Localize("device");
        case ITEM_SUBCLASS_JEWELCRAFTING:     return PBC_Localize("jewelcrafting material");
        case ITEM_SUBCLASS_CLOTH:             return PBC_Localize("cloth");
        case ITEM_SUBCLASS_LEATHER:           return PBC_Localize("leather");
        case ITEM_SUBCLASS_METAL_STONE:       return PBC_Localize("metal and stone");
        case ITEM_SUBCLASS_MEAT:              return PBC_Localize("meat");
        case ITEM_SUBCLASS_HERB:              return PBC_Localize("herb");
        case ITEM_SUBCLASS_ELEMENTAL:         return PBC_Localize("elemental item");
        case ITEM_SUBCLASS_TRADE_GOODS_OTHER: return PBC_Localize("trade goods");
        case ITEM_SUBCLASS_ENCHANTING:        return PBC_Localize("enchanting material");
        case ITEM_SUBCLASS_MATERIAL:          return PBC_Localize("material");
        default:                              return PBC_Localize("trade goods");
    }
}

// ---------------------------------------------------------------------------
// Projectile
// ---------------------------------------------------------------------------

std::string PBC_ProjectileTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARROW:  return PBC_Localize("arrow");
        case ITEM_SUBCLASS_BULLET: return PBC_Localize("bullet");
        default:                   return PBC_Localize("ammunition");
    }
}

// ---------------------------------------------------------------------------
// Container
// ---------------------------------------------------------------------------

std::string PBC_ContainerTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_CONTAINER:               return PBC_Localize("bag");
        case ITEM_SUBCLASS_SOUL_CONTAINER:          return PBC_Localize("soul bag");
        case ITEM_SUBCLASS_HERB_CONTAINER:          return PBC_Localize("herb bag");
        case ITEM_SUBCLASS_ENCHANTING_CONTAINER:    return PBC_Localize("enchanting bag");
        case ITEM_SUBCLASS_ENGINEERING_CONTAINER:   return PBC_Localize("engineering bag");
        case ITEM_SUBCLASS_GEM_CONTAINER:           return PBC_Localize("gem bag");
        case ITEM_SUBCLASS_MINING_CONTAINER:        return PBC_Localize("mining bag");
        case ITEM_SUBCLASS_LEATHERWORKING_CONTAINER:return PBC_Localize("leatherworking bag");
        case ITEM_SUBCLASS_INSCRIPTION_CONTAINER:   return PBC_Localize("inscription bag");
        default:                                    return PBC_Localize("bag");
    }
}

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

std::string PBC_KeyTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_KEY:      return PBC_Localize("key");
        case ITEM_SUBCLASS_LOCKPICK: return PBC_Localize("lockpick");
        default:                     return PBC_Localize("key");
    }
}

// ---------------------------------------------------------------------------
// Quiver
// ---------------------------------------------------------------------------

std::string PBC_QuiverTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_QUIVER:     return PBC_Localize("quiver");
        case ITEM_SUBCLASS_AMMO_POUCH: return PBC_Localize("ammo pouch");
        default:                       return PBC_Localize("quiver");
    }
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

std::string PBC_MiscTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_JUNK:          return PBC_Localize("junk item");
        case ITEM_SUBCLASS_JUNK_REAGENT:  return PBC_Localize("reagent");
        case ITEM_SUBCLASS_JUNK_PET:      return PBC_Localize("pet");
        case ITEM_SUBCLASS_JUNK_HOLIDAY:  return PBC_Localize("holiday item");
        case ITEM_SUBCLASS_JUNK_OTHER:    return PBC_Localize("miscellaneous item");
        case ITEM_SUBCLASS_JUNK_MOUNT:    return PBC_Localize("mount");
        default:                          return PBC_Localize("item");
    }
}

// ---------------------------------------------------------------------------
// Composite phrase builder
// ---------------------------------------------------------------------------

std::string PBC_BuildItemPhrase(ItemTemplate const* tmpl)
{
    if (!tmpl) return PBC_Localize("an item");

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
        case ITEM_CLASS_QUEST:       type = PBC_Localize("quest item"); break;
        case ITEM_CLASS_REAGENT:     type = PBC_Localize("reagent"); break;
        case ITEM_CLASS_GLYPH:       type = "glyph"; break;
        default:                     type = PBC_Localize("item"); break;
    }

    return PBC_Localize("a {0} {1}", quality, type);
}
