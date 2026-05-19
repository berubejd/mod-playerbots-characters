#include "pbc_world.h"
#include "pbc_config.h"
#include "pbc_events.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_wmo_areas.h"
#include "pbc_log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Group.h"
#include "Chat.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "SharedDefines.h"
#include "GameTime.h"

// ---------------------------------------------------------------------------
// PBC_WorldScript
// ---------------------------------------------------------------------------

PBC_WorldScript::PBC_WorldScript() : WorldScript("PBC_WorldScript") {}

void PBC_WorldScript::OnStartup()
{
    PBC_LoadConfig(true);

    if (!g_PBC_Enable)
    {
        PBC_Log(PBC_LogLevel::DEFAULT, "Module is disabled, skipping initialization.");
        return;
    }

    PBC_LoadWMOAreaNames();
    PBC_LoadCharacterCards();
    PBC_LoadMemoriesFromDB();
    PBC_LoadHistoryFromDB();
    PBC_LoadRelationshipsFromDB();
    PBC_LoadCharacterDataFromDB();

    g_PBC_EventThreadDone.store(true);

    // Start the HTTP/WS server if a port is configured.
    if (g_PBC_HttpServerPort > 0)
    {
        if (!PBC_HttpServerStart(g_PBC_HttpServerBind, g_PBC_HttpServerPort, g_PBC_HttpServerTimeout))
        {
            PBC_Log(PBC_LogLevel::ERROR, "HTTP server could not be started on {}:{} — treating as disabled. "
                      "The rest of the module continues normally.",
                      g_PBC_HttpServerBind, g_PBC_HttpServerPort);
            g_PBC_HttpServerPort = 0; // treat as disabled
        }
    }
    else
    {
        PBC_Log(PBC_LogLevel::DEFAULT, "HTTP server disabled (PBC.HttpServerPort = 0).");
    }

    PBC_Log(PBC_LogLevel::DEFAULT, "Module started.");

    // Check if legacy card additions need migration to the new memories system.
    if (DB_MemoriesTableEmpty() && DB_CardAdditionsTableNotEmpty())
    {
        g_PBC_CardAdditionsMigrationNeeded = true;
        PBC_Log(PBC_LogLevel::WARNING, "Legacy card additions detected but no memories found. "
                 "Run `.chars migrate-card-additions` from the server console to migrate. "
                 "This warning will repeat every 60 seconds until the migration is performed or the `mod_pbc_character_card_additions` table is deleted.");
    }
}

void PBC_WorldScript::OnShutdown()
{
    // Stop the HTTP/WS server if it is running.
    if (PBC_HttpServerIsRunning())
    {
        PBC_Log(PBC_LogLevel::DEFAULT, "Stopping HTTP server...");
        PBC_HttpServerStop();
    }

    // History is written through to DB on every PBC_AppendHistory call,
    // so no explicit flush is needed on shutdown.
    PBC_Log(PBC_LogLevel::DEFAULT, "Module shutdown.");
}


