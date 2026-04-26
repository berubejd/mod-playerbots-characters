#include "pbc_events.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_llm.h"
#include "pbc_utils.h"
#include "Log.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Item.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "GridNotifiers.h"
#include "CellImpl.h"
#include "SharedDefines.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Chat.h"
#include "DBCStores.h"

#include <algorithm>
#include <regex>
#include <unordered_set>
#include <random>

// ---------------------------------------------------------------------------
// Pointer sanity guard
//
// On a 64-bit system, valid heap/stack pointers are always well above the
// first 64 KiB of address space.  Values in that range are garbage that the
// playerbots module occasionally passes to hooks during bot packet processing.
// ---------------------------------------------------------------------------
#define PBC_PTR_VALID(p) (reinterpret_cast<uintptr_t>(p) > 0xFFFFu)

// ---------------------------------------------------------------------------
// Strip WoW item/object link markup, keeping only the display name.
// ---------------------------------------------------------------------------
static std::string SanitizeChatMessage(const std::string& msg)
{
    static const std::regex linkPattern(
        R"((?:\|c[0-9a-fA-F]{8})?\|H[^|]+\|h(\[[^\]]*\])\|h(?:\|r)?)",
        std::regex::optimize);

    std::string result;
    result.reserve(msg.size());

    auto it  = msg.cbegin();
    auto end = msg.cend();
    std::smatch m;

    while (std::regex_search(it, end, m, linkPattern))
    {
        result.append(it, m.prefix().second);
        result += m[1].str();
        it = m.suffix().first;
    }
    result.append(it, end);
    return result;
}

// ---------------------------------------------------------------------------
// Narrator formatting helpers
// ---------------------------------------------------------------------------

std::string PBC_MakeEventLine(const std::string& text) { return "*" + text + "*"; }
std::string PBC_MakeHistLine(const std::string& text)  { return "Narrator: *" + text + "*"; }

// ---------------------------------------------------------------------------
// PBC_NotifyRealPlayersInGroup
//
// Sends the current event text as a system message to all real (non-bot)
// players in the same group as 'anchor'.  Called at event dispatch time
// when PBC.DisplayNarratorEvents is enabled, before bots process the event.
// For bot anchors (e.g. location events) the function walks the bot's group
// so real players in that group still receive the notification.
// ---------------------------------------------------------------------------
void PBC_NotifyRealPlayersInGroup(Player* anchor, const std::string& eventLine)
{
    if (!g_PBC_DisplayNarratorEvents) return;
    if (!PBC_PTR_VALID(anchor) || eventLine.empty()) return;

    auto sendTo = [&](Player* p)
    {
        if (!PBC_PTR_VALID(p) || !p->IsInWorld()) return;
        WorldSession* sess = p->GetSession();
        if (!PBC_PTR_VALID(sess) || sess->IsBot()) return;
        ChatHandler(sess).SendSysMessage(eventLine);
    };

    Group* grp = anchor->GetGroup();
    if (grp)
    {
        for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            sendTo(ref->GetSource());
    }
    else
    {
        sendTo(anchor);
    }
}

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

static bool IsBlacklisted(const std::string& msg)
{
    for (const auto& prefix : g_PBC_Blacklist)
        if (!prefix.empty() && msg.rfind(prefix, 0) == 0)
            return true;
    return false;
}


static std::vector<Player*> FindNearbyBots(WorldObject* source, float range = 60.0f)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(source) || !source->GetMap()) return bots;

    auto doWork = [&](Player* p)
    {
        if (!PBC_PTR_VALID(p) || p == source) return;
        if (!p->IsInWorld()) return;
        if (!p->GetSession() || !p->GetSession()->IsBot()) return;
        if (p->IsWithinDist(source, range))
            bots.push_back(p);
    };
    Acore::PlayerDistWorker<decltype(doWork)> worker(source, range, doWork);
    Cell::VisitObjects(source, worker, range);
    return bots;
}

static std::vector<Player*> FindGroupBots(Player* player)
{
    std::vector<Player*> bots;
    if (!PBC_PTR_VALID(player)) return bots;

    Group* grp = player->GetGroup();
    if (!grp) return bots;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || member == player) continue;
        if (!member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) continue;
        bots.push_back(member);
    }
    return bots;
}

