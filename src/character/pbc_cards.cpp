#include "pbc_cards.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"

#include "DatabaseEnv.h"
#include "CharacterCache.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "WorldSession.h"

#include "pbc_json.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using json = pbc_json;

// ---------------------------------------------------------------------------
// The fixed set of persona fields (DB key <-> struct member).
// ---------------------------------------------------------------------------
namespace {

struct CardField { const char* key; std::string PBC_CardEntry::* mem; };

const CardField kCardFields[] = {
    { "premise",      &PBC_CardEntry::premise     },
    { "personality",  &PBC_CardEntry::personality },
    { "values",       &PBC_CardEntry::values      },
    { "background",   &PBC_CardEntry::background   },
    { "speech_style", &PBC_CardEntry::speechStyle },
    { "quirks",       &PBC_CardEntry::quirks      },
};

std::string TrimCopy(std::string s)
{
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    // Strip a single pair of surrounding quotes some models add.
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

bool AnyFieldPresent(const PBC_CardEntry& c)
{
    for (const auto& f : kCardFields)
        if (!(c.*(f.mem)).empty())
            return true;
    return false;
}

bool AnyFieldEmpty(const PBC_CardEntry& c)
{
    for (const auto& f : kCardFields)
        if ((c.*(f.mem)).empty())
            return true;
    return false;
}

// Assign parsed values onto the card.  When onlyEmpty is true, present fields
// are never overwritten (used for derivation, which must preserve authored or
// generated content).
void ApplyFields(PBC_CardEntry& c, const std::unordered_map<std::string, std::string>& fields,
                 bool onlyEmpty)
{
    for (const auto& f : kCardFields)
    {
        auto it = fields.find(f.key);
        if (it == fields.end() || it->second.empty())
            continue;
        std::string& dst = c.*(f.mem);
        if (!onlyEmpty || dst.empty())
            dst = it->second;
    }
}

std::string BuildPresentFields(const PBC_CardEntry& c)
{
    std::ostringstream oss;
    for (const auto& f : kCardFields)
    {
        const std::string& v = c.*(f.mem);
        if (!v.empty())
            oss << f.key << ": " << v << "\n";
    }
    std::string out = oss.str();
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::string BuildMissingFields(const PBC_CardEntry& c)
{
    std::ostringstream oss;
    for (const auto& f : kCardFields)
        if ((c.*(f.mem)).empty())
            oss << "- " << f.key << "\n";
    std::string out = oss.str();
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// Format directive appended to generation/derivation prompts via
// {format_instructions}.  Field keys stay in English (they map to DB columns /
// JSON keys); only the persona content follows the server locale.
std::string CardFormatInstructions()
{
    if (g_PBC_CardGenerationFormat == "labeled")
        return "Respond with one field per line in the form `key: value`, using exactly these keys: "
               "premise, personality, values, background, speech_style, quirks. Omit the line entirely "
               "for any field you are leaving empty. Do not add commentary, markdown, or any other text.";
    return "Respond with a single JSON object and nothing else. Use exactly these keys: "
           "\"premise\", \"personality\", \"values\", \"background\", \"speech_style\", \"quirks\". "
           "Each value must be a string; use an empty string for any field you are leaving empty. "
           "Do not wrap the JSON in code fences or add any commentary.";
}

// Extract the first complete, balanced top-level JSON object from text,
// ignoring braces inside string literals and any text before/after the object.
bool ExtractJsonObject(const std::string& text, std::string& out)
{
    size_t start = text.find('{');
    if (start == std::string::npos)
        return false;

    int  depth = 0;
    bool inStr = false;
    bool esc   = false;
    for (size_t i = start; i < text.size(); ++i)
    {
        char c = text[i];
        if (inStr)
        {
            if (esc)            esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"')  inStr = false;
        }
        else if (c == '"')
        {
            inStr = true;
        }
        else if (c == '{')
        {
            ++depth;
        }
        else if (c == '}')
        {
            if (--depth == 0)
            {
                out = text.substr(start, i - start + 1);
                return true;
            }
        }
    }
    return false;  // unbalanced
}

// Outcome of parsing model output.
//   ok     — the output was in the expected format (a JSON object parsed, or at
//            least one labeled field line matched). A valid response that fills
//            nothing (conservative) is ok=true, fields=0.
//   fields — number of non-empty fields extracted.
// This lets callers distinguish a conservative empty result (don't retry) from
// malformed/unparseable output (retry).
struct CardParseResult { bool ok = false; int fields = 0; };

CardParseResult ParseCardFields(const std::string& text, std::unordered_map<std::string, std::string>& out)
{
    out.clear();

    if (g_PBC_CardGenerationFormat == "json")
    {
        std::string jsonObj;
        if (!ExtractJsonObject(text, jsonObj))
            return { false, 0 };
        try
        {
            json j = json::parse(jsonObj);
            for (const auto& f : kCardFields)
            {
                if (j.contains(f.key) && j[f.key].is_string())
                {
                    std::string v = TrimCopy(j[f.key].get<std::string>());
                    if (!v.empty())
                        out[f.key] = v;
                }
            }
        }
        catch (const std::exception& ex)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Card parse (json) failed: {}", ex.what());
            return { false, 0 };
        }
        return { true, static_cast<int>(out.size()) };  // valid object (possibly empty)
    }

    // labeled
    static const std::regex kLabel(
        R"(^\s*[*_>\-\s]*(premise|personality|values|background|speech_style|quirks)\s*[:\-]\s*(.*)$)",
        std::regex::icase);

    bool anyLabel = false;
    std::istringstream iss(text);
    std::string line;
    std::string curKey;
    while (std::getline(iss, line))
    {
        std::smatch m;
        if (std::regex_match(line, m, kLabel))
        {
            anyLabel = true;
            curKey = m[1].str();
            std::transform(curKey.begin(), curKey.end(), curKey.begin(), ::tolower);
            out[curKey] = TrimCopy(m[2].str());
        }
        else if (!curKey.empty())
        {
            std::string extra = TrimCopy(line);
            if (!extra.empty())
                out[curKey] = out[curKey].empty() ? extra : out[curKey] + " " + extra;
        }
    }
    for (auto it = out.begin(); it != out.end(); )
    {
        if (it->second.empty()) it = out.erase(it);
        else ++it;
    }

    // ok if the output looked like labeled fields at all (even if all empty).
    return { anyLabel, static_cast<int>(out.size()) };
}

PBC_LLMResult CallCardModel(const std::string& sys,
                            const std::vector<PBC_ChatTurn>& turns,
                            double temperature)
{
    // One-time heavier call routed through the "cards" connection slot
    // (falls back to "default"). preserveNewlines keeps JSON/labeled
    // multi-line output intact.
    const PBC_APIConfig* base = PBC_GetConnection("cards");
    if (!base)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "CallCardModel: no card/default connection configured.");
        return PBC_LLMResult{ false, "", 0 };
    }

    PBC_APIConfig cfg = *base;
    cfg.jsonMode = (g_PBC_CardGenerationFormat == "json");   // enforce provider JSON mode

    // Per-operation temperature and no output cap — the model must be able to
    // produce the full card structure.
    if (!cfg.requestParameters.is_object())
        cfg.requestParameters = pbc_json::object();
    cfg.requestParameters["temperature"] = temperature;
    cfg.requestParameters.erase("max_tokens");
    // Ollama nests sampling knobs under "options" — mirror the overrides there.
    if (cfg.requestParameters.contains("options") && cfg.requestParameters["options"].is_object())
    {
        cfg.requestParameters["options"]["temperature"] = temperature;
        cfg.requestParameters["options"].erase("num_predict");
    }

    return PBC_CallLLMConversation(cfg, sys, turns, /*preserveNewlines=*/true);
}

// Substitute identity vars + the card-specific tokens into a prompt template.
std::string RenderPrompt(std::string tmpl, const PBC_VarMap& vars,
                         bool includeFormat,
                         const std::string& presentFields = "",
                         const std::string& missingFields = "",
                         const std::string& recentSummaries = "")
{
    PBC_ExpandNewlineEscapes(tmpl);
    PBC_SubstituteFromMap(tmpl, vars);
    if (includeFormat)
        PBC_ReplaceToken(tmpl, "format_instructions", CardFormatInstructions());
    PBC_ReplaceToken(tmpl, "present_fields", presentFields);
    PBC_ReplaceToken(tmpl, "missing_fields", missingFields);
    PBC_ReplaceToken(tmpl, "recent_summaries", recentSummaries);
    PBC_CleanUnknownTokens(tmpl);
    return tmpl;
}

// Build the anti-collision block: recent generated premises, one per line, so a
// batch of new cards doesn't converge on the same origins/hooks.
std::string BuildRecentSummaries()
{
    static constexpr size_t kRecentSummaryLimit = 25;
    std::vector<std::string> sums = DB_GetRecentGeneratedSummaries(kRecentSummaryLimit);
    if (sums.empty())
        return "(none yet)";
    std::string out;
    for (const auto& s : sums)
        out += "- " + s + "\n";
    if (!out.empty() && out.back() == '\n')
        out.pop_back();
    return out;
}

// Parse the localizable few-shot block (g_PBC_CardGenerationFewShot) into
// user/assistant turns.  Turns are delimited by lines containing exactly
// [user] or [assistant] (case-insensitive); content before the first marker is
// ignored.  Empty input → no turns (no few-shot).
std::vector<PBC_ChatTurn> ParseFewShot(const std::string& text)
{
    std::vector<PBC_ChatTurn> turns;
    if (text.empty())
        return turns;

    auto plainTrim = [](const std::string& s) -> std::string
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };

