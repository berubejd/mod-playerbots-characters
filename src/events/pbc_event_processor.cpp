#include "pbc_event_processor.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_event_dispatch.h"
#include "pbc_condense.h"
#include "pbc_log.h"

#include "Player.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "WorldSession.h"
#include "SharedDefines.h"
#include "GameTime.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <queue>

// ===========================================================================
// Narrator segment parsing helpers
// ===========================================================================

std::vector<NarratorSegment> ParseNarratorSpans(const std::string& text)
{
    std::vector<NarratorSegment> segments;

    // Pre-scan: find all valid *text* narrator spans (opening '*'
    // followed by at least one character and a closing '*').
    std::vector<std::pair<size_t, size_t>> narrSpans; // [start, end] inclusive
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            if (text[pos] == '*')
            {
                size_t closingPos = text.find('*', pos + 1);
                if (closingPos != std::string::npos && closingPos > pos + 1)
                {
                    narrSpans.emplace_back(pos, closingPos);
                    pos = closingPos + 1;
                    continue;
                }
            }
            pos++;
        }
    }

    if (narrSpans.empty())
    {
        segments.push_back({text, false});
        return segments;
    }

    size_t lastEnd = 0;
    for (const auto& span : narrSpans)
    {
        // Regular text before this narrator block
        if (span.first > lastEnd)
        {
            std::string reg = text.substr(lastEnd, span.first - lastEnd);
            size_t s = reg.find_first_not_of(" \t\n\r");
            size_t e = reg.find_last_not_of(" \t\n\r");
            if (s != std::string::npos && e != std::string::npos && s <= e)
                segments.push_back({reg.substr(s, e - s + 1), false});
        }

        // Narrator block
        segments.push_back({text.substr(span.first, span.second - span.first + 1), true});

        lastEnd = span.second + 1;
    }

    // Remaining regular text after the last narrator block
    if (lastEnd < text.size())
    {
        std::string reg = text.substr(lastEnd);
        size_t s = reg.find_first_not_of(" \t\n\r");
        size_t e = reg.find_last_not_of(" \t\n\r");
        if (s != std::string::npos && e != std::string::npos && s <= e)
            segments.push_back({reg.substr(s, e - s + 1), false});
    }

    return segments;
}

void PushReplySegments(const PBC_CharacterSnapshot& snap,
                       PBC_EventItem& ev,
                       const std::vector<NarratorSegment>& segments)
{
    for (const auto& seg : segments)
    {
        if (seg.isNarrator)
        {
            PBC_PendingAction narrAction;
            narrAction.charGuid          = snap.charObjGuid;
            narrAction.text              = seg.text;
            narrAction.isNarratorMessage = true;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(narrAction));
        }
        else if (!seg.text.empty())
        {
            PBC_PendingAction action;
            action.charGuid    = snap.charObjGuid;
            action.targetGuid  = snap.whisperTargetGuid;
            action.chatType    = ev.chatType;
            action.text        = seg.text;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(action));
        }
    }
}

// ===========================================================================
// PBC_PushNarratorSummary
// ===========================================================================

void PBC_PushNarratorSummary(const ObjectGuid& anchorObjGuid, const std::string& eventLine)
{
    if (g_PBC_DisplayNarratorEvents && !anchorObjGuid.IsEmpty())
    {
        PBC_PendingAction narrAction;
        narrAction.charGuid          = anchorObjGuid;
        narrAction.text              = eventLine;
        narrAction.isNarratorMessage = true;

        std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
        g_PBC_PendingActions.push(std::move(narrAction));
    }
}

// ===========================================================================
// Extracted event type handlers
// ===========================================================================

namespace {

void ProcessHistoryReload()
{
    PBC_LoadHistoryFromDB();
    PBC_LoadRelationshipsFromDB();
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "HistoryReload: chat history and relationships reloaded from DB.");
    g_PBC_EventThreadDone.store(true);
}