static bool BotIsGroupedWithRealPlayer(Player* bot)
{
    if (!PBC_PTR_VALID(bot)) return false;
    Group* grp = bot->GetGroup();
    if (!grp) return false;
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || !member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) return true;
    }
    return false;
}

static bool MentionsCharacter(const std::string& msg, const std::string& charName)
{
    std::string lower = msg, lname = charName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    return lower.find(lname) != std::string::npos;
}

// ---------------------------------------------------------------------------
// BuildCharacterEvent
//
// Rolls chance for each character in the provided list, builds snapshots for
// responders, collects GUIDs for silent characters, and returns a ready-to-push
// PBC_EventItem.  chatType and combatSkip are passed through.
// ---------------------------------------------------------------------------
static PBC_EventItem BuildCharacterEvent(const std::vector<Player*>& bots,
                                    const std::string& eventLine,
                                    const std::string& histLine,
                                    uint32_t chance,
                                    uint32_t chatType,
                                    bool canCreateEvents = false)
{
    PBC_EventItem ev;
    ev.type                  = PBC_EventType::Normal;
    ev.eventLine             = eventLine;
    ev.histLine              = histLine;
    ev.chatType              = chatType;
    ev.canCreateEvents       = canCreateEvents;

    for (Player* bot : bots)
    {
        uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), chance);
        bool rolled = PBC_RollChance(effectiveChance);
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] Roll event character={} chance={}% (base={}% mod={}) -> {}",
                     bot->GetName(), effectiveChance, chance,
                     static_cast<int32_t>(effectiveChance) - static_cast<int32_t>(chance),
                     rolled ? "RESPOND" : "silent");
        if (rolled)
            ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
        else
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
    }

    return ev;
}

// ---------------------------------------------------------------------------
// PBC_DispatchGroupEvent
// ---------------------------------------------------------------------------

void PBC_DispatchGroupEvent(Player* anchor, const std::string& eventLine,
                             const std::string& histLine, uint32_t chance,
                             bool notifyRealPlayers)
{
    if (!PBC_PTR_VALID(anchor)) return;

    WorldSession* anchorSess = anchor->GetSession();
    bool anchorIsReal = PBC_PTR_VALID(anchorSess) && !anchorSess->IsBot();
    bool anchorIsBot  = PBC_PTR_VALID(anchorSess) && anchorSess->IsBot();

    if (!anchorIsReal && !BotIsGroupedWithRealPlayer(anchor))
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] DispatchGroupEvent: skipped — no real player in group (anchor={})", anchor->GetName());
        return;
    }

    if (notifyRealPlayers)
        PBC_NotifyRealPlayersInGroup(anchor, eventLine);

    auto bots = FindGroupBots(anchor);
    if (bots.empty() && !anchorIsBot) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] DispatchGroupEvent: anchor={} bots={} chance={}% event=\"{}\"",
                 anchor->GetName(), bots.size(), chance, eventLine);

    PBC_EventItem ev = BuildCharacterEvent(bots, eventLine, histLine, chance, CHAT_MSG_PARTY,
                                     /*canCreateEvents=*/true);

    // If the anchor is itself a bot (e.g. a bot leveling up), it was excluded
    // from FindGroupBots() but still needs to receive the histLine and any
    // responder replies in its own history.
    if (anchorIsBot)
        ev.silentCharGuids.push_back(anchor->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}


// ---------------------------------------------------------------------------
// HandleChatMessage  (shared for all chat types)
// ---------------------------------------------------------------------------