    std::istringstream iss(text);
    std::string line, role, content;

    auto flush = [&]()
    {
        if (!role.empty())
        {
            std::string c = plainTrim(content);
            if (!c.empty())
                turns.push_back({ role, c });
        }
        content.clear();
    };

    while (std::getline(iss, line))
    {
        std::string marker = plainTrim(line);
        std::transform(marker.begin(), marker.end(), marker.begin(), ::tolower);
        if (marker == "[user]")           { flush(); role = "user"; }
        else if (marker == "[assistant]") { flush(); role = "assistant"; }
        else if (!role.empty())           { content += line; content += "\n"; }
    }
    flush();
    return turns;
}

// Few-shot examples for generation, loaded from the localizable prompt file.
// Only applied in JSON mode (the shipped examples are JSON); empty file = none.
std::vector<PBC_ChatTurn> GenerationFewShot()
{
    return ParseFewShot(g_PBC_CardGenerationFewShot);
}

PBC_VarMap SeedVarMap(const PBC_EventItem& ev)
{
    PBC_VarMap vars;
    vars["char_name"]   = ev.cardGenName;
    vars["char_gender"] = ev.cardGenGender;
    vars["char_race"]   = ev.cardGenRace;
    vars["char_class"]  = ev.cardGenClass;
    vars["char_level"]  = ev.cardGenLevel;
    return vars;
}

