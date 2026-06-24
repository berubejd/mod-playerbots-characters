#ifndef MOD_PBC_CARDS_H
#define MOD_PBC_CARDS_H

#include <string>
#include <cstdint>
#include "pbc_config.h"   // PBC_CardEntry, PBC_CardProvenance

// ---------------------------------------------------------------------------
// Provenance enum <-> DB ENUM string conversion.
// ---------------------------------------------------------------------------
std::string        PBC_CardProvenanceToStr(PBC_CardProvenance p);
PBC_CardProvenance PBC_CardProvenanceFromStr(const std::string& s);

// ---------------------------------------------------------------------------
// Disk import (main-thread only — touches the DB and CharacterCache).
//
// Scans the disk cards already loaded into g_PBC_CharacterCards (name -> text),
// resolves each name to a GUID, and seeds mod_pbc_cards with the file body as
// the `background` field, provenance='override', pinned=1, storing the content
// hash as the re-import signal.  Idempotent: a row is written only when it is
// new, or when it is pinned and the file's content hash has changed.  Rows that
// are not pinned (DB-authoritative / user-edited) are left untouched.  Updates
// the in-memory g_PBC_Cards cache.  Returns the number of rows imported/updated.
//
// Requires g_PBC_CharacterCards and g_PBC_Cards to be loaded first.
// ---------------------------------------------------------------------------
int PBC_ImportDiskCardsToDB();

#endif // MOD_PBC_CARDS_H