void ProcessCondensation(PBC_EventItem& ev,
                          const std::string& condenseSysPrompt,
                          const std::string& condenseUsrTmpl)
{
    // Notify real players that a lengthy background condensation is starting
    PBC_PushNarratorSummary(ev.condensationChar.charObjGuid,
        PBC_MakeEventLine("Condensing " + ev.condensationChar.charName + "'s history..."));

    // Capture the full history snapshot BEFORE condensation truncates it
    std::deque<std::string> preCondensationHistory = ev.condensationChar.history;

    bool condensed = PBC_CondenseInline(ev.condensationChar, condenseSysPrompt, condenseUsrTmpl);

    if (!condensed)
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING,
                 "Condensation event failed for character={} — history left untouched, "
                 "will retry when threshold is reached again",
                 ev.condensationChar.charName);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    // Condensation succeeded — reload memories from DB
    PBC_LoadMemoriesFromDB();

    // Queue relationship updates for all party members
    QueueRelationshipUpdatesAfterCondensation(ev.condensationChar, preCondensationHistory);

    g_PBC_EventThreadDone.store(true);
}

void ProcessRelationshipUpdate(PBC_EventItem& ev)
{
    if (ev.relationshipSystemPrompt.empty() || ev.relationshipUserPromptTmpl.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "RelationshipUpdate: prompts not configured, skipping for character={}",
                 ev.relationshipChar.charName);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    // Notify real players that a lengthy background relationship update is starting
    PBC_PushNarratorSummary(ev.relationshipChar.charObjGuid,
        PBC_MakeEventLine("Updating " + ev.relationshipChar.charName + "'s relationships..."));

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "RelationshipUpdate: character={} target={}",
             ev.relationshipChar.charName, ev.relationshipTargetName);

    std::string userPrompt = ev.relationshipUserPromptTmpl;
    PBC_ExpandNewlineEscapes(userPrompt);

    PBC_ReplaceToken(userPrompt, "character_card",           ev.relationshipChar.characterCard);
    { std::ostringstream histOss; for (const auto& line : ev.relationshipChar.history) histOss << line << "\n"; PBC_ReplaceToken(userPrompt, "chat_history", histOss.str()); }
    PBC_ReplaceToken(userPrompt, "relationship_target",      ev.relationshipTargetInfo);
    PBC_ReplaceToken(userPrompt, "target_current_relationship", ev.relationshipCurrentText);

    PBC_LLMResult res = g_PBC_UseAltModelForRelationshipUpdate
        ? PBC_CallLLMAlt(ev.relationshipSystemPrompt, userPrompt, /*maxTokensOverride=*/-1)
        : PBC_CallLLM(ev.relationshipSystemPrompt, userPrompt, /*maxTokensOverride=*/-1);

    if (!res.success || res.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "RelationshipUpdate: LLM failed for character={} target={}",
                 ev.relationshipChar.charName, ev.relationshipTargetName);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
        auto& entry = g_PBC_Relationships[ev.relationshipChar.charGuidRaw][ev.relationshipTargetName];
        entry.text = res.text;
        entry.updatedAt = PBC_FormatDateTime(std::time(nullptr));
    }
    DB_UpsertRelationship(ev.relationshipChar.charGuidRaw, ev.relationshipTargetName, res.text);
    PBC_WsNotify(ev.relationshipChar.charGuidRaw, "relationship");

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "RelationshipUpdate: updated character={} target={} text=\"{}\"",
             ev.relationshipChar.charName, ev.relationshipTargetName,
             PBC_SanitizeForFmt(res.text));

    g_PBC_EventThreadDone.store(true);
}