// ---------------------------------------------------------------------------
// Single-flight guard: at most one in-flight generation/derivation per GUID.
// ---------------------------------------------------------------------------
std::mutex                    s_genMutex;
std::unordered_set<uint64_t>  s_genInFlight;
// Characters for which an opportunistic (resolver-driven) derivation has
// already been attempted this session — bounds retries so conservatively-empty
// fields are not re-derived on every contact.  Cleared on restart.
std::unordered_set<uint64_t>  s_deriveTried;

bool TryMarkInFlight(uint64_t guid)
{
    std::lock_guard<std::mutex> lock(s_genMutex);
    return s_genInFlight.insert(guid).second;
}

void ClearInFlight(uint64_t guid)
{
    std::lock_guard<std::mutex> lock(s_genMutex);
    s_genInFlight.erase(guid);
}

// ---------------------------------------------------------------------------
// Dedicated card-generation worker.
//
// Card generation/derivation make multi-second LLM calls.  They must NOT share
// the single chat-event worker queue (that would schedule card work ahead of
// the chat reply that triggered it and stall all other events).  Instead they
// run on this dedicated background thread with its own queue.
// ---------------------------------------------------------------------------
std::queue<PBC_EventItem> s_cardJobs;
std::mutex                s_cardJobsMutex;
std::condition_variable   s_cardJobsCv;
std::thread               s_cardWorker;
std::atomic<bool>         s_cardWorkerStop{ false };

void EnqueueCardJob(PBC_EventItem ev)
{
    {
        std::lock_guard<std::mutex> lk(s_cardJobsMutex);
        s_cardJobs.push(std::move(ev));
    }
    s_cardJobsCv.notify_one();
}

void CardWorkerLoop()
{
    for (;;)
    {
        PBC_EventItem job;
        {
            std::unique_lock<std::mutex> lk(s_cardJobsMutex);
            s_cardJobsCv.wait(lk, [] { return s_cardWorkerStop.load() || !s_cardJobs.empty(); });
            if (s_cardWorkerStop.load())
                return;  // abandon any pending jobs on shutdown
            job = std::move(s_cardJobs.front());
            s_cardJobs.pop();
        }
        PBC_ProcessCardGeneration(job);
    }
}

// Resolve a character's identity (offline-safe): CharacterCache first, then a
// direct `characters` lookup as a fallback.  Returns false only if neither
// source has the GUID.
bool SeedIdentityFromGuid(uint64_t guid, std::string& name, std::string& gender,
                          std::string& race, std::string& cls, std::string& level)
{
    if (CharacterCacheEntry const* e = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid(guid)))
    {
        name   = e->Name;
        gender = PBC_GenderStr(e->Sex);
        race   = PBC_RaceStr(e->Race);
        cls    = PBC_ClassStr(e->Class);
        level  = std::to_string(e->Level);
        return true;
    }

    if (QueryResult r = CharacterDatabase.Query(
            "SELECT name, race, class, gender, level FROM characters WHERE guid = {}", guid))
    {
        name   = (*r)[0].Get<std::string>();
        race   = PBC_RaceStr(static_cast<uint8_t>((*r)[1].Get<uint8_t>()));
        cls    = PBC_ClassStr(static_cast<uint8_t>((*r)[2].Get<uint8_t>()));
        gender = PBC_GenderStr(static_cast<uint8_t>((*r)[3].Get<uint8_t>()));
        level  = std::to_string((*r)[4].Get<uint32_t>());
        return true;
    }

    return false;
}