void PBC_WorldScript::OnUpdate(uint32_t diff)
{
    if (!g_PBC_Enable) return;

    static uint32_t s_tickTimer = 0;
    if (s_tickTimer > diff)
    {
        s_tickTimer -= diff;
        return;
    }
    s_tickTimer = 100; // 100 ms gate

    // ---------------------------------------------------------------------------
    // 0. Poll party flight/location/combat state every 1 second.
    // ---------------------------------------------------------------------------
    {
        static time_t s_lastPartyPoll = 0;
        time_t now = GameTime::GetGameTime().count();
        if (s_lastPartyPoll == 0 || (now - s_lastPartyPoll) >= 1)
        {
            s_lastPartyPoll = now;
            PBC_PollPartyState();
        }
    }

    // ---------------------------------------------------------------------------
    // 0b. Trigger condensation for tracked player characters whose history
    //     exceeds the token limit.  Checked every 30 seconds.
    // ---------------------------------------------------------------------------
    if (g_PBC_TrackPlayerCharacter && g_PBC_MaxHistoryCtx > 0)
    {
        static time_t s_lastPlayerCondenseCheck = 0;
        time_t now = GameTime::GetGameTime().count();
        if (s_lastPlayerCondenseCheck == 0 || (now - s_lastPlayerCondenseCheck) >= 30)
        {
            s_lastPlayerCondenseCheck = now;

            // Walk all sessions to find real players whose history needs condensation
            WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
            for (auto const& [id, session] : sessions)
            {
                Player* player = session->GetPlayer();
                if (!player || !player->IsInWorld()) continue;

                WorldSession* sess = player->GetSession();
                if (!PBC_PTR_VALID(sess) || sess->IsBot()) continue;

                uint64_t playerGuid = player->GetGUID().GetCounter();
                int histTokens = PBC_EstimateHistoryTokens(playerGuid);
                if (histTokens > static_cast<int>(g_PBC_MaxHistoryCtx))
                {
                    PBC_Log(PBC_LogLevel::DEBUG, "OnUpdate: player character={} history tokens={} exceeds limit {}, triggering condensation",
                             player->GetName(), histTokens, g_PBC_MaxHistoryCtx);
                    PBC_TriggerCondensation(player);
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // 0c. Warn about pending card additions migration every 60 seconds.
    // ---------------------------------------------------------------------------
    if (g_PBC_CardAdditionsMigrationNeeded)
    {
        static time_t s_lastMigrationWarn = 0;
        time_t now = GameTime::GetGameTime().count();
        if (s_lastMigrationWarn == 0 || (now - s_lastMigrationWarn) >= 60)
        {
            s_lastMigrationWarn = now;
            PBC_Log(PBC_LogLevel::WARNING, "Legacy card additions detected but no memories found. "
                     "Run `.chars migrate-card-additions` from the server console to migrate.");
        }
    }

    // ---------------------------------------------------------------------------
    // 1. Drain secondary event requests posted by the event thread.
    //    The worker thread cannot do Player* lookups or take snapshots, so it
    //    posts a lightweight PBC_PendingEventRequest and we resolve it here.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingEventRequest> localReqs;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingEventRequestsMutex);
            std::swap(localReqs, g_PBC_PendingEventRequests);
        }

        while (!localReqs.empty())
        {
            PBC_PendingEventRequest& req = localReqs.front();

            // Find the anchor bot to locate the group.
            Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(req.anchorCharGuid));
            if (!anchor || !anchor->IsInWorld())
            {
                localReqs.pop();
                continue;
            }

            // Collect group bots excluding those in the excluded set.
            auto targets = PBC_FindGroupBotsExcluding(anchor, req.excludedCharGuids);

            if (!targets.empty())
            {
                // If the original event had a Narrator histLine that these bots
                // were not part of, write it to their histories now so they
                // have full context.
                if (!req.originHistLine.empty())
                {
                    for (Player* bot : targets)
                        PBC_AppendHistory(bot->GetGUID().GetCounter(), req.originHistLine);
                }

                PBC_EventItem newEv;
                newEv.type             = PBC_EventType::Normal;
                newEv.eventLine        = req.eventLine;
                newEv.histLine         = req.histLine;
                newEv.chatType         = req.chatType;
                newEv.canCreateEvents  = false; // message events never spawn further events
                // Original responders already have histLine; they only need
                // to receive any new replies produced by this secondary event.
                newEv.replyOnlyCharGuids = req.originCharGuids;
                // Player characters already have histLine from the primary event;
                // they only need new replies.  Propagated via playerCharGuids.
                newEv.playerCharGuids  = req.playerCharGuids;

                // Shuffle targets so the penalty doesn't always favour the
                // same character — same approach as the primary chat handler.
                std::shuffle(targets.begin(), targets.end(), PBC_GetRNG());

                // Apply the same penalty logic as the primary chat handler
                // (HandleChatMessage), but start with the penalty already applied
                // once — the original responder already "used" a successful roll.
                uint32 startingChance = g_PBC_ReplyChanceMessage > g_PBC_RollPenaltyOnAnswer
                    ? g_PBC_ReplyChanceMessage - g_PBC_RollPenaltyOnAnswer : 0;

                PBC_RollBotsWithPenalty(newEv, targets, startingChance, "SecondaryEvent");

                PBC_Log(PBC_LogLevel::DEBUG,
                             "OnUpdate: secondary event materialised — "
                             "targets={} responding={} silent={} event=\"{}\"",
                             targets.size(),
                             newEv.respondingChars.size(),
                             newEv.silentCharGuids.size(),
                             newEv.eventLine);

                PBC_PushEvent(std::move(newEv));
            }

            localReqs.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 1b. Drain whisper requests posted from the HTTP API thread.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingWhisperRequest> localWhispers;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingWhisperRequestsMutex);
            std::swap(localWhispers, g_PBC_PendingWhisperRequests);
        }

        while (!localWhispers.empty())
        {
            PBC_PendingWhisperRequest& wr = localWhispers.front();

            Player* sender = ObjectAccessor::FindPlayer(ObjectGuid(wr.senderGuid));
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(wr.targetGuid));

            if (!sender || !sender->IsInWorld() || !target || !target->IsInWorld())
            {
                PBC_Log(PBC_LogLevel::DEBUG, "API whisper: sender or target not online, skipping");
                localWhispers.pop();
                continue;
            }

            WorldSession* ts = target->GetSession();
            if (!ts || !ts->IsBot())
            {
                PBC_Log(PBC_LogLevel::DEBUG, "API whisper: target is not a character, skipping");
                localWhispers.pop();
                continue;
            }

            PBC_DispatchWhisperEvent(sender, target, wr.message);
            localWhispers.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 1c. Drain party message requests posted from the HTTP API thread.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingPartyMessageRequest> localMsgs;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingPartyMessageRequestsMutex);
            std::swap(localMsgs, g_PBC_PendingPartyMessageRequests);
        }

        while (!localMsgs.empty())
        {
            PBC_PendingPartyMessageRequest& pm = localMsgs.front();

            Player* sender = ObjectAccessor::FindPlayer(ObjectGuid(pm.senderGuid));
            if (!sender || !sender->IsInWorld())
            {
                PBC_Log(PBC_LogLevel::DEBUG, "API party message: sender GUID={} is not online, skipping", pm.senderGuid);
                localMsgs.pop();
                continue;
            }

            PBC_DispatchPartyMessageEvent(sender, pm.message, pm.senderName, CHAT_MSG_PARTY);
            localMsgs.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 1d. Drain trigger requests posted from the HTTP API thread.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingTriggerRequest> localTriggers;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingTriggerRequestsMutex);
            std::swap(localTriggers, g_PBC_PendingTriggerRequests);
        }

        while (!localTriggers.empty())
        {
            PBC_PendingTriggerRequest& tr = localTriggers.front();

            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(tr.targetGuid));
            if (!target || !target->IsInWorld())
            {
                PBC_Log(PBC_LogLevel::DEBUG, "API trigger: target GUID={} is not online, skipping", tr.targetGuid);
                localTriggers.pop();
                continue;
            }

            WorldSession* ts = target->GetSession();
            if (!ts || !ts->IsBot())
            {
                PBC_Log(PBC_LogLevel::DEBUG, "API trigger: target GUID={} is not a character, skipping", tr.targetGuid);
                localTriggers.pop();
                continue;
            }

            PBC_DispatchTriggerEvent(target);
            localTriggers.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 2. Drain completed chat-send actions from the event thread.
    // ---------------------------------------------------------------------------
    {
        std::queue<PBC_PendingAction> local;
        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            std::swap(local, g_PBC_PendingActions);
        }
        while (!local.empty())
        {
            PBC_PendingAction& action = local.front();

            if (!action.text.empty())
            {
                Player* bot = ObjectAccessor::FindPlayer(action.charGuid);

                // Narrator system message (e.g. "thinks..." notification or a
                // leading *text* block from the LLM reply) — send to all real
                // players in the bot's group.  If the bot is gone, skip it.
                if (action.isNarratorMessage)
                {
                    if (bot && bot->IsInWorld())
                        PBC_NotifyRealPlayersInGroup(bot, action.text);
                    local.pop();
                    continue;
                }

                if (bot && bot->IsInWorld())
                {
                    uint32_t ct = action.chatType;

                    if (ct == CHAT_MSG_WHISPER && !action.targetGuid.IsEmpty())
                    {
                        Player* target = ObjectAccessor::FindPlayer(action.targetGuid);
                        if (target)
                            bot->Whisper(action.text, LANG_UNIVERSAL, target);
                    }
                    else if (ct == CHAT_MSG_PARTY || ct == CHAT_MSG_PARTY_LEADER)
                    {
                        Group* grp = bot->GetGroup();
                        if (grp)
                        {
                            // Bots are never party leaders — always send as CHAT_MSG_PARTY
                            // regardless of whether the original message was CHAT_MSG_PARTY_LEADER.
                            WorldPacket data;
                            ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, LANG_UNIVERSAL, bot, nullptr, action.text);
                            grp->BroadcastPacket(&data, false, grp->GetMemberGroup(bot->GetGUID()));
                        }
                        else
                        {
                            bot->Say(action.text, LANG_UNIVERSAL);
                        }
                    }
                    else if (ct == CHAT_MSG_RAID || ct == CHAT_MSG_RAID_LEADER || ct == CHAT_MSG_RAID_WARNING)
                    {
                        Group* grp = bot->GetGroup();
                        if (grp)
                        {
                            WorldPacket data;
                            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, LANG_UNIVERSAL, bot, nullptr, action.text);
                            grp->BroadcastPacket(&data, false);
                        }
                        else
                        {
                            bot->Say(action.text, LANG_UNIVERSAL);
                        }
                    }
                    else if (ct == CHAT_MSG_YELL)
                    {
                        bot->Yell(action.text, LANG_UNIVERSAL);
                    }
                    else
                    {
                        bot->Say(action.text, LANG_UNIVERSAL);
                    }

                    PBC_Log(PBC_LogLevel::DEBUG, "OnUpdate: sent chat for character={} type={}",
                                 bot->GetName(), ct);
                }
            }

            local.pop();
        }
    }

    // ---------------------------------------------------------------------------
    // 3. Spawn next event thread if the previous one has finished.
    // ---------------------------------------------------------------------------
    if (g_PBC_EventThreadDone.load())
    {
        PBC_EventItem nextEvent;
        bool hasEvent = false;

        {
            std::lock_guard<std::mutex> lock(g_PBC_EventQueueMutex);
            if (!g_PBC_EventQueue.empty())
            {
                nextEvent = std::move(g_PBC_EventQueue.front());
                g_PBC_EventQueue.pop();
                hasEvent = true;
            }
        }

        if (hasEvent)
        {
            g_PBC_EventThreadDone.store(false);

            switch (nextEvent.type)
            {
                case PBC_EventType::Normal:
                case PBC_EventType::QuestSummarization:
                    PBC_Log(PBC_LogLevel::DEBUG, "OnUpdate: spawning event thread for type={} event=\"{}\"",
                             static_cast<int>(nextEvent.type), nextEvent.eventLine);
                    break;
                case PBC_EventType::Condensation:
                    PBC_Log(PBC_LogLevel::DEBUG, "OnUpdate: spawning event thread for type=Condensation character=\"{}\"",
                             nextEvent.condensationChar.charName);
                    break;
                case PBC_EventType::HistoryReload:
                    PBC_Log(PBC_LogLevel::DEBUG, "OnUpdate: spawning event thread for type=HistoryReload");
                    break;
                case PBC_EventType::RelationshipUpdate:
                    PBC_Log(PBC_LogLevel::DEBUG, "OnUpdate: spawning event thread for type=RelationshipUpdate character=\"{}\" target=\"{}\"",
                             nextEvent.relationshipChar.charName, nextEvent.relationshipTargetName);
                    break;
            }

            std::thread([ev = std::move(nextEvent)]() mutable {
                PBC_ProcessEventItem(std::move(ev));
            }).detach();
        }
    }

}
