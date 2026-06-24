#include "pbc_database.h"
#include "pbc_config.h"
#include "pbc_cards.h"
#include "pbc_log.h"
#include "pbc_utils.h"

#include "DatabaseEnv.h"

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

// Whether the attribution columns exist (set by the loaders from the schema).
// Inserts degrade to the legacy column set when false, mirroring the loaders,
// so a not-yet-applied migration never produces failed writes / stale IDs.
namespace {
    bool s_historyAttributionReady  = false;
    bool s_memoriesAttributionReady = false;
}

// ---------------------------------------------------------------------------
// Chat history — normalized schema (mod_pbc_history + mod_pbc_history_owners)
// ---------------------------------------------------------------------------

uint64_t DB_InsertHistoryMessage(uint64_t authorGuid, uint8_t type,
                                 const std::string& message,
                                 const std::vector<uint64_t>& ownerGuids,
                                 uint64_t subjectGuid,
                                 const std::string& eventType,
                                 const std::string& mood)
{
    if (ownerGuids.empty())
        return 0;

    std::string escaped = message;
    CharacterDatabase.EscapeString(escaped);

    auto nullableText = [](std::string v) -> std::string
    {
        if (v.empty())
            return "NULL";
        CharacterDatabase.EscapeString(v);
        return "'" + v + "'";
    };
    std::string subjectSql = subjectGuid ? std::to_string(subjectGuid) : "NULL";

    // Insert the message row (synchronous, single-connection).  Use the legacy
    // column set when the attribution migration has not been applied.
    if (s_historyAttributionReady)
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_pbc_history (author_guid, subject_guid, type, event_type, mood, message) "
            "VALUES ({}, {}, {}, {}, {}, '{}')",
            authorGuid,
            subjectSql,
            static_cast<uint32_t>(type),
            nullableText(eventType),
            nullableText(mood),
            escaped);
    else
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_pbc_history (author_guid, type, message) VALUES ({}, {}, '{}')",
            authorGuid,
            static_cast<uint32_t>(type),
            escaped);

    // Get the auto-increment ID (connection-safe: DirectExecute+Query on same pool)
    QueryResult idResult = CharacterDatabase.Query("SELECT LAST_INSERT_ID()");
    uint64_t historyId = idResult ? (*idResult)[0].Get<uint64_t>() : 0;
    if (historyId == 0)
        return 0;

    // Insert ownership rows
    for (uint64_t ownerGuid : ownerGuids)
    {
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_pbc_history_owners (guid, history_id) VALUES ({}, {})",
            ownerGuid,
            historyId
        );
    }

    return historyId;
}

void DB_UpdateHistoryMessage(uint64_t historyId, const std::string& newMessage)
{
    std::string escaped = newMessage;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_history SET message = '{}' WHERE id = {}",
        escaped,
        historyId
    );
}

void DB_UpdateHistoryMood(uint64_t historyId, const std::string& mood)
{
    if (!s_historyAttributionReady)
        return;  // mood column absent (migration not applied) — skip
    std::string escaped = mood;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_history SET mood = '{}' WHERE id = {}",
        escaped,
        historyId
    );
}

void DB_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId,
                               bool removeOrphaned)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_history_owners WHERE guid = {} AND history_id = {}",
        guid,
        historyId
    );

    if (removeOrphaned)
    {
        CharacterDatabase.Execute(
            "DELETE FROM mod_pbc_history "
            "WHERE id = {} AND NOT EXISTS ("
            "  SELECT 1 FROM mod_pbc_history_owners WHERE history_id = {}"
            ")",
            historyId,
            historyId
        );
    }
}

void DB_RemoveAllHistoryOwnership(uint64_t guid)
{
    // Delete all ownership rows for this character
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_history_owners WHERE guid = {}",
        guid
    );

    // Clean orphaned messages (those with zero remaining owners)
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_history "
        "WHERE NOT EXISTS ("
        "  SELECT 1 FROM mod_pbc_history_owners WHERE history_id = mod_pbc_history.id"
        ")"
    );
}