static void HandleChatMessage(Player* sender, uint32 type, const std::string& rawMsg,
                               Player* whisperTarget = nullptr)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(sender)) return;
    if (type == CHAT_MSG_AFK || type == CHAT_MSG_DND) return;
    if (IsBlacklisted(rawMsg)) return;

    const std::string msg = SanitizeChatMessage(rawMsg);
    std::string senderName = sender->GetName();

    // --- Whisper path ---
    if (type == CHAT_MSG_WHISPER)
    {
        if (!PBC_PTR_VALID(whisperTarget)
            || !whisperTarget->GetSession()
            || !whisperTarget->GetSession()->IsBot())
            return;

        std::string eventLine   = senderName + " tells you privately: " + msg;
        std::string historyLine = senderName + " (privately to you): " + msg;

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] Whisper event: {} -> {}: \"{}\" (chance={}%)",
                     senderName, whisperTarget->GetName(), msg, g_PBC_ReplyChanceWhisper);

        PBC_EventItem ev;
        ev.type    = PBC_EventType::Normal;
        ev.eventLine = eventLine;
        ev.histLine  = historyLine;
        ev.chatType  = CHAT_MSG_WHISPER;

        if (PBC_RollChance(g_PBC_ReplyChanceWhisper))
        {
            PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(whisperTarget);
            snap.whisperTargetGuid = sender->GetGUID();
            snap.whisperTargetName = senderName;
            ev.respondingChars.push_back(std::move(snap));
        }
        else
        {
            ev.silentCharGuids.push_back(whisperTarget->GetGUID().GetCounter());
        }

        PBC_PushEvent(std::move(ev));
        return;
    }

    // --- Say / Yell / Group / Raid ---
    std::string historyLine = senderName + ": " + msg;
    std::string eventLine   = senderName + " says: " + msg;

    bool senderIsBot = sender->GetSession() && sender->GetSession()->IsBot();

    bool isGroupChat = (type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER ||
                        type == CHAT_MSG_RAID  || type == CHAT_MSG_RAID_LEADER  ||
                        type == CHAT_MSG_RAID_WARNING);

    std::vector<Player*> bots = isGroupChat ? FindGroupBots(sender) : FindNearbyBots(sender);

    if (bots.empty()) return;

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Chat event from {} (isBot={}, type={}): \"{}\" — {} bot(s)",
                 senderName, senderIsBot, type, msg, bots.size());

    // Check if the message mentions any specific bot (real player only).
    bool anyMention = false;
    if (!senderIsBot)
    {
        for (Player* bot : bots)
            if (MentionsCharacter(msg, bot->GetName())) { anyMention = true; break; }
    }

    PBC_EventItem ev;
    ev.type      = PBC_EventType::Normal;
    ev.eventLine = eventLine;
    ev.histLine  = historyLine;
    ev.chatType  = type;

    if (anyMention)
    {
        // Build ordered list of mentioned bots (by position in message).
        std::string lowerMsg = msg;
        std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

        std::vector<std::pair<size_t, Player*>> positions;
        for (Player* bot : bots)
        {
            if (senderIsBot) break;
            std::string lname = bot->GetName();
            std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
            size_t pos = lowerMsg.find(lname);
            if (pos != std::string::npos)
                positions.emplace_back(pos, bot);
        }
        std::sort(positions.begin(), positions.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        // Mentioned bots: roll at mention chance, in mention order.
        std::unordered_set<uint64_t> mentionedGuids;
        for (auto& [pos, bot] : positions)
        {
            mentionedGuids.insert(bot->GetGUID().GetCounter());
            bool rolled = PBC_RollChance(g_PBC_ReplyChanceMention);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll mention character={} chance={}% -> {}",
                         bot->GetName(), g_PBC_ReplyChanceMention, rolled ? "RESPOND" : "silent");
            if (rolled)
            {
                ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
            }
            else
                ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }

        // Non-mentioned bots hear the message but always stay silent.
        for (Player* bot : bots)
        {
            if (mentionedGuids.count(bot->GetGUID().GetCounter())) continue;
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }
    }
    else
    {
        // No mention: randomize bot roll order, roll with penalty on success.
        // First bot rolls at g_PBC_ReplyChanceMessage. Each time a bot rolls
        // successfully, the chance for the next bot is reduced by
        // g_PBC_RollPenaltyOnAnswer. If a bot fails its roll, the next bot
        // rolls at the same chance (no penalty). Once the chance reaches 0
        // all remaining bots are silently skipped.
        std::vector<Player*> shuffledBots = bots;
        std::shuffle(shuffledBots.begin(), shuffledBots.end(), PBC_GetRNG());

        uint32 currentChance = g_PBC_ReplyChanceMessage;
        for (Player* bot : shuffledBots)
        {
            if (currentChance == 0)
            {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] Roll message character={} chance=0% -> silent (no chance left)",
                             bot->GetName());
                ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
                continue;
            }

            uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), currentChance);
            bool rolled = PBC_RollChance(effectiveChance);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Roll message character={} chance={}% (base={}% mod={}) -> {}",
                         bot->GetName(), effectiveChance, currentChance,
                         static_cast<int32_t>(effectiveChance) - static_cast<int32_t>(currentChance),
                         rolled ? "RESPOND" : "silent");
            if (rolled)
            {
                ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
                currentChance = currentChance > g_PBC_RollPenaltyOnAnswer
                    ? currentChance - g_PBC_RollPenaltyOnAnswer : 0;
            }
            else
            {
                ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
            }
        }
    }

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] Chat from {} type={} -> {}/{} bots will respond",
                 senderName, type, ev.respondingChars.size(), bots.size());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_PlayerEvents
