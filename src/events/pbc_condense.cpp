#include "pbc_condense.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"

#include <regex>
#include <sstream>
#include <mutex>

// ---------------------------------------------------------------------------
// PBC_ParseMemoryLines
//
// Parse LLM output lines matching [N] text and insert as memories.
// ---------------------------------------------------------------------------
int PBC_ParseMemoryLines(const std::string& text, uint64_t botGuid)
{
    static const std::regex kMemLine(R"(\[(\d+)\]\s*(.+))");
    std::istringstream iss(text);
    std::string line;
    int memCount = 0;

    while (std::getline(iss, line))
    {
        std::smatch m;
        if (!std::regex_match(line, m, kMemLine))
            continue;

        uint8_t importance = 5;
        try { importance = static_cast<uint8_t>(std::clamp(std::stoi(m[1].str()), 1, 10)); } catch (...) {}
        std::string memText = m[2].str();
        if (memText.empty())
            continue;

        DB_InsertMemory(botGuid, memText, importance);

        PBC_MemoryEntry entry;
        entry.dbId       = 0;
        entry.text       = std::move(memText);
        entry.importance = importance;
        entry.createdAt  = PBC_FormatDate(std::time(nullptr));

        {
            std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
            g_PBC_Memories[botGuid].push_back(std::move(entry));
        }
        ++memCount;
    }
    return memCount;
}

// ---------------------------------------------------------------------------
// PBC_CondenseInline
//
// Runs condensation synchronously inside the event thread.
// ---------------------------------------------------------------------------
bool PBC_CondenseInline(PBC_CharacterSnapshot& snap,
                        const std::string& sysPrompt,
                        const std::string& userPromptTmpl)
{
    if (sysPrompt.empty() || userPromptTmpl.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "CondenseInline: prompts not configured, skipping for character={}", snap.charName);
        return false;
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "CondenseInline: character={} history_lines={}", snap.charName, snap.history.size());

    std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(snap, userPromptTmpl);
    const PBC_APIConfig* cfg = PBC_GetConnection("condensation");
    PBC_LLMResult res = PBC_CallLLMWithConfig(*cfg, sysPrompt, userPrompt, /*preserveNewlines=*/true);

    if (!res.success || res.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "CondenseInline: LLM failed for character={} — history left untouched, will retry on next event", snap.charName);
        return false;
    }

    int memCount = PBC_ParseMemoryLines(res.text, snap.charGuidRaw);
    PBC_WsNotify(snap.charGuidRaw, "memory");

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        g_PBC_HistoryOwners.erase(snap.charGuidRaw);
        g_PBC_LastHistoryTime.erase(snap.charGuidRaw);
    }
    DB_RemoveAllHistoryOwnership(snap.charGuidRaw);
    snap.history.clear();

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "CondenseInline: condensed character={} memories_extracted={}",
             snap.charName, memCount);

    return true;
}

// ---------------------------------------------------------------------------
// QueueRelationshipUpdatesAfterCondensation
//
// After condensation succeeds, queue RelationshipUpdate events for all party
// members of the given character, using the pre-condensation history so the
// LLM has full context.
// ---------------------------------------------------------------------------
void QueueRelationshipUpdatesAfterCondensation(
    const PBC_CharacterSnapshot& snap,
    const std::deque<std::string>& preCondensationHistory)
{
    if (g_PBC_RelationshipUpdateSystemPrompt.empty() ||
        g_PBC_RelationshipUpdateUserPrompt.empty() ||
        snap.partyMemberNames.empty())
        return;

    for (const auto& memberName : snap.partyMemberNames)
    {
        std::string currentRel;
        {
            std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
            auto botIt = g_PBC_Relationships.find(snap.charGuidRaw);
            if (botIt != g_PBC_Relationships.end())
            {
                auto tgtIt = botIt->second.find(memberName);
                if (tgtIt != botIt->second.end())
                    currentRel = tgtIt->second.text;
            }
        }
        if (currentRel.empty())
            currentRel = PBC_DefaultRelationshipText(memberName);

        // Use the pre-condensation history so the LLM has full context.
        PBC_CharacterSnapshot relSnap = snap;
        relSnap.history = preCondensationHistory;

        PBC_EventItem relEv;
        relEv.type                       = PBC_EventType::RelationshipUpdate;
        relEv.relationshipChar            = std::move(relSnap);
        relEv.relationshipTargetName     = memberName;
        relEv.relationshipTargetInfo     = PBC_BuildTargetInfo(memberName);
        relEv.relationshipCurrentText    = currentRel;
        relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
        relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

        PBC_PushEvent(std::move(relEv));

        PBC_Log(PBC_LogLevel::PBC_DEBUG,
                 "Condensation: queuing relationship update for character={} target={}",
                 snap.charName, memberName);
    }
}