// ---------------------------------------------------------------------------
// Character memories
// ---------------------------------------------------------------------------

void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance,
                     uint64_t subjectGuid, const std::string& type, const std::string& mood)
{
    std::string escaped = memoryText;
    CharacterDatabase.EscapeString(escaped);

    std::string escapedType = type.empty() ? "general" : type;
    CharacterDatabase.EscapeString(escapedType);

    auto nullableText = [](std::string v) -> std::string
    {
        if (v.empty())
            return "NULL";
        CharacterDatabase.EscapeString(v);
        return "'" + v + "'";
    };
    std::string subjectSql = subjectGuid ? std::to_string(subjectGuid) : "NULL";

    if (s_memoriesAttributionReady)
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_pbc_memories (bot_guid, subject_guid, type, mood, memory_text, importance) "
            "VALUES ({}, {}, '{}', {}, '{}', {})",
            botGuid,
            subjectSql,
            escapedType,
            nullableText(mood),
            escaped,
            importance
        );
    else
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_pbc_memories (bot_guid, memory_text, importance) VALUES ({}, '{}', {})",
            botGuid,
            escaped,
            importance
        );
}

void DB_DeleteMemoriesForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllMemories()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_memories");
}

void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance)
{
    std::string escaped = newText;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_memories SET memory_text = '{}', importance = {} WHERE id = {}",
        escaped,
        static_cast<uint32_t>(importance),
        memoryId
    );
}

void DB_DeleteMemoryById(uint64_t memoryId)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE id = {}",
        memoryId
    );
}

void DB_MarkMemoryUsed(uint64_t memoryId)
{
    if (!s_memoriesAttributionReady)
        return;  // columns absent (migration not applied) — skip to avoid failed SQL
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_memories SET used = 1, last_used_at = NOW() WHERE id = {}",
        memoryId
    );
}

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

void DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier)
{
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_data (bot_guid, roll_chance_modifier) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE roll_chance_modifier = {}",
        botGuid,
        modifier,
        modifier
    );
}

// ---------------------------------------------------------------------------
// Character relationships
// ---------------------------------------------------------------------------

void DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                            const std::string& relationshipText)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = relationshipText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_relationships "
        "  (bot_guid, target_name, relationship_text) "
        "VALUES ({}, '{}', '{}') "
        "ON DUPLICATE KEY UPDATE "
        "  relationship_text = '{}', "
        "  updated_at = CURRENT_TIMESTAMP",
        botGuid,
        escapedName,
        escapedText,
        escapedText
    );
}

void DB_DeleteRelationshipsForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllRelationships()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_relationships");
}

void DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
                                const std::string& newText)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = newText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_relationships SET relationship_text = '{}' "
        "WHERE bot_guid = {} AND target_name = '{}'",
        escapedText,
        botGuid,
        escapedName
    );
}

void DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {} AND target_name = '{}'",
        botGuid,
        escapedName
    );
}

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

bool DB_TableHasColumn(const std::string& table, const std::string& column)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM information_schema.columns "
        "WHERE table_schema = DATABASE() AND table_name = '{}' AND column_name = '{}' LIMIT 1",
        table, column);
    return !!result;
}

bool DB_MemoriesTableEmpty()
{
    QueryResult result = CharacterDatabase.Query("SELECT 1 FROM mod_pbc_memories LIMIT 1");
    return !result;
}

bool DB_CardAdditionsTableNotEmpty()
{
    QueryResult tableCheck = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_character_card_additions'"
    );
    if (!tableCheck || (*tableCheck)[0].Get<uint64_t>() == 0)
        return false;

    QueryResult result = CharacterDatabase.Query("SELECT 1 FROM mod_pbc_character_card_additions LIMIT 1");
    return !!result;
}

// ---------------------------------------------------------------------------
// DB Loader functions (moved from pbc_config.cpp)
// ---------------------------------------------------------------------------