// ---------------------------------------------------------------------------

PBC_PlayerEvents::PBC_PlayerEvents() : PlayerScript("PBC_PlayerEvents",
{
    PLAYERHOOK_CAN_PLAYER_USE_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
    PLAYERHOOK_ON_STORE_NEW_ITEM,
    PLAYERHOOK_ON_DUEL_END,
    PLAYERHOOK_ON_LEVEL_CHANGED,
    PLAYERHOOK_ON_CREATURE_KILL,
    PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
}) {}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg, Player* receiver)
{
    HandleChatMessage(player, type, msg, receiver);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg)
{
    HandleChatMessage(player, type, msg);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg, Group* /*group*/)
{
    HandleChatMessage(player, type, msg);
    return true;
}

// ---------------------------------------------------------------------------
// Item quality / type helpers (used by OnPlayerStoreNewItem)
// ---------------------------------------------------------------------------

static std::string ItemQualityStr(uint32 quality)
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

static std::string WeaponTypeStr(uint32 subClass)
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

// Returns a human-readable armor slot name from the item's InventoryType.
static std::string ArmorSlotStr(uint32 inventoryType)
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

// Builds a descriptive armor type string combining material and slot,
// e.g. "cloth robe", "plate helm", "leather legguards", "mail boots".
// For shields, bucklers and relics the subclass alone is descriptive enough.
// For misc subclass (rings, trinkets, cloaks, etc.) the slot name is used.
static std::string BuildArmorTypeStr(uint32 subClass, uint32 inventoryType)
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
    std::string slot = ArmorSlotStr(inventoryType);

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

static std::string ConsumableTypeStr(uint32 subClass)
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

static std::string GemTypeStr(uint32 subClass)
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

static std::string RecipeTypeStr(uint32 subClass)
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

static std::string TradeGoodsTypeStr(uint32 subClass)
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

static std::string ProjectileTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_ARROW:  return "arrow";
        case ITEM_SUBCLASS_BULLET: return "bullet";
        default:                   return "ammunition";
    }
}

static std::string ContainerTypeStr(uint32 subClass)
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

static std::string KeyTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_KEY:      return "key";
        case ITEM_SUBCLASS_LOCKPICK: return "lockpick";
        default:                     return "key";
    }
}

static std::string QuiverTypeStr(uint32 subClass)
{
    switch (subClass)
    {
        case ITEM_SUBCLASS_QUIVER:     return "quiver";
        case ITEM_SUBCLASS_AMMO_POUCH: return "ammo pouch";
        default:                       return "quiver";
    }
}

static std::string MiscTypeStr(uint32 subClass)
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

// Returns "a" or "an" based on the first character of the word that follows.
static const char* ArticleFor(const std::string& word)
{
    if (word.empty()) return "a";
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(word[0])));
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an" : "a";
}

// Builds a phrase like "a legendary two-handed mace", "an epic cloth robe",
// "a rare ring", "an uncommon potion", etc.
static std::string BuildItemPhrase(ItemTemplate const* tmpl)
{
    std::string quality = ItemQualityStr(tmpl->Quality);
    std::string type;

    switch (tmpl->Class)
    {
        case ITEM_CLASS_WEAPON:      type = WeaponTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_ARMOR:       type = BuildArmorTypeStr(tmpl->SubClass, tmpl->InventoryType); break;
        case ITEM_CLASS_CONSUMABLE:  type = ConsumableTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_GEM:         type = GemTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_RECIPE:      type = RecipeTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_TRADE_GOODS: type = TradeGoodsTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_PROJECTILE:  type = ProjectileTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_CONTAINER:   type = ContainerTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_KEY:         type = KeyTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_QUIVER:      type = QuiverTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_MISC:        type = MiscTypeStr(tmpl->SubClass); break;
        case ITEM_CLASS_QUEST:       type = "quest item"; break;
        case ITEM_CLASS_REAGENT:     type = "reagent"; break;
        case ITEM_CLASS_GLYPH:       type = "glyph"; break;
        default:                     type = "item"; break;
    }

    return std::string(ArticleFor(quality)) + " " + quality + " " + type;
}

