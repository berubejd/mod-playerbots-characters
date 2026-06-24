#ifndef MOD_PBC_CARDS_H
#define MOD_PBC_CARDS_H

#include <string>
#include <cstdint>
#include "pbc_config.h"   // PBC_CardEntry, PBC_CardProvenance, PBC_EventItem

class Player;

// ---------------------------------------------------------------------------
// Provenance enum <-> DB ENUM string conversion.
// ---------------------------------------------------------------------------
std::string        PBC_CardProvenanceToStr(PBC_CardProvenance p);
PBC_CardProvenance PBC_CardProvenanceFromStr(const std::string& s);

// ---------------------------------------------------------------------------
// Deterministic render: turn the stored persona fields into card text.
// Pure — NO model call.  Only the persona tokens ({premise}, {background}, …)
// are substituted; identity tokens ({char_name}, {char_race}, …) are left in
// place for the normal variable-substitution pass.  A template line whose
// persona field is empty is dropped entirely (no placeholder noise).
// ---------------------------------------------------------------------------
std::string PBC_RenderCard(const PBC_CardEntry& card);

// True if the card has at least one non-empty persona field.  An all-empty row
// (e.g. a bare SQL row) is treated as "no usable card" by the resolver.
bool PBC_CardHasContent(const PBC_CardEntry& card);

// ---------------------------------------------------------------------------
// Autogeneration (main-thread queueing; processed off-thread).
// ---------------------------------------------------------------------------

// Resolver hook: if autogen is enabled and no card row exists for the bot,
// queue a single-flight CardGeneration (Generate) event.  No-op otherwise.
void PBC_MaybeQueueCardGeneration(Player* bot);

// Resolver hook: if a card exists but is incomplete (has content yet is missing
// some fields), opportunistically queue a derivation to complete it — bounded
// to one attempt per character per server session.  No-op otherwise.
void PBC_MaybeQueueCardDerivation(Player* bot);

// Force a regeneration for an owned/bot character (the `.chars regen-card`
// command).  Returns false if autogen is disabled, the card is pinned, or a
// generation for this character is already in flight.
bool PBC_QueueCardRegen(Player* bot);

// Set/clear the pinned flag for a card (DB + cache).  Returns false if the
// character has no card row.
bool PBC_SetCardPinned(uint64_t guid, bool pinned);

// Process a CardGeneration event (runs on the dedicated card worker thread).
void PBC_ProcessCardGeneration(PBC_EventItem& ev);

// Enqueue an optional AI mood-refine job (history row id + event description)
// onto the background worker.  No-op when historyId is 0.
void PBC_EnqueueMoodRefine(uint64_t historyId, const std::string& eventText);

// Start/stop the dedicated card-generation worker thread.  Card LLM work runs
// here, off the shared chat-event queue, so it never delays bot replies.
// Stop also drains queued jobs and clears transient tracking (in-flight set and
// the opportunistic-derive suppressor) for a clean restart.
void PBC_StartCardWorker();
void PBC_StopCardWorker();

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