void PBC_LoadHistoryFromDB()
{
    // Graceful degradation: if the attribution migration has not been applied,
    // load the legacy column set so bots keep full history context (just
    // without the new enrichment) instead of starting empty.  Warn so the
    // operator knows to apply the migration.
    const bool enriched = DB_TableHasColumn("mod_pbc_history", "subject_guid");
    s_historyAttributionReady = enriched;  // gates enriched inserts
    if (!enriched)
        PBC_Log(PBC_LogLevel::PBC_WARNING,
            "mod_pbc_history is missing attribution columns — migration-20260624.sql has not been "
            "applied. Loading legacy history without attribution enrichment; apply the DB update and "
            "run .chars reload to enable it.");

    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    g_PBC_History.clear();
    g_PBC_HistoryOwners.clear();
    g_PBC_LastHistoryTime.clear();

    // 1. Load all messages from mod_pbc_history
    QueryResult msgResult = enriched
        ? CharacterDatabase.Query(
            "SELECT id, UNIX_TIMESTAMP(timestamp), author_guid, type, message, "
            "subject_guid, event_type, mood "
            "FROM mod_pbc_history ORDER BY id ASC")
        : CharacterDatabase.Query(
            "SELECT id, UNIX_TIMESTAMP(timestamp), author_guid, type, message "
            "FROM mod_pbc_history ORDER BY id ASC");

    if (msgResult)
    {
        do {
            PBC_HistoryEntry entry;
            entry.id          = (*msgResult)[0].Get<uint64_t>();
            entry.timestamp   = static_cast<time_t>((*msgResult)[1].Get<uint64_t>());
            entry.authorGuid  = (*msgResult)[2].Get<uint64_t>();
            entry.type        = static_cast<uint8_t>((*msgResult)[3].Get<uint32_t>());
            entry.message     = (*msgResult)[4].Get<std::string>();
            if (enriched)
            {
                entry.subjectGuid = (*msgResult)[5].Get<uint64_t>();
                entry.eventType   = (*msgResult)[6].Get<std::string>();
                entry.mood        = (*msgResult)[7].Get<std::string>();
            }
            g_PBC_History[entry.id] = std::move(entry);
        } while (msgResult->NextRow());
    }

    // 2. Load ownership (ordered by history_id = chronological)
    QueryResult ownResult = CharacterDatabase.Query(
        "SELECT guid, history_id FROM mod_pbc_history_owners ORDER BY history_id ASC");

    if (ownResult)
    {
        do {
            uint64_t guid      = (*ownResult)[0].Get<uint64_t>();
            uint64_t historyId = (*ownResult)[1].Get<uint64_t>();
            g_PBC_HistoryOwners[guid].push_back(historyId);

            // Track last timestamp per character
            auto hit = g_PBC_History.find(historyId);
            if (hit != g_PBC_History.end() && hit->second.timestamp > 0)
                g_PBC_LastHistoryTime[guid] = hit->second.timestamp;
        } while (ownResult->NextRow());
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Chat history loaded from DB ({} messages, {} characters).",
             g_PBC_History.size(), g_PBC_LastHistoryTime.size());
}