// Queue an off-thread derivation (fill empty sibling fields) for an imported
// card.  Seeds identity offline-safe (cache → DB).  Returns true if an event
// was enqueued, false if the identity could not be resolved.
bool QueueCardDerivation(uint64_t guid)
{
    std::string name, gender, race, cls, level;
    if (!SeedIdentityFromGuid(guid, name, gender, race, cls, level))
        return false;  // cannot resolve identity (character vanished) — skip

    // Derive events are not gated on the single-flight slot: the event queue
    // already serializes processing, and the Derive handler is idempotent
    // (it only fills still-empty fields), so it is safe to enqueue even if a
    // Generate for this GUID is currently pending.
    PBC_EventItem ev;
    ev.type          = PBC_EventType::CardGeneration;
    ev.cardGenMode   = PBC_CardGenMode::Derive;
    ev.cardGenGuid   = guid;
    ev.cardGenName   = name;
    ev.cardGenGender = gender;
    ev.cardGenRace   = race;
    ev.cardGenClass  = cls;
    ev.cardGenLevel  = level;
    EnqueueCardJob(std::move(ev));
    return true;
}

} // namespace

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
        // Skip empty / whitespace-only files so they don't create a blank,
        // content-less card row.
        if (text.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Card import: disk card '{}' is empty — skipping.", name);
            continue;
        }

        uint64_t guid = ResolveUniqueGuidByName(name);
        if (guid == 0)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                    "Card import: no unique character for disk card '{}' — skipping.", name);
            continue;
        }

        std::string hash = PBC_Sha256Hex(text);

        // Decide + write under g_PBC_CardsMutex so card DB mutations are
        // serialized against generation/derivation persists (which also read
        // and write the row under this lock).  Decision is authoritative
        // against the DB (not the cache), so a stale/empty cache can never
        // clobber an unpinned, DB-authoritative row:
        //   - no row        → new import (pinned override)
        //   - pinned, changed→ re-import
        //   - pinned, same  → skip (unchanged), re-attempt derivation
        //   - unpinned      → skip (DB authoritative / user-edited)
        bool didImport = false;
        bool wantDerive = false;
        {
            std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);

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
            {
                // DB-authoritative (unpinned / user-edited) — never touch from disk.
            }
            else if (existing && existingPinned && existingHash == hash)
            {
                // Unchanged pinned import — nothing to do. A failed or incomplete
                // derivation is retried opportunistically on the next bot contact
                // (PBC_MaybeQueueCardDerivation), so we deliberately do NOT
                // re-derive on every reload, which would re-run the LLM for cards
                // whose remaining fields were left empty on purpose.
            }
            else
            {
                PBC_CardEntry card;
                card.botGuid      = guid;
                card.name         = name;
                card.background   = text;  // verbatim file body; tokens resolved at render
                card.provenance   = PBC_CardProvenance::Override;
                card.pinned       = true;
                card.cardFileHash = hash;

                DB_UpsertCard(card);
                g_PBC_Cards[guid] = card;
                didImport  = true;
                wantDerive = g_PBC_CardDeriveOnImport;
            }
        }

        if (didImport)
        {
            ++imported;
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Card import: imported disk card '{}' (guid={}).", name, guid);
        }

        // Queue derivation outside the lock (it enqueues an event; the authored
        // pinned background is never overwritten by derivation).
        if (wantDerive)
            QueueCardDerivation(guid);
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Card import: {} disk card(s) imported/updated into mod_pbc_cards.", imported);
    return imported;
}

// ---------------------------------------------------------------------------
// Deterministic render
// ---------------------------------------------------------------------------
bool PBC_CardHasContent(const PBC_CardEntry& card)
{
    return AnyFieldPresent(card);
}

std::string PBC_RenderCard(const PBC_CardEntry& card)
{
    std::string tmpl = g_PBC_CardRenderTemplate;
    PBC_ExpandNewlineEscapes(tmpl);

    std::istringstream iss(tmpl);
    std::string line;
    std::vector<std::string> kept;
    while (std::getline(iss, line))
    {
        bool drop = false;
        std::string rendered = line;
        for (const auto& f : kCardFields)
        {
            const std::string token = "{" + std::string(f.key) + "}";
            if (rendered.find(token) == std::string::npos)
                continue;
            const std::string& val = card.*(f.mem);
            if (val.empty()) { drop = true; break; }  // empty field → drop the line
            PBC_ReplaceToken(rendered, f.key, val);
        }
        if (!drop)
            kept.push_back(rendered);
    }

    std::string out;
    for (size_t i = 0; i < kept.size(); ++i)
    {
        out += kept[i];
        if (i + 1 < kept.size())
            out += "\n";
    }
    return out;
}