void ProcessCardAdditionsMigration(PBC_EventItem& ev)
{
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "CardAdditionsMigration: starting...");

    QueryResult addResult = CharacterDatabase.Query(
        "SELECT bot_guid, addition FROM mod_pbc_character_card_additions ORDER BY bot_guid ASC, id ASC"
    );

    if (!addResult)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "CardAdditionsMigration: no card additions found, nothing to migrate.");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    std::unordered_map<uint64_t, std::vector<std::string>> additionsByBot;
    uint32_t totalAdditions = 0;
    do
    {
        uint64_t    botGuid  = (*addResult)[0].Get<uint64_t>();
        std::string addition = (*addResult)[1].Get<std::string>();
        additionsByBot[botGuid].push_back(std::move(addition));
        ++totalAdditions;
    } while (addResult->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "CardAdditionsMigration: found {} additions across {} characters",
             totalAdditions, additionsByBot.size());

    uint32_t processed = 0;
    uint32_t totalMemories = 0;

    for (auto& [botGuid, additions] : additionsByBot)
    {
        std::string charName;
        QueryResult nameResult = CharacterDatabase.Query(
            "SELECT name FROM characters WHERE guid = {}", botGuid
        );
        if (nameResult)
            charName = (*nameResult)[0].Get<std::string>();

        std::string charCard;
        if (!charName.empty())
        {
            auto cardIt = g_PBC_CharacterCards.find(charName);
            if (cardIt != g_PBC_CharacterCards.end())
                charCard = cardIt->second;
        }
        if (charCard.empty())
            charCard = g_PBC_DefaultCharacterDescription;

        PBC_CharacterSnapshot snap;
        snap.charGuidRaw   = botGuid;
        snap.charName      = charName;
        snap.characterCard = charCard;
        for (const auto& addition : additions)
            snap.history.push_back(addition);

        std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(
            snap, ev.migrationCondensationUserPromptTmpl);

        PBC_LLMResult res = g_PBC_UseAltModelForCondensation
            ? PBC_CallLLMAlt(ev.migrationCondensationSystemPrompt, userPrompt, /*maxTokensOverride=*/-1, /*preserveNewlines=*/true)
            : PBC_CallLLM(ev.migrationCondensationSystemPrompt, userPrompt, /*maxTokensOverride=*/-1, /*preserveNewlines=*/true);

        if (!res.success || res.text.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                     "CardAdditionsMigration: LLM failed for character={}, skipping",
                     charName.empty() ? fmt::format("guid:{}", botGuid) : charName);
            processed += additions.size();
            continue;
        }

        int memCount = PBC_ParseMemoryLines(res.text, botGuid);
        totalMemories += memCount;
        processed += additions.size();

        PBC_Log(PBC_LogLevel::PBC_DEFAULT,
                 "CardAdditionsMigration: character={} additions={} memories_extracted={} progress={}/{}",
                 charName.empty() ? fmt::format("guid:{}", botGuid) : charName,
                 additions.size(), memCount, processed, totalAdditions);
    }

    PBC_LoadMemoriesFromDB();

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
             "CardAdditionsMigration: complete. {} additions processed, {} memories created.",
             totalAdditions, totalMemories);

    g_PBC_EventThreadDone.store(true);
}