void PBC_LoadMemoriesFromDB()
{
    // Graceful degradation (see PBC_LoadHistoryFromDB): load the legacy column
    // set when the enrichment migration has not been applied, so memories are
    // never lost — only their enrichment defaults are used.
    const bool enriched = DB_TableHasColumn("mod_pbc_memories", "subject_guid");
    s_memoriesAttributionReady = enriched;  // gates enriched inserts
    if (!enriched)
        PBC_Log(PBC_LogLevel::PBC_WARNING,
            "mod_pbc_memories is missing enrichment columns — migration-20260624.sql has not been "
            "applied. Loading legacy memories without enrichment; apply the DB update and run "
            ".chars reload to enable it.");

    QueryResult result = enriched
        ? CharacterDatabase.Query(
            "SELECT id, bot_guid, memory_text, importance, UNIX_TIMESTAMP(created_at), "
            "subject_guid, type, mood, active, used, UNIX_TIMESTAMP(last_used_at) "
            "FROM mod_pbc_memories ORDER BY bot_guid ASC, id ASC")
        : CharacterDatabase.Query(
            "SELECT id, bot_guid, memory_text, importance, UNIX_TIMESTAMP(created_at) "
            "FROM mod_pbc_memories ORDER BY bot_guid ASC, id ASC");

    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    g_PBC_Memories.clear();

    if (!result) return;

    size_t count = 0;
    do {
        uint64_t    dbId       = (*result)[0].Get<uint64_t>();
        uint64_t    botGuid    = (*result)[1].Get<uint64_t>();
        std::string memText    = (*result)[2].Get<std::string>();
        uint8_t     importance = static_cast<uint8_t>((*result)[3].Get<uint32_t>());
        time_t      createdAt  = static_cast<time_t>((*result)[4].Get<uint64_t>());

        PBC_MemoryEntry entry;
        entry.dbId        = dbId;
        entry.text        = std::move(memText);
        entry.importance  = importance;
        entry.createdAt   = PBC_FormatDate(createdAt);
        if (enriched)
        {
            entry.subjectGuid = (*result)[5].Get<uint64_t>();
            entry.type        = (*result)[6].Get<std::string>();
            entry.mood        = (*result)[7].Get<std::string>();
            entry.active      = (*result)[8].Get<uint8_t>() != 0;
            entry.used        = (*result)[9].Get<uint8_t>() != 0;
            entry.lastUsedAt  = static_cast<time_t>((*result)[10].Get<uint64_t>());
            if (entry.type.empty())
                entry.type = "general";
        }
        g_PBC_Memories[botGuid].push_back(std::move(entry));
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character memories loaded from DB ({} entries).", count);
}

void PBC_LoadCharacterDataFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, roll_chance_modifier FROM mod_pbc_data"
    );

    std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
    g_PBC_RollChanceModifiers.clear();

    if (!result)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Characters data loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t botGuid = (*result)[0].Get<uint64_t>();
        int32_t  rollMod = (*result)[1].Get<int32_t>();
        if (rollMod != 0)
            g_PBC_RollChanceModifiers[botGuid] = rollMod;
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Characters data loaded from DB ({} entries, {} with roll modifier).",
             count, g_PBC_RollChanceModifiers.size());
}

void PBC_LoadRelationshipsFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, target_name, relationship_text, "
        "UNIX_TIMESTAMP(updated_at) FROM mod_pbc_relationships"
    );

    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    g_PBC_Relationships.clear();

    if (!result)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Relationships loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t    botGuid    = (*result)[0].Get<uint64_t>();
        std::string targetName = (*result)[1].Get<std::string>();
        std::string relText    = (*result)[2].Get<std::string>();
        time_t      updatedAt  = static_cast<time_t>((*result)[3].Get<uint64_t>());

        auto& entry = g_PBC_Relationships[botGuid][targetName];
        entry.text      = std::move(relText);
        entry.updatedAt = PBC_FormatDateTime(updatedAt);
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Relationships loaded from DB ({} entries).", count);
}

// ---------------------------------------------------------------------------
// Character cards (mod_pbc_cards)
// ---------------------------------------------------------------------------

void DB_UpsertCard(const PBC_CardEntry& card)
{
    // Render a nullable TEXT field: empty string -> SQL NULL (means "unset").
    auto nullable = [](std::string v) -> std::string
    {
        if (v.empty())
            return "NULL";
        CharacterDatabase.EscapeString(v);
        return "'" + v + "'";
    };

    std::string name = card.name;
    CharacterDatabase.EscapeString(name);

    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_cards "
        "(bot_guid, name, premise, personality, `values`, background, speech_style, quirks, "
        " provenance, pinned, card_file_hash, gen_model, gen_version) "
        "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, '{}', {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "  name=VALUES(name), premise=VALUES(premise), personality=VALUES(personality), "
        "  `values`=VALUES(`values`), background=VALUES(background), speech_style=VALUES(speech_style), "
        "  quirks=VALUES(quirks), provenance=VALUES(provenance), pinned=VALUES(pinned), "
        "  card_file_hash=VALUES(card_file_hash), gen_model=VALUES(gen_model), gen_version=VALUES(gen_version)",
        card.botGuid,
        name,
        nullable(card.premise),
        nullable(card.personality),
        nullable(card.values),
        nullable(card.background),
        nullable(card.speechStyle),
        nullable(card.quirks),
        PBC_CardProvenanceToStr(card.provenance),
        card.pinned ? 1 : 0,
        nullable(card.cardFileHash),
        nullable(card.genModel),
        card.genVersion
    );
}