// ---------------------------------------------------------------------------
// Identity seed from a live Player (online generation/regeneration).
// ---------------------------------------------------------------------------
static void SeedFromPlayer(PBC_EventItem& ev, Player* bot)
{
    ev.cardGenGuid   = bot->GetGUID().GetCounter();
    ev.cardGenName   = bot->GetName();
    ev.cardGenGender = PBC_GenderStr(bot->getGender());
    ev.cardGenRace   = PBC_RaceStr(bot->getRace());
    ev.cardGenClass  = PBC_ClassStr(bot->getClass());
    ev.cardGenLevel  = std::to_string(bot->GetLevel());
}

// ---------------------------------------------------------------------------
// Autogeneration queueing
// ---------------------------------------------------------------------------
void PBC_MaybeQueueCardGeneration(Player* bot)
{
    if (!g_PBC_Enable || !g_PBC_CardAutogenEnabled || !bot)
        return;

    // Automatic autogeneration is for playerbots only — never synthesize a
    // persona for a real player's own character (they author their own).
    WorldSession* sess = bot->GetSession();
    if (!sess || !sess->IsBot())
        return;

    uint64_t guid = bot->GetGUID().GetCounter();

    // A renderable (content-bearing) cached card means there is nothing to
    // generate. Pinned/empty rows and any cache/DB drift on the pinned flag are
    // resolved authoritatively against the DB below.
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
        auto it = g_PBC_Cards.find(guid);
        if (it != g_PBC_Cards.end() && PBC_CardHasContent(it->second))
            return;
    }
    if (!TryMarkInFlight(guid))
        return;  // already generating/queued

    // Cache miss/placeholder — confirm against the DB authoritatively (the cache
    // can drift, e.g. a direct SQL seed). A content-bearing or pinned row means
    // hydrate-and-stop; an empty non-pinned row falls through to generation.
    PBC_CardEntry existing;
    if (DB_LoadCard(guid, existing))
    {
        if (PBC_CardHasContent(existing) || existing.pinned)
        {
            {
                std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
                g_PBC_Cards[guid] = std::move(existing);
            }
            ClearInFlight(guid);
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                "Card autogen skipped for guid={}: hydrated existing DB card into cache.", guid);
            return;
        }
    }
    else
    {
        // No DB row — respect a pinned cache entry (drift) rather than generating
        // over it, consistent with PBC_QueueCardRegen and the persist guard.
        bool cachePinned = false;
        {
            std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
            auto it = g_PBC_Cards.find(guid);
            cachePinned = (it != g_PBC_Cards.end() && it->second.pinned);
        }
        if (cachePinned)
        {
            ClearInFlight(guid);
            return;
        }
    }

    PBC_EventItem ev;
    ev.type        = PBC_EventType::CardGeneration;
    ev.cardGenMode = PBC_CardGenMode::Generate;
    SeedFromPlayer(ev, bot);
    EnqueueCardJob(std::move(ev));

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Card autogen queued for '{}' (guid={}).", bot->GetName(), guid);
}

void PBC_MaybeQueueCardDerivation(Player* bot)
{
    if (!g_PBC_Enable || !bot)
        return;

    WorldSession* sess = bot->GetSession();
    if (!sess || !sess->IsBot())
        return;

    uint64_t guid = bot->GetGUID().GetCounter();

    // Only act on a card that has content but is still missing some fields.
    bool incomplete = false;
    bool isImport   = false;  // override provenance == disk/SQL import
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
        auto it = g_PBC_Cards.find(guid);
        if (it != g_PBC_Cards.end())
        {
            incomplete = AnyFieldPresent(it->second) && AnyFieldEmpty(it->second);
            isImport   = (it->second.provenance == PBC_CardProvenance::Override);
        }
    }
    if (!incomplete)
        return;

    // Gate by the relevant setting: import-sourced cards follow CardDeriveOnImport
    // (so a verbatim-disk-card setup never derives); generated/edited cards follow
    // CardAutogenEnabled (the LLM-card machinery).
    if (isImport ? !g_PBC_CardDeriveOnImport : !g_PBC_CardAutogenEnabled)
        return;

    // Bounded: at most one opportunistic derive attempt per character per
    // session, so a conservatively-empty field isn't retried on every contact.
    // Consume the slot only once a derive is actually enqueued.
    {
        std::lock_guard<std::mutex> lock(s_genMutex);
        if (s_deriveTried.count(guid))
            return;
    }
    if (QueueCardDerivation(guid))
    {
        std::lock_guard<std::mutex> lock(s_genMutex);
        s_deriveTried.insert(guid);
    }
}

