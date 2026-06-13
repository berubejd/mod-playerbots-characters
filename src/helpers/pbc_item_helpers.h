#ifndef MOD_PBC_ITEM_HELPERS_H
#define MOD_PBC_ITEM_HELPERS_H

#include <string>
#include "Define.h"

struct ItemTemplate;

// ---------------------------------------------------------------------------
// Shared item description helpers
//
// Pure string builders that turn ItemTemplate fields into human-readable
// phrases for weapons and armour — the only item classes that trigger
// loot/quest-reward events (see PBC_LOOT_EVENT_ITEM_CLASSES).
// No game-object dependencies beyond ItemTemplate — safe to call from any
// thread as long as the template pointer remains valid (templates are
// immutable after load).
// ---------------------------------------------------------------------------

// Human-readable item quality name ("common", "uncommon", "rare", …).
std::string PBC_ItemQualityStr(uint32 quality);

// Human-readable weapon sub-class ("one-handed axe", "dagger", …).
std::string PBC_WeaponTypeStr(uint32 subClass);

// Human-readable armor slot from InventoryType ("helm", "boots", …).
std::string PBC_ArmorSlotStr(uint32 inventoryType);

// Combines armor material + slot, e.g. "cloth robe", "plate helm".
std::string PBC_BuildArmorTypeStr(uint32 subClass, uint32 inventoryType);

// Builds a phrase like "a legendary two-handed mace", "an epic cloth robe",
// "a rare ring".  Only weapons and armour are handled explicitly; anything
// else falls back to a generic "a {quality} item".
std::string PBC_BuildItemPhrase(ItemTemplate const* tmpl);

#endif // MOD_PBC_ITEM_HELPERS_H