// ---------------------------------------------------------------------------
// Bitmask of item classes that trigger loot events.
// Only weapons and armor by default — add more ITEM_CLASS_* values to enable
// additional item types (e.g. | (1u << ITEM_CLASS_CONSUMABLE)).
// ---------------------------------------------------------------------------
#define PBC_LOOT_EVENT_ITEM_CLASSES  ((1u << ITEM_CLASS_WEAPON) | (1u << ITEM_CLASS_ARMOR))

void PBC_PlayerEvents::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(item)) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;
    if (!(PBC_LOOT_EVENT_ITEM_CLASSES & (1u << tmpl->Class))) return;

    std::string itemName = tmpl->Name1;
    std::string phrase   = BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine("The party has found " + phrase + " named " + itemName),
        PBC_MakeHistLine("The party acquired " + phrase + " named " + itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(winner) || !PBC_PTR_VALID(loser) || type != DUEL_WON) return;
    PBC_DispatchGroupEvent(winner,
        PBC_MakeEventLine(winner->GetName() + " just won the duel against " + loser->GetName()),
        PBC_MakeHistLine(winner->GetName() + " won the duel against " + loser->GetName()),
        g_PBC_ReplyChanceDuel);
}

void PBC_PlayerEvents::OnPlayerLevelChanged(Player* player, uint8 oldLevel)
{
    if (!g_PBC_Enable || !PBC_PTR_VALID(player)) return;

    // Skip level-up events for low levels to avoid spamming history.
    if (oldLevel < 30) return;

    WorldSession* sess = player->GetSession();
    bool anchorIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    bool anchorIsBot  = PBC_PTR_VALID(sess) && sess->IsBot();

    if (!anchorIsReal && !BotIsGroupedWithRealPlayer(player)) return;

    auto bots = FindGroupBots(player);
    if (bots.empty() && !anchorIsBot) return;

    static const char* levelUpEventPhrases[] = {
        " can feel their abilities growing stronger",
        " grows more powerful, their skills sharpened by experience",
        " has grown stronger through their trials and hardships",
        " feels a surge of power as their abilities improve",
        " has become more capable, their experience forging them anew",
    };
    static const char* levelUpHistPhrases[] = {
        " grew stronger",
        " became more powerful through experience",
        " emerged from their trials more capable than before",
        " felt their abilities sharpen and grow",
        " gained new strength and skill",
    };

    const std::string& name = player->GetName();
    int idx = std::uniform_int_distribution<int>(0, 4)(PBC_GetRNG());
    std::string eventLine = PBC_MakeEventLine(name + levelUpEventPhrases[idx]);
    std::string histLine  = PBC_MakeHistLine(name + levelUpHistPhrases[idx]);

    PBC_NotifyRealPlayersInGroup(player, eventLine);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerLevelChanged: player={} level={} bots={} chance={}%",
                 player->GetName(), player->GetLevel(), bots.size(), g_PBC_ReplyChanceLevelUp);

    PBC_EventItem ev = BuildCharacterEvent(bots, eventLine, histLine,
                                     g_PBC_ReplyChanceLevelUp, CHAT_MSG_PARTY,
                                     /*canCreateEvents=*/true);

    if (anchorIsBot)
        ev.silentCharGuids.push_back(player->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// IsSignificantKill
//
// Returns true for dungeon/raid bosses, world bosses, and rare-elite or
// higher-ranked creatures (unique named rares such as King Mosh).
// Plain ELITE rank (rank 1) is intentionally excluded — there are many
// open-world elite creatures (e.g. Plated Stegodon, Devilsaurs) that spawn
// in packs and are not meaningful story events.
// ---------------------------------------------------------------------------
static bool IsSignificantKill(const Creature* killed)
{
    if (!PBC_PTR_VALID(killed)) return false;
    if (killed->IsDungeonBoss() || killed->isWorldBoss()) return true;
    uint32 rank = killed->GetCreatureTemplate()->rank;
    return rank >= CREATURE_ELITE_RAREELITE;
}

// ---------------------------------------------------------------------------
// BuildBossLabel
//
// Produces a display string for the killed creature, including its subtitle
// if it has one: "Kel'Thuzad (The Lich's Champion)" or just "Murloc Raider".
// ---------------------------------------------------------------------------
static std::string BuildBossLabel(const Creature* killed)
{
    std::string label = killed->GetName();
    const std::string& sub = killed->GetCreatureTemplate()->SubName;
    if (!sub.empty())
        label += " (" + sub + ")";
    return label;
}

void PBC_PlayerEvents::OnPlayerCreatureKill(Player* killer, Creature* killed)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(killer) || !PBC_PTR_VALID(killed)) return;
    if (!IsSignificantKill(killed)) return;

    // Only fire when the killer is in a group that contains at least one real player.
    Group* grp = killer->GetGroup();
    if (!grp) return;

    WorldSession* killerSess = killer->GetSession();
    bool killerIsBot = PBC_PTR_VALID(killerSess) && killerSess->IsBot();

    if (!BotIsGroupedWithRealPlayer(killer))
    {
        // Also allow real players as the killer themselves.
        if (!PBC_PTR_VALID(killerSess) || killerSess->IsBot()) return;
    }

    auto bots = FindGroupBots(killer);
    // If killer is a bot they are excluded from FindGroupBots; still proceed so
    // they receive the event.  If killer is a real player and no bots are in the
    // group there is nothing to dispatch.
    if (bots.empty() && !killerIsBot) return;

    std::string bossLabel = BuildBossLabel(killed);

    // Location: use party leader's zone/area for a specific location string,
    // matching the same approach used for character location in pbc_character.cpp.
    std::string location;
    {
        Player* locAnchor = killer;
        if (Player* leader = ObjectAccessor::FindPlayer(grp->GetLeaderGUID()))
            if (leader->IsInWorld())
                locAnchor = leader;

        location = PBC_BuildPlaceName(locAnchor);
    }

    std::string eventSuffix = bossLabel;
    if (!location.empty())
        eventSuffix += " in " + location;

    std::string eventLine = PBC_MakeEventLine("The party has slain " + eventSuffix);
    std::string histLine  = PBC_MakeHistLine("The party fought and slain " + eventSuffix);

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerCreatureKill: killer={} boss='{}' location='{}' bots={} chance={}%",
                 killer->GetName(), bossLabel, location, bots.size(), g_PBC_ReplyChanceBossKill);

    PBC_NotifyRealPlayersInGroup(killer, eventLine);

    PBC_EventItem ev = BuildCharacterEvent(bots, eventLine, histLine,
                                     g_PBC_ReplyChanceBossKill, CHAT_MSG_PARTY,
                                     /*canCreateEvents=*/true);

    // If the killer is itself a bot it was excluded from FindGroupBots() but
    // still needs to receive the event in its own history.
    if (killerIsBot)
        ev.silentCharGuids.push_back(killer->GetGUID().GetCounter());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Strip WoW quest text formatting codes.
