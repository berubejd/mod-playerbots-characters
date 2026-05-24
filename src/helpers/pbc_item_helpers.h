#ifndef MOD_PBC_ITEM_HELPERS_H
#define MOD_PBC_ITEM_HELPERS_H

#include <string>
#include "Define.h"

struct ItemTemplate;

// ---------------------------------------------------------------------------
// Shared item description helpers
//
// Pure string builders that turn ItemTemplate fields into human-readable
// phrases like "an epic cloth robe", "a rare dagger", "an uncommon potion".
// No game-object dependencies beyond ItemTemplate — safe to call from any
// thread as long as the template pointer remains valid (templates are
// immutable after load).
// ---------------------------------------------------------------------------

// Returns "a" or "an" based on the first character of the word that follows.
const char* PBC_ArticleFor(const std::string& word);

// Human-readable item quality name ("common", "uncommon", "rare", …).
std::string PBC_ItemQualityStr(uint32 quality);

// Human-readable weapon sub-class ("one-handed axe", "dagger", …).
std::string PBC_WeaponTypeStr(uint32 subClass);

// Human-readable armor slot from InventoryType ("helm", "boots", …).
std::string PBC_ArmorSlotStr(uint32 inventoryType);

// Combines armor material + slot, e.g. "cloth robe", "plate helm".
std::string PBC_BuildArmorTypeStr(uint32 subClass, uint32 inventoryType);

// Human-readable consumable sub-class ("potion", "elixir", …).
std::string PBC_ConsumableTypeStr(uint32 subClass);

// Human-readable gem sub-class ("red gem", "meta gem", …).
std::string PBC_GemTypeStr(uint32 subClass);

// Human-readable recipe sub-class ("alchemy recipe", "cooking recipe", …).
std::string PBC_RecipeTypeStr(uint32 subClass);

// Human-readable trade-goods sub-class ("herb", "cloth", …).
std::string PBC_TradeGoodsTypeStr(uint32 subClass);

// Human-readable projectile sub-class ("arrow", "bullet").
std::string PBC_ProjectileTypeStr(uint32 subClass);

// Human-readable container sub-class ("bag", "herb bag", …).
std::string PBC_ContainerTypeStr(uint32 subClass);

// Human-readable key sub-class ("key", "lockpick").
std::string PBC_KeyTypeStr(uint32 subClass);

// Human-readable quiver sub-class ("quiver", "ammo pouch").
std::string PBC_QuiverTypeStr(uint32 subClass);

// Human-readable misc sub-class ("junk item", "mount", "pet", …).
std::string PBC_MiscTypeStr(uint32 subClass);

// Builds a phrase like "a legendary two-handed mace", "an epic cloth robe",
// "a rare ring", "an uncommon potion", etc.
std::string PBC_BuildItemPhrase(ItemTemplate const* tmpl);

#endif // MOD_PBC_ITEM_HELPERS_H