void DB_SetCardPinned(uint64_t botGuid, bool pinned)
{
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_cards SET pinned = {} WHERE bot_guid = {}",
        pinned ? 1 : 0,
        botGuid);
}

// Column order shared by the card SELECTs below.
static const char* kCardSelectCols =
    "bot_guid, name, premise, personality, `values`, background, speech_style, quirks, "
    "provenance, pinned, card_file_hash, gen_model, gen_version";

// Parse the current row of a card SELECT (column order = kCardSelectCols).
static void AssignCardRow(const QueryResult& result, PBC_CardEntry& c)
{
    c.botGuid      = (*result)[0].Get<uint64_t>();
    c.name         = (*result)[1].Get<std::string>();
    c.premise      = (*result)[2].Get<std::string>();
    c.personality  = (*result)[3].Get<std::string>();
    c.values       = (*result)[4].Get<std::string>();
    c.background    = (*result)[5].Get<std::string>();
    c.speechStyle  = (*result)[6].Get<std::string>();
    c.quirks       = (*result)[7].Get<std::string>();
    c.provenance   = PBC_CardProvenanceFromStr((*result)[8].Get<std::string>());
    c.pinned       = (*result)[9].Get<uint8_t>() != 0;
    c.cardFileHash = (*result)[10].Get<std::string>();
    c.genModel     = (*result)[11].Get<std::string>();
    c.genVersion   = (*result)[12].Get<uint32_t>();
}

bool DB_LoadCard(uint64_t botGuid, PBC_CardEntry& out)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT {} FROM mod_pbc_cards WHERE bot_guid = {}", kCardSelectCols, botGuid);
    if (!result)
        return false;
    AssignCardRow(result, out);
    return true;
}

std::vector<PBC_CardEntry> DB_LoadAllCards()
{
    std::vector<PBC_CardEntry> out;
    if (!DB_TableHasColumn("mod_pbc_cards", "bot_guid"))
        return out;

    QueryResult result = CharacterDatabase.Query("SELECT {} FROM mod_pbc_cards", kCardSelectCols);
    if (!result)
        return out;

    do {
        PBC_CardEntry c;
        AssignCardRow(result, c);
        out.push_back(std::move(c));
    } while (result->NextRow());
    return out;
}

std::vector<std::string> DB_GetRecentGeneratedSummaries(size_t limit)
{
    std::vector<std::string> out;
    if (limit == 0)
        return out;

    QueryResult result = CharacterDatabase.Query(
        "SELECT premise FROM mod_pbc_cards "
        "WHERE provenance = 'generated' AND premise IS NOT NULL AND premise <> '' "
        "ORDER BY created_at DESC, bot_guid DESC LIMIT {}", limit);
    if (!result)
        return out;

    do {
        out.push_back((*result)[0].Get<std::string>());
    } while (result->NextRow());
    return out;
}

void PBC_LoadCardsFromDB()
{
    // Schema-readiness guard: if mod_pbc_cards does not exist yet (migration
    // not applied), skip without clearing the in-memory cache.
    if (!DB_TableHasColumn("mod_pbc_cards", "bot_guid"))
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR,
            "mod_pbc_cards does not exist — migration-20260624.sql has not been applied. "
            "Skipping card load. Apply the DB update and run .chars reload.");
        return;
    }

    QueryResult result = CharacterDatabase.Query("SELECT {} FROM mod_pbc_cards", kCardSelectCols);

    std::lock_guard<std::mutex> lock(g_PBC_CardsMutex);
    g_PBC_Cards.clear();

    if (!result)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character cards loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        PBC_CardEntry c;
        AssignCardRow(result, c);
        g_PBC_Cards[c.botGuid] = std::move(c);
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character cards loaded from DB ({} entries).", count);
}