void ProcessCombatSummarization(PBC_EventItem& ev)
{
    if (ev.combatSystemPrompt.empty() || ev.combatUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: CombatSummarization prompts empty, skipping");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    PBC_LLMResult summary = PBC_CallLLM(ev.combatSystemPrompt, ev.combatUserPrompt);
    if (!summary.success || summary.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: CombatSummarization LLM failed");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    ev.eventLine = PBC_MakeEventLine(summary.text);
    ev.source.narratorText = summary.text;

    PBC_PushNarratorSummary(ev.anchorObjGuid, ev.eventLine);
}

void ProcessQuestSummarization(PBC_EventItem& ev)
{
    if (ev.questSystemPrompt.empty() || ev.questUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: QuestSummarization prompts empty, skipping");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    PBC_LLMResult summary = PBC_CallLLM(ev.questSystemPrompt, ev.questUserPrompt);
    if (!summary.success || summary.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: QuestSummarization LLM failed");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    ev.eventLine = PBC_MakeEventLine(summary.text);
    ev.source.narratorText = summary.text;

    PBC_PushNarratorSummary(ev.anchorObjGuid, ev.eventLine);
}

struct PendingReply {
    uint64_t    authorGuid;
    uint64_t    targetGuid;   // For whispers: the recipient. 0 otherwise.
    uint8_t     chatType;
    std::string messageText;  // Raw text (no speaker prefix)
};

void ProcessNormal(PBC_EventItem& ev,
                   const std::string& sysPrompt,
                   const std::string& condenseSysPrompt,
                   const std::string& condenseUsrTmpl)
{
    PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessEvent: type={} respondingChars={} silentChars={} event=\"{}\"",
             static_cast<int>(ev.type), ev.respondingChars.size(), ev.silentCharGuids.size(), ev.eventLine);

    std::string currentEvent = ev.eventLine;

    uint64_t    lastResponderGuid = 0;
    std::string lastEventLine;

    std::vector<PendingReply> replies;

    for (PBC_CharacterSnapshot& snap : ev.respondingChars)
    {
        // Post deferred "thinks..." notification
        if (g_PBC_DisplayNarratorEvents)
        {
            PBC_PendingAction action;
            action.charGuid          = snap.charObjGuid;
            action.text              = PBC_MakeEventLine(snap.charName + " thinks...");
            action.isNarratorMessage = true;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(action));
        }
        PBC_WsNotify(snap.charGuidRaw, "thinks");

        // Condense inline if over token budget
        int histTokens = PBC_EstimateHistoryTokens(snap.charGuidRaw);
        if (histTokens > static_cast<int>(g_PBC_MaxHistoryCtx))
        {
            PBC_PushNarratorSummary(snap.charObjGuid,
                PBC_MakeEventLine("Condensing " + snap.charName + "'s history..."));

            std::deque<std::string> preCondensationHistory = snap.history;

            bool condensed = PBC_CondenseInline(snap, condenseSysPrompt, condenseUsrTmpl);
            if (condensed)
            {
                PBC_LoadMemoriesFromDB();
                QueueRelationshipUpdatesAfterCondensation(snap, preCondensationHistory);
            }
        }

        // Build user prompt from snapshot
        std::string userPrompt = PBC_BuildUserPromptFromSnapshot(snap, currentEvent);

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessEvent: calling LLM for character={} event=\"{}\"",
                 snap.charName, currentEvent);

        PBC_LLMResult res = PBC_CallLLM(sysPrompt, userPrompt);

        if (!res.success || res.text.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: LLM failed/empty for character={}", snap.charName);
            continue;
        }

        // Collect structured reply data (no pre-rendering)
        PendingReply reply;
        reply.authorGuid  = snap.charGuidRaw;
        reply.chatType    = static_cast<uint8_t>(ev.chatType);
        reply.messageText = res.text;
        if (ev.chatType == CHAT_MSG_WHISPER && !snap.whisperTargetGuid.IsEmpty())
            reply.targetGuid = snap.whisperTargetGuid.GetCounter();
        else
            reply.targetGuid = 0;

        // Append source-derived histLine to the snapshot's local history copy
        // so subsequent responders see it in their chain context
        std::string histLine = PBC_MakeHistLineFromSource(ev.source);
        if (!histLine.empty())
            snap.history.push_back(histLine);

        replies.push_back(std::move(reply));

        // -----------------------------------------------------------------
        // Send immediate WS preview (id=0) — pre-render for each recipient
        // -----------------------------------------------------------------
        {
            PBC_HistoryEntry previewEntry;
            previewEntry.id         = 0;
            previewEntry.authorGuid = snap.charGuidRaw;
            previewEntry.type       = static_cast<uint8_t>(ev.chatType);
            previewEntry.message    = res.text;

            // Preview for all recipients (responding chars, silent, replyOnly, players)
            std::unordered_set<uint64_t> previewTargets;
            for (const PBC_CharacterSnapshot& rs : ev.respondingChars)
                previewTargets.insert(rs.charGuidRaw);
            if (ev.chatType != CHAT_MSG_WHISPER)
            {
                for (uint64_t g : ev.silentCharGuids)   previewTargets.insert(g);
                for (uint64_t g : ev.replyOnlyCharGuids) previewTargets.insert(g);
                for (uint64_t g : ev.playerCharGuids)    previewTargets.insert(g);
            }
            else
            {
                // Whispers: only sender + target see them
                previewTargets.clear();
                previewTargets.insert(snap.charGuidRaw);
                if (!snap.whisperTargetGuid.IsEmpty())
                    previewTargets.insert(snap.whisperTargetGuid.GetCounter());
                for (uint64_t g : ev.playerCharGuids)
                    previewTargets.insert(g);
            }

            for (uint64_t targetGuid : previewTargets)
            {
                std::string rendered = PBC_RenderHistoryLine(previewEntry, targetGuid);
                PBC_WsNotifyHistoryPreview(targetGuid, rendered);
            }
        }

        // Split reply into narrator/regular segments if narrator events enabled
        if (g_PBC_DisplayNarratorEvents)
        {
            auto segments = ParseNarratorSpans(res.text);
            PushReplySegments(snap, ev, segments);
        }
        else
        {
            if (!res.text.empty())
            {
                PBC_PendingAction action;
                action.charGuid    = snap.charObjGuid;
                action.targetGuid  = snap.whisperTargetGuid;
                action.chatType    = ev.chatType;
                action.text        = res.text;

                std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
                g_PBC_PendingActions.push(std::move(action));
            }
        }

        // Advance the chain
        currentEvent = snap.charName + " says: " + res.text;

        lastResponderGuid = snap.charGuidRaw;
        lastEventLine     = currentEvent;

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessEvent: character={} replied", snap.charName);
    }

    // -----------------------------------------------------------------------
    // Deferred structured history writes
    // -----------------------------------------------------------------------

    // 1. Original source event for all participants
    if (ev.source.HasSource())
    {
        // Collect all unique participant GUIDs
        std::vector<uint64_t> allOwners;
        for (const PBC_CharacterSnapshot& snap : ev.respondingChars)
            allOwners.push_back(snap.charGuidRaw);
        for (uint64_t g : ev.silentCharGuids)
            allOwners.push_back(g);
        for (uint64_t g : ev.replyOnlyCharGuids)
            allOwners.push_back(g);
        for (uint64_t g : ev.playerCharGuids)
            allOwners.push_back(g);
        std::sort(allOwners.begin(), allOwners.end());
        allOwners.erase(std::unique(allOwners.begin(), allOwners.end()), allOwners.end());

        if (ev.source.IsChat())
        {
            // Chat event from a known sender — store as a proper
            // chat entry so PBC_RenderHistoryLine produces "Name: message".
            PBC_AppendHistoryMessage(ev.source.senderGuid, ev.chatType,
                                     ev.source.message, allOwners);
        }
        else if (ev.source.IsNarrator())
        {
            // Narrator event — store raw text; PBC_RenderHistoryLine
            // will wrap it as "Narrator: *text*" for display.
            PBC_AppendHistoryMessage(0, 0, ev.source.narratorText, allOwners);
        }
    }

    // 2. Reply messages — one per reply, assigned to appropriate owners
    for (const auto& reply : replies)
    {
        std::vector<uint64_t> owners;

        if (reply.chatType == CHAT_MSG_WHISPER)
        {
            // Whispers: only sender and recipient see them
            owners = {reply.authorGuid, reply.targetGuid};
        }
        else
        {
            // Public chat: all participants
            for (const PBC_CharacterSnapshot& snap : ev.respondingChars)
                owners.push_back(snap.charGuidRaw);
            for (uint64_t g : ev.silentCharGuids)
                owners.push_back(g);
            for (uint64_t g : ev.replyOnlyCharGuids)
                owners.push_back(g);
            for (uint64_t g : ev.playerCharGuids)
                owners.push_back(g);
        }

        // Deduplicate owner list
        std::sort(owners.begin(), owners.end());
        owners.erase(std::unique(owners.begin(), owners.end()), owners.end());

        PBC_AppendHistoryMessage(reply.authorGuid, reply.chatType,
                                 reply.messageText, owners);
    }

    // -----------------------------------------------------------------------
    // Secondary event
    // -----------------------------------------------------------------------
    if (ev.canCreateEvents && lastResponderGuid != 0 && !replies.empty())
    {
        std::unordered_set<uint64_t> excluded;
        for (const auto& rs : ev.respondingChars)
            excluded.insert(rs.charGuidRaw);
        for (uint64_t g : ev.silentCharGuids)
            excluded.insert(g);

        PBC_PendingEventRequest req;
        req.eventLine           = lastEventLine;
        req.source              = ev.source;
        req.chatType            = ev.chatType;
        req.anchorCharGuid     = lastResponderGuid;
        for (const auto& rs : ev.respondingChars)
            req.originCharGuids.push_back(rs.charGuidRaw);
        req.playerCharGuids   = ev.playerCharGuids;
        req.excludedCharGuids = std::move(excluded);

        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingEventRequestsMutex);
            g_PBC_PendingEventRequests.push(std::move(req));
        }

        PBC_Log(PBC_LogLevel::PBC_DEBUG,
                 "ProcessEvent: queued secondary event from last responder guid={} "
                 "excluded={} silent={} event=\"{}\"",
                 lastResponderGuid, req.excludedCharGuids.size(), ev.silentCharGuids.size(), lastEventLine);
    }
}

} // anonymous namespace