// $b / $B  = line break
// $N / $n  = player name placeholder
// $R / $r  = player race placeholder
// $C / $c  = player class placeholder
// $G x:y;  = gender-conditional text
// ---------------------------------------------------------------------------
static std::string StripWowTextCodes(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '$' && i + 1 < text.size())
        {
            char next = text[i + 1];
            // $b / $B -> newline
            if (next == 'b' || next == 'B')
            {
                result += '\n';
                ++i;
                continue;
            }
            // $N / $n / $R / $r / $C / $c -> skip placeholder
            if (next == 'N' || next == 'n' || next == 'R' || next == 'r' ||
                next == 'C' || next == 'c')
            {
                ++i;
                continue;
            }
            // $G x:y; -> skip entire conditional
            if (next == 'G' || next == 'g')
            {
                i += 2; // skip '$G'
                while (i < text.size() && text[i] != ';')
                    ++i;
                continue;
            }
        }
        result += text[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Look up quest starter/ender NPC names from ObjectMgr relations.
// Returns a comma-separated list of creature names for the given quest ID.
// ---------------------------------------------------------------------------
static std::string GetQuestStarterNames(uint32 questId)
{
    std::string result;
    auto* relMap = sObjectMgr->GetCreatureQuestRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(it->first);
            if (cInfo)
            {
                if (!result.empty()) result += ", ";
                result += cInfo->Name;
            }
        }
    }
    // Also check gameobject starters
    auto* goRelMap = sObjectMgr->GetGOQuestRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(it->first);
            if (goInfo)
            {
                if (!result.empty()) result += ", ";
                result += goInfo->name;
            }
        }
    }
    return result;
}