bool PBC_QueueCardRegen(Player* bot)
{
    if (!bot || !g_PBC_CardAutogenEnabled)
        return false;

    uint64_t guid = bot->GetGUID().GetCounter();

    // Check the pinned flag against the DB authoritatively (the cache can be
    // stale relative to a direct DB change). This matches the discard guard in
    // PBC_ProcessCardGeneration, so regen never reports success then silently
    // gets dropped. A missing row is fine — regen will generate a fresh card.
    {
        PBC_CardEntry existing;
        if (DB_LoadCard(guid, existing))
        {
            {
                std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
                g_PBC_Cards[guid] = existing;  // keep the cache consistent
            }
            if (existing.pinned)
                return false;  // pinned cards are authoritative / read-only
        }
        else
        {
            // No DB row — respect a pinned cache entry (what the resolver is
            // serving) so a drift case (direct SQL delete / failed persist)
            // can't be clobbered by regen.
            std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
            auto it = g_PBC_Cards.find(guid);
            if (it != g_PBC_Cards.end() && it->second.pinned)
                return false;
        }
    }

    if (!TryMarkInFlight(guid))
        return false;  // already in flight

    PBC_EventItem ev;
    ev.type         = PBC_EventType::CardGeneration;
    ev.cardGenMode  = PBC_CardGenMode::Generate;
    ev.cardGenForce = true;  // explicit regen overwrites an existing (non-pinned) card
    SeedFromPlayer(ev, bot);
    EnqueueCardJob(std::move(ev));
    return true;
}

bool PBC_SetCardPinned(uint64_t guid, bool pinned)
{
    // Hold the lock across the DB read/write so card mutations stay serialized
    // against generation/derivation/import persists. Confirm the row exists in
    // the DB authoritatively — pinning a row that isn't there would silently
    // update zero rows and desync the cache.
    std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);

    PBC_CardEntry card;
    if (!DB_LoadCard(guid, card))
        return false;  // no card row for this character

    card.pinned = pinned;
    DB_SetCardPinned(guid, pinned);
    g_PBC_Cards[guid] = std::move(card);
    return true;
}