// ===========================================================================
// PBC_ProcessEventItem
// ===========================================================================

void PBC_ProcessEventItem(PBC_EventItem ev)
{
    // -------------------------------------------------------------------
    // Insert time-gap narrator lines BEFORE any event-type-specific
    // processing, so every character (responding, silent, or undergoing
    // condensation/relationship-update) knows about the time gap before
    // the LLM prompt is built.  PBC_MaybeInsertTimeGap now handles its
    // own DB and in-memory writes directly.
    // -------------------------------------------------------------------
    bool incomingIsWhisper = (ev.chatType == CHAT_MSG_WHISPER);

    for (PBC_CharacterSnapshot& snap : ev.respondingChars)
    {
        if (PBC_MaybeInsertTimeGap(snap.charGuidRaw, incomingIsWhisper))
            snap.history.push_back("Narrator: *some time passes*");
    }
    for (uint64_t guid : ev.silentCharGuids)
        PBC_MaybeInsertTimeGap(guid, incomingIsWhisper);

    if (ev.type == PBC_EventType::Condensation)
    {
        if (PBC_MaybeInsertTimeGap(ev.condensationChar.charGuidRaw, false))
            ev.condensationChar.history.push_back("Narrator: *some time passes*");
    }
    if (ev.type == PBC_EventType::RelationshipUpdate)
    {
        if (PBC_MaybeInsertTimeGap(ev.relationshipChar.charGuidRaw, false))
            ev.relationshipChar.history.push_back("Narrator: *some time passes*");
    }

    // Capture config strings (read-only, safe without lock)
    std::string sysPrompt         = g_PBC_SystemPrompt;
    std::string condenseSysPrompt = g_PBC_CondensationSystemPrompt;
    std::string condenseUsrTmpl   = g_PBC_CondensationUserPrompt;

    // -----------------------------------------------------------------------
    // HistoryReload
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::HistoryReload)
    {
        ProcessHistoryReload();
        return;
    }

    // -----------------------------------------------------------------------
    // Condensation event
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::Condensation)
    {
        ProcessCondensation(ev, condenseSysPrompt, condenseUsrTmpl);
        return;
    }

    // -----------------------------------------------------------------------
    // RelationshipUpdate event
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::RelationshipUpdate)
    {
        ProcessRelationshipUpdate(ev);
        return;
    }

    // -----------------------------------------------------------------------
    // CardAdditionsMigration
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::CardAdditionsMigration)
    {
        ProcessCardAdditionsMigration(ev);
        return;
    }

    // -----------------------------------------------------------------------
    // CombatSummarization: generate summary, then fall through to Normal
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::CombatSummarization)
    {
        ProcessCombatSummarization(ev);
        // Fall through to Normal processing
    }

    // -----------------------------------------------------------------------
    // QuestSummarization: generate summary, then fall through to Normal
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::QuestSummarization)
    {
        ProcessQuestSummarization(ev);
        // Fall through to Normal processing
    }

    // -----------------------------------------------------------------------
    // Normal event processing
    // -----------------------------------------------------------------------
    ProcessNormal(ev, sysPrompt, condenseSysPrompt, condenseUsrTmpl);

    g_PBC_EventThreadDone.store(true);
}