static std::string GetQuestEnderNames(uint32 questId)
{
    std::string result;
    auto* relMap = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(it->first);
            if (cInfo)
            {
                if (!result.empty()) result += ", ";
                result += cInfo->Name;
            }
        }
    }
    // Also check gameobject enders
    auto* goRelMap = sObjectMgr->GetGOQuestInvolvedRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(it->first);
            if (goInfo)
            {
                if (!result.empty()) result += ", ";
                result += goInfo->name;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// String substitution for quest prompt placeholders.
// ---------------------------------------------------------------------------
static std::string SubstituteQuestVars(const std::string& tmpl,
                                        const std::string& title,
                                        const std::string& description,
                                        const std::string& logDescription,
                                        const std::string& completionLog,
                                        const std::string& rewardText,
                                        const std::string& questGiver,
                                        const std::string& questEnder)
{
    std::string result = tmpl;
    PBC_ReplaceToken(result, "quest_title",          title);
    PBC_ReplaceToken(result, "quest_description",    description);
    PBC_ReplaceToken(result, "quest_log_description", logDescription);
    PBC_ReplaceToken(result, "quest_completion_log",  completionLog);
    PBC_ReplaceToken(result, "quest_reward_text",    rewardText);
    PBC_ReplaceToken(result, "quest_giver",          questGiver);
    PBC_ReplaceToken(result, "quest_ender",          questEnder);
    return result;
}

// ---------------------------------------------------------------------------
// Common guard checks for quest events.
// Returns true if the event should proceed.
// ---------------------------------------------------------------------------
static bool QuestEventGuard(Player* player)
{
    if (!g_PBC_Enable) return false;
    if (!PBC_PTR_VALID(player)) return false;

    Group* grp = player->GetGroup();
    if (!grp) return false;
    if (grp->GetLeaderGUID() != player->GetGUID()) return false;

    WorldSession* sess = player->GetSession();
    bool leaderIsReal = PBC_PTR_VALID(sess) && !sess->IsBot();
    if (!leaderIsReal && !BotIsGroupedWithRealPlayer(player)) return false;

    return true;
}

// ---------------------------------------------------------------------------
// Quest completed event
// ---------------------------------------------------------------------------
void PBC_PlayerEvents::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!QuestEventGuard(player) || !quest) return;

    if (g_PBC_QuestCompletedSystemPrompt.empty() || g_PBC_QuestCompletedUserPrompt.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] OnPlayerCompleteQuest: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = StripWowTextCodes(quest->GetTitle());
    std::string questDescription    = StripWowTextCodes(quest->GetDetails());
    std::string questLogDescription = StripWowTextCodes(quest->GetObjectives());
    std::string questCompletionLog  = StripWowTextCodes(quest->GetCompletedText());
    std::string questRewardText     = StripWowTextCodes(quest->GetOfferRewardText());
    std::string questGiver          = GetQuestStarterNames(quest->GetQuestId());
    std::string questEnder          = GetQuestEnderNames(quest->GetQuestId());

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] OnPlayerCompleteQuest: leader={} quest='{}' (id={})",
                 player->GetName(), questTitle, quest->GetQuestId());

    std::string userPrompt = SubstituteQuestVars(
        g_PBC_QuestCompletedUserPrompt,
        questTitle, questDescription, questLogDescription, questCompletionLog,
        questRewardText, questGiver, questEnder);

    auto bots = FindGroupBots(player);
    if (bots.empty()) return;

    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = CHAT_MSG_PARTY;
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestCompletedSystemPrompt;
    ev.questUserPrompt    = userPrompt;
    ev.anchorObjGuid      = player->GetGUID();

    PBC_EventItem rolled = BuildCharacterEvent(bots, "", "", g_PBC_ReplyChanceQuestCompleted,
                                         CHAT_MSG_PARTY, /*canCreateEvents=*/true);
    ev.respondingChars  = std::move(rolled.respondingChars);
    ev.silentCharGuids  = std::move(rolled.silentCharGuids);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Quest taken event — internal handler shared by all three script types.