// ---------------------------------------------------------------------------
// CardGeneration event processing (event thread)
// ---------------------------------------------------------------------------
void PBC_ProcessCardGeneration(PBC_EventItem& ev)
{
    const uint64_t guid = ev.cardGenGuid;

    // Release the single-flight slot on any exit path — but only for Generate
    // events, which are the ones that reserve it (Derive events do not).
    struct InFlightGuard
    {
        uint64_t g;
        bool     active;
        ~InFlightGuard() { if (active) ClearInFlight(g); }
    } guard{ guid, ev.cardGenMode == PBC_CardGenMode::Generate };

    // Opportunistic-derivation suppressor lifecycle: s_deriveTried was set when
    // a derive was enqueued from bot contact. Release it on any non-definitive
    // exit (so a later contact retries); keep it only on a definitive outcome
    // (a parsed model answer, or an already-complete card). Inactive for
    // Generate-mode jobs, and a harmless no-op for import-derives (not in the set).
    bool deriveKeepTried = false;
    struct DeriveTriedGuard
    {
        uint64_t    g;
        bool        active;
        const bool* keep;
        ~DeriveTriedGuard()
        {
            if (!active || *keep) return;
            std::lock_guard<std::mutex> lock(s_genMutex);
            s_deriveTried.erase(g);
        }
    } deriveTriedGuard{ guid, ev.cardGenMode == PBC_CardGenMode::Derive, &deriveKeepTried };

    if (guid == 0)
        return;

    // If the cards table is missing (migration not applied), don't generate or
    // persist — this avoids a cache/DB divergence and a wasted LLM call.
    if (!DB_TableHasColumn("mod_pbc_cards", "bot_guid"))
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING,
            "CardGeneration: mod_pbc_cards is missing (migration-20260624.sql not applied) — "
            "skipping for guid={}.", guid);
        return;
    }

    // Re-check config at process time — a reload may have disabled autogen after
    // this job was queued. (Derive-mode's import/generated gating is applied
    // after the card is loaded, below.)
    if (ev.cardGenMode == PBC_CardGenMode::Generate && !g_PBC_CardAutogenEnabled)
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG,
            "CardGeneration: autogen disabled — skipping queued generation for guid={}.", guid);
        return;
    }

    PBC_VarMap vars = SeedVarMap(ev);

    // Build the working card.
    PBC_CardEntry card;
    PBC_CardEntry deriveSource;  // Derive mode: the material we derive FROM (for the staleness check)
    if (ev.cardGenMode == PBC_CardGenMode::Derive)
    {
        // Load the working card from cache, falling back to the authoritative
        // DB row (the cache may not yet hold a freshly-imported card, and derive
        // events may run while a generation is in flight).
        std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
        auto it = g_PBC_Cards.find(guid);
        if (it != g_PBC_Cards.end())
        {
            card = it->second;
        }
        else if (!DB_LoadCard(guid, card))
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "CardGeneration(Derive): no card for guid={}, skipping.", guid);
            return;
        }
        deriveSource = card;  // snapshot of the source fields before derivation

        // Re-check config at process time (a reload may have disabled the
        // relevant setting after this job was queued).
        bool isImport = (card.provenance == PBC_CardProvenance::Override);
        if (isImport ? !g_PBC_CardDeriveOnImport : !g_PBC_CardAutogenEnabled)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                "CardGeneration(Derive): disabled by config — skipping for guid={}.", guid);
            return;
        }
    }
    else
    {
        card.botGuid    = guid;
        card.name       = ev.cardGenName;
        card.provenance = PBC_CardProvenance::Generated;
        card.pinned     = false;
    }

    // Derive mode with a complete card has nothing to do.
    if (ev.cardGenMode == PBC_CardGenMode::Derive && !AnyFieldEmpty(card))
    {
        deriveKeepTried = true;  // complete — definitive, no retry
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "CardGeneration(Derive): card already complete for guid={}.", guid);
        return;
    }

    // -- Generate step (Generate mode only): synthesize fields from identity.
    if (ev.cardGenMode == PBC_CardGenMode::Generate)
    {
        std::string sys = RenderPrompt(g_PBC_CardGenerationSystemPrompt, vars, /*includeFormat=*/false);
        std::string usr = RenderPrompt(g_PBC_CardGenerationUserPrompt,   vars, /*includeFormat=*/true,
                                       /*presentFields=*/"", /*missingFields=*/"",
                                       /*recentSummaries=*/BuildRecentSummaries());

        // Few-shot examples (JSON, second person) are only coherent with the
        // JSON contract; in labeled mode rely on the format instructions instead.
        std::vector<PBC_ChatTurn> turns;
        if (g_PBC_CardGenerationFormat == "json")
            turns = GenerationFewShot();
        turns.push_back({ "user", usr });

        PBC_LLMResult res = CallCardModel(sys, turns, g_PBC_CardGenerationTemperature);
        if (!res.success || res.text.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                "CardGeneration: LLM failed for '{}' (guid={}) — not persisting, will retry on next contact.",
                ev.cardGenName, guid);
            return;
        }

        std::unordered_map<std::string, std::string> fields;
        if (ParseCardFields(res.text, fields).fields == 0)
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                "CardGeneration: no fields parsed for '{}' (guid={}) — not persisting.", ev.cardGenName, guid);
            return;
        }
        ApplyFields(card, fields, /*onlyEmpty=*/false);
        if (const PBC_APIConfig* cardCfg = PBC_GetConnection("cards"))
            card.genModel = cardCfg->model;
    }

    // -- Derive step (both modes): conservatively fill any still-empty fields.
    if (AnyFieldEmpty(card) && AnyFieldPresent(card))
    {
        std::string sys = RenderPrompt(g_PBC_CardDerivationSystemPrompt, vars, /*includeFormat=*/false);
        std::string usr = RenderPrompt(g_PBC_CardDerivationUserPrompt,   vars, /*includeFormat=*/true,
                                       BuildPresentFields(card), BuildMissingFields(card));

        std::vector<PBC_ChatTurn> turns = { { "user", usr } };
        PBC_LLMResult res = CallCardModel(sys, turns, g_PBC_CardDerivationTemperature);

        std::unordered_map<std::string, std::string> fields;
        CardParseResult pr = (res.success && !res.text.empty())
            ? ParseCardFields(res.text, fields)
            : CardParseResult{ false, 0 };

        if (pr.ok)
        {
            // A parsed response is the model's considered answer — apply whatever
            // it returned. A conservative empty result (no fields it can justify)
            // is intentional and must NOT trigger an immediate retry, so mark the
            // attempt definitive (the card is re-attempted next session/reload).
            if (pr.fields > 0)
                ApplyFields(card, fields, /*onlyEmpty=*/true);  // never overwrite present fields
            deriveKeepTried = true;
        }
        else
        {
            // HTTP failure or unparseable/malformed output — leave the suppressor
            // released (the default) so a later contact retries.
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                "CardGeneration: derivation produced no parseable result for guid={}, proceeding with partial card.", guid);
        }
    }

    if (!AnyFieldPresent(card))
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING,
            "CardGeneration: produced no usable fields for guid={} — not persisting.", guid);
        return;
    }

    // Persist atomically against concurrent card mutations (disk import, pin,
    // or edits) that may have happened while the LLM call was in flight.
    {
        std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);

        if (ev.cardGenMode == PBC_CardGenMode::Generate)
        {
            // Decide against the DB authoritatively — the in-memory cache may be
            // stale or empty (e.g. a prior card-load failure), which must not be
            // allowed to clobber a real row (especially a pinned import).
            PBC_CardEntry existing;
            if (DB_LoadCard(guid, existing))
            {
                // Keep the cache consistent with the DB so the resolver stops
                // returning the default and re-queueing.
                g_PBC_Cards[guid] = existing;

                // Never clobber a pinned (file/override-authoritative) card.
                if (existing.pinned)
                {
                    PBC_Log(PBC_LogLevel::PBC_WARNING,
                        "CardGeneration: a pinned card appeared for guid={} during generation — "
                        "discarding generated result.", guid);
                    return;
                }
                // Auto-generation must not clobber a content-bearing card that
                // appeared meanwhile; an empty placeholder row may be overwritten,
                // and an explicit regen (force) overwrites a non-pinned card.
                if (PBC_CardHasContent(existing) && !ev.cardGenForce)
                {
                    PBC_Log(PBC_LogLevel::PBC_DEBUG,
                        "CardGeneration: a card already exists for guid={} — discarding auto-generated result.", guid);
                    return;
                }
                // empty non-pinned row, or forced regen → fall through to overwrite
            }
            else
            {
                // No DB row — respect a pinned cache entry (drift), consistent
                // with the queue-time guard and PBC_QueueCardRegen.
                auto it = g_PBC_Cards.find(guid);
                if (it != g_PBC_Cards.end() && it->second.pinned)
                {
                    PBC_Log(PBC_LogLevel::PBC_WARNING,
                        "CardGeneration: cache-pinned card with no DB row for guid={} — discarding generated result.", guid);
                    return;
                }
            }
            DB_UpsertCard(card);
            g_PBC_Cards[guid] = card;
        }
        else // Derive: fill only the fields still empty on the AUTHORITATIVE DB
             // row, so a concurrent import/edit (and the pinned background) is
             // preserved even if the cache is stale.
        {
            PBC_CardEntry current;
            if (!DB_LoadCard(guid, current))
            {
                deriveKeepTried = false;  // not persisted — allow a later retry
                PBC_Log(PBC_LogLevel::PBC_DEBUG,
                    "CardGeneration(Derive): card removed for guid={} before persist, skipping.", guid);
                return;
            }

            // Abort if the source material changed while we were deriving (e.g. a
            // disk re-import replaced the pinned background): the derived fields
            // would be inconsistent with the new source. A fresh derive will have
            // been queued by the re-import.
            for (const auto& f : kCardFields)
            {
                const std::string& src = deriveSource.*(f.mem);
                if (!src.empty() && current.*(f.mem) != src)
                {
                    g_PBC_Cards[guid] = current;  // keep the cache consistent with the DB
                    deriveKeepTried = false;      // not persisted — allow a later retry
                    PBC_Log(PBC_LogLevel::PBC_DEBUG,
                        "CardGeneration(Derive): source changed for guid={} during derivation — discarding.", guid);
                    return;
                }
            }

            bool changed = false;
            for (const auto& f : kCardFields)
            {
                std::string& dst = current.*(f.mem);
                const std::string& src = card.*(f.mem);
                if (dst.empty() && !src.empty()) { dst = src; changed = true; }
            }
            if (!changed)
            {
                g_PBC_Cards[guid] = current;  // keep the cache consistent with the DB
                PBC_Log(PBC_LogLevel::PBC_DEBUG,
                    "CardGeneration(Derive): nothing new to fill for guid={}.", guid);
                return;
            }
            DB_UpsertCard(current);
            g_PBC_Cards[guid] = current;
            card = current;            // for the log line below
        }
    }

    PBC_WsNotify(guid, "card");

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "CardGeneration: {} card for '{}' (guid={}).",
        ev.cardGenMode == PBC_CardGenMode::Generate ? "generated" : "derived",
        card.name.empty() ? ev.cardGenName : card.name, guid);
}

// ---------------------------------------------------------------------------
// Card worker lifecycle
// ---------------------------------------------------------------------------
void PBC_StartCardWorker()
{
    if (s_cardWorker.joinable())
        return;  // already running
    s_cardWorkerStop.store(false);
    s_cardWorker = std::thread(CardWorkerLoop);
}

void PBC_StopCardWorker()
{
    {
        std::lock_guard<std::mutex> lk(s_cardJobsMutex);
        s_cardWorkerStop.store(true);
    }
    s_cardJobsCv.notify_all();
    if (s_cardWorker.joinable())
        s_cardWorker.join();

    // Drop abandoned jobs and clear transient tracking so a later restart
    // (re-enable) begins clean and no GUID is left falsely "in flight".
    {
        std::lock_guard<std::mutex> lk(s_cardJobsMutex);
        std::queue<PBC_EventItem> empty;
        std::swap(s_cardJobs, empty);
    }
    {
        std::lock_guard<std::mutex> lk(s_genMutex);
        s_genInFlight.clear();
        s_deriveTried.clear();
    }
}
