#include "pbc_cards.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_utils.h"
#include "pbc_log.h"

#include "DatabaseEnv.h"

#include <mutex>

// ---------------------------------------------------------------------------
// Provenance enum <-> DB ENUM string conversion
// ---------------------------------------------------------------------------
std::string PBC_CardProvenanceToStr(PBC_CardProvenance p)
{
    switch (p)
    {
        case PBC_CardProvenance::Edited:   return "edited";
        case PBC_CardProvenance::Override: return "override";
        case PBC_CardProvenance::Generated:
        default:                           return "generated";
    }
}

PBC_CardProvenance PBC_CardProvenanceFromStr(const std::string& s)
{
    if (s == "edited")   return PBC_CardProvenance::Edited;
    if (s == "override") return PBC_CardProvenance::Override;
    return PBC_CardProvenance::Generated;
}

// ---------------------------------------------------------------------------
// Resolve a character name to a GUID, rejecting ambiguous (duplicate) names.
// Returns 0 if the character does not exist or the name is not unique.
// ---------------------------------------------------------------------------
static uint64_t ResolveUniqueGuidByName(const std::string& name)
{
    std::string escaped = name;
    CharacterDatabase.EscapeString(escaped);

    QueryResult result = CharacterDatabase.Query(
        "SELECT guid FROM characters WHERE name = '{}'", escaped);
    if (!result)
        return 0;

    if (result->GetRowCount() > 1)
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING,
                "Card import: character name '{}' is ambiguous ({} matches) — skipping disk card.",
                name, result->GetRowCount());
        return 0;
    }

    return (*result)[0].Get<uint32_t>();
}

// ---------------------------------------------------------------------------
// Disk import — seed mod_pbc_cards from disk cards (background-only override)
// ---------------------------------------------------------------------------
int PBC_ImportDiskCardsToDB()
{
    // Snapshot the disk cards under no lock (g_PBC_CharacterCards is only
    // mutated on the main thread, where this runs).
    if (g_PBC_CharacterCards.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Card import: no disk cards to import.");
        return 0;
    }

    // Schema-readiness guard: if the cards table does not exist yet (migration
    // not applied), skip the import entirely rather than issuing failing writes.
    if (!DB_TableHasColumn("mod_pbc_cards", "bot_guid"))
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR,
            "Card import: mod_pbc_cards does not exist — migration-20260624.sql has not been "
            "applied. Skipping disk card import.");
        return 0;
    }

    int imported = 0;
    for (const auto& [name, text] : g_PBC_CharacterCards)
    {
        uint64_t guid = ResolveUniqueGuidByName(name);
        if (guid == 0)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                    "Card import: no unique character for disk card '{}' — skipping.", name);
            continue;
        }

        std::string hash = PBC_Sha256Hex(text);

        // Decide authoritatively against the DB (not the in-memory cache) so a
        // stale or empty cache can never cause an unpinned, DB-authoritative
        // row to be clobbered:
        //   - no row        → new import (pinned override)
        //   - pinned, changed→ re-import
        //   - pinned, same  → skip (unchanged)
        //   - unpinned      → skip (DB authoritative / user-edited)
        bool existing = false;
        bool existingPinned = false;
        std::string existingHash;
        if (QueryResult row = CharacterDatabase.Query(
                "SELECT pinned, card_file_hash FROM mod_pbc_cards WHERE bot_guid = {}", guid))
        {
            existing       = true;
            existingPinned = (*row)[0].Get<uint8_t>() != 0;
            existingHash   = (*row)[1].Get<std::string>();
        }

        if (existing && !existingPinned)
            continue;  // DB-authoritative — never overwrite from disk
        if (existing && existingPinned && existingHash == hash)
            continue;  // unchanged pinned import

        PBC_CardEntry card;
        card.botGuid      = guid;
        card.name         = name;
        card.background   = text;  // verbatim file body; tokens resolved at render
        card.provenance   = PBC_CardProvenance::Override;
        card.pinned       = true;
        card.cardFileHash = hash;

        DB_UpsertCard(card);

        {
            std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
            g_PBC_Cards[guid] = card;
        }

        ++imported;
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "Card import: imported disk card '{}' (guid={}).", name, guid);
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Card import: {} disk card(s) imported/updated into mod_pbc_cards.", imported);
    return imported;
}