// ---------------------------------------------------------------------------
static void HandleQuestTaken(Player* player, Quest const* quest, std::string const& questGiver)
{
    if (!QuestEventGuard(player) || !quest) return;

    if (g_PBC_QuestTakenSystemPrompt.empty() || g_PBC_QuestTakenUserPrompt.empty())
    {
        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] HandleQuestTaken: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = StripWowTextCodes(quest->GetTitle());
    std::string questDescription    = StripWowTextCodes(quest->GetDetails());
    std::string questLogDescription = StripWowTextCodes(quest->GetObjectives());
    std::string questCompletionLog  = StripWowTextCodes(quest->GetCompletedText());

    if (g_PBC_DebugEnabled)
        LOG_INFO("server.loading", "[PBC] HandleQuestTaken: leader={} quest='{}' (id={}) giver='{}'",
                 player->GetName(), questTitle, quest->GetQuestId(), questGiver);

    std::string userPrompt = SubstituteQuestVars(
        g_PBC_QuestTakenUserPrompt,
        questTitle, questDescription, questLogDescription, questCompletionLog,
        /*rewardText=*/"", questGiver, /*questEnder=*/"");

    auto bots = FindGroupBots(player);
    if (bots.empty()) return;

    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = CHAT_MSG_PARTY;
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestTakenSystemPrompt;
    ev.questUserPrompt    = userPrompt;
    ev.anchorObjGuid      = player->GetGUID();

    PBC_EventItem rolled = BuildCharacterEvent(bots, "", "", g_PBC_ReplyChanceQuestTaken,
                                         CHAT_MSG_PARTY, /*canCreateEvents=*/true);
    ev.respondingChars  = std::move(rolled.respondingChars);
    ev.silentCharGuids  = std::move(rolled.silentCharGuids);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Quest taken — from creature (AllCreatureScript fires for ALL creatures)
// ---------------------------------------------------------------------------
PBC_AllCreatureQuestScript::PBC_AllCreatureQuestScript()
    : AllCreatureScript("PBC_AllCreatureQuestScript") {}

bool PBC_AllCreatureQuestScript::CanCreatureQuestAccept(Player* player, Creature* creature, Quest const* quest)
{
    if (creature && quest && player)
    {
        std::string giverName = creature->GetName();
        HandleQuestTaken(player, quest, giverName);
    }
    return false; // don't prevent quest acceptance
}

// ---------------------------------------------------------------------------
// Quest taken — from gameobject (AllGameObjectScript fires for ALL GOs)
// ---------------------------------------------------------------------------
PBC_AllGameObjectQuestScript::PBC_AllGameObjectQuestScript()
    : AllGameObjectScript("PBC_AllGameObjectQuestScript") {}

bool PBC_AllGameObjectQuestScript::CanGameObjectQuestAccept(Player* player, GameObject* go, Quest const* quest)
{
    if (go && quest && player)
    {
        GameObjectTemplate const* goInfo = go->GetGOInfo();
        std::string giverName = goInfo ? goInfo->name : go->GetName();
        HandleQuestTaken(player, quest, giverName);
    }
    return false; // don't prevent quest acceptance
}

// ---------------------------------------------------------------------------
// Quest taken — from item (AllItemScript fires for ALL items)
// ---------------------------------------------------------------------------
PBC_AllItemQuestScript::PBC_AllItemQuestScript()
    : AllItemScript("PBC_AllItemQuestScript") {}

bool PBC_AllItemQuestScript::CanItemQuestAccept(Player* player, Item* item, Quest const* quest)
{
    if (item && quest && player)
    {
        ItemTemplate const* itemInfo = item->GetTemplate();
        std::string giverName = itemInfo ? itemInfo->Name1 : "Unknown Item";
        HandleQuestTaken(player, quest, giverName);
    }
    return true; // true = allow quest acceptance (AllItemScript convention)
}
