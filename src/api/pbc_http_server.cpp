#include "pbc_http.h"
#include "pbc_http_auth.h"
#include "pbc_http_handlers.h"
#include "pbc_config.h"
#include "pbc_log.h"

#define httplib pbc_httplib
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <fstream>
#include <regex>

#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "WorldSession.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ===========================================================================
// Server globals
// ===========================================================================

static std::unique_ptr<httplib::Server>  s_httpServer;
static std::unique_ptr<std::thread>      s_httpThread;
static std::atomic<bool>                 s_httpRunning{false};
static std::atomic<bool>                 s_httpShuttingDown{false};

// ===========================================================================
// WS subscription state
// ===========================================================================

static std::mutex s_wsSubMutex;
static std::unordered_map<httplib::ws::WebSocket*, uint64_t> s_wsConnectionSubs;
static std::unordered_map<uint64_t, std::unordered_set<httplib::ws::WebSocket*>> s_wsGuidSubs;

// Subscribe a WS connection to a character GUID.
static void WsSubscribe(httplib::ws::WebSocket* ws, uint64_t botGuid)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    auto it = s_wsConnectionSubs.find(ws);
    if (it != s_wsConnectionSubs.end() && it->second != 0)
    {
        s_wsGuidSubs[it->second].erase(ws);
        if (s_wsGuidSubs[it->second].empty())
            s_wsGuidSubs.erase(it->second);
    }

    s_wsConnectionSubs[ws] = botGuid;
    if (botGuid != 0)
        s_wsGuidSubs[botGuid].insert(ws);
}

// Remove all subscription state for a WS connection.
static void WsUnsubscribe(httplib::ws::WebSocket* ws)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    auto it = s_wsConnectionSubs.find(ws);
    if (it != s_wsConnectionSubs.end())
    {
        if (it->second != 0)
        {
            s_wsGuidSubs[it->second].erase(ws);
            if (s_wsGuidSubs[it->second].empty())
                s_wsGuidSubs.erase(it->second);
        }
        s_wsConnectionSubs.erase(it);
    }
}

// Send a raw JSON string to all WS subscribers of a character GUID.
static void WsSendToSubscribers(uint64_t botGuid, const std::string& jsonMessage)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    auto it = s_wsGuidSubs.find(botGuid);
    if (it == s_wsGuidSubs.end())
        return;

    std::vector<httplib::ws::WebSocket*> toRemove;

    for (auto* ws : it->second)
    {
        if (!ws->send(jsonMessage))
            toRemove.push_back(ws);
    }

    for (auto* ws : toRemove)
    {
        it->second.erase(ws);
        s_wsConnectionSubs.erase(ws);
    }

    if (it->second.empty())
        s_wsGuidSubs.erase(it);
}

// ===========================================================================
// Extract token from Sec-WebSocket-Protocol header (browser WebSocket auth).
// ===========================================================================

static std::string ExtractWebSocketToken(const httplib::Request& req)
{
    std::string proto = req.get_header_value("Sec-WebSocket-Protocol");
    if (proto.empty())
        return "";

    std::vector<std::string> parts;
    std::stringstream ss(proto);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        size_t start = part.find_first_not_of(" \t");
        size_t end = part.find_last_not_of(" \t");
        if (start != std::string::npos)
            parts.push_back(part.substr(start, end - start + 1));
    }

    if (parts.size() >= 2 && parts[0] == "access_token")
        return parts[1];

    return "";
}

// ===========================================================================
// Public WS notification functions
// ===========================================================================

void PBC_WsNotify(uint64_t botGuid, const std::string& eventType)
{
    if (!s_httpRunning.load())
        return;

    json j;
    j["event"] = eventType;
    WsSendToSubscribers(botGuid, j.dump());
}

void PBC_WsNotifyHistory(uint64_t botGuid, size_t messageId, const std::string& text)
{
    if (!s_httpRunning.load())
        return;

    json j;
    j["event"] = "history";
    j["message"] = {{"id", messageId}, {"text", text}};
    WsSendToSubscribers(botGuid, j.dump());
}

// ===========================================================================
// Server lifecycle
// ===========================================================================

bool PBC_HttpServerStart(const std::string& bindAddr, int port, int timeoutSec)
{
    if (port <= 0)
        return false;

    s_httpShuttingDown.store(false);

    // Private key is required for the authorization layer
    if (g_PBC_HttpServerPrivateKey.empty())
    {
        PBC_Log(PBC_LogLevel::ERROR,
                  "HTTP server not started: PBC.HttpServerPrivateKey is not set. "
                  "A private key is required for the authorization layer.");
        return false;
    }

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    PBC_Log(PBC_LogLevel::ERROR,
              "HTTP server not started: OpenSSL is not compiled in. "
              "The authorization layer requires OpenSSL for token encryption.");
    return false;
#else
    try
    {
        auto svr = std::make_unique<httplib::Server>();

        // -------------------------------------------------------------------
        // Pre-routing handler: WebSocket authorization
        // -------------------------------------------------------------------
        svr->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res)
            -> httplib::Server::HandlerResponse
        {
            if (req.path == "/ws" && req.get_header_value("Upgrade") == "websocket")
            {
                std::string token = ExtractWebSocketToken(req);
                if (token.empty())
                {
                    res.status = 401;
                    res.set_content("{\"error\":\"Authentication required. "
                                    "Use Sec-WebSocket-Protocol: access_token, <token>\"}",
                                    "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                uint64_t guid = PBC_ValidateToken(token);
                if (guid == 0)
                {
                    res.status = 401;
                    res.set_content("{\"error\":\"Invalid or expired token\"}", "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                PBC_Log(PBC_LogLevel::DEBUG, "WS: authenticated player GUID={}", guid);
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });

        // -------------------------------------------------------------------
        // Frontend static file serving
        // -------------------------------------------------------------------
        bool frontendServing = false;
        if (!g_PBC_HttpServerFrontendPath.empty())
        {
            std::string frontendPath = g_PBC_HttpServerFrontendPath;
            std::error_code ec;
            auto canonicalPath = std::filesystem::canonical(frontendPath, ec);
            if (!ec && std::filesystem::is_directory(canonicalPath))
            {
                std::string canonicalStr = canonicalPath.string();
                svr->set_mount_point("/", canonicalStr);

                svr->set_error_handler([canonicalStr](const httplib::Request& req, httplib::Response& res) {
                    if (res.status != 404 || req.method != "GET")
                        return;

                    if (req.path.size() >= 4 && req.path.substr(0, 4) == "/api")
                        return;
                    if (req.path.size() >= 3 && req.path.substr(0, 3) == "/ws")
                        return;

                    std::string indexPath = canonicalStr + "/index.html";
                    std::error_code ec2;
                    auto resolvedIndex = std::filesystem::canonical(indexPath, ec2);
                    if (ec2 || !std::filesystem::is_regular_file(resolvedIndex))
                        return;

                    std::string resolvedStr = resolvedIndex.string();
                    if (resolvedStr.size() < canonicalStr.size() ||
                        resolvedStr.substr(0, canonicalStr.size()) != canonicalStr)
                        return;

                    std::ifstream ifs(resolvedStr, std::ios::binary);
                    if (ifs.is_open())
                    {
                        std::string content((std::istreambuf_iterator<char>(ifs)),
                                            std::istreambuf_iterator<char>());
                        res.status = 200;
                        res.set_content(content, "text/html");
                    }
                });

                frontendServing = true;
                PBC_Log(PBC_LogLevel::DEFAULT, "Frontend serving enabled from '{}' (canonical: '{}')",
                         frontendPath, canonicalStr);
            }
            else
            {
                PBC_Log(PBC_LogLevel::WARNING,
                         "Frontend path '{}' does not exist or is not a directory. "
                         "Frontend serving disabled.", frontendPath);
            }
        }

        if (!frontendServing)
        {
            // Simple health check
            svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
                HandleGetRoot(req, res);
            });
        }

        // -------------------------------------------------------------------
        // Route registration
        // -------------------------------------------------------------------

        // GET /api/token — no auth required
        svr->Get("/api/token", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            HandleGetToken(req, res);
        });

        // GET /api/player
        svr->Get("/api/player", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetPlayer(req, res, player);
        });

        // GET /api/party
        svr->Get("/api/party", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetParty(req, res, player);
        });

        // GET /api/config
        svr->Get("/api/config", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetConfig(req, res, player);
        });

        // GET /api/char/:guid/card
        svr->Get("/api/char/:guid/card", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharCard(req, res, player);
        });

        // GET /api/char/:guid/context
        svr->Get("/api/char/:guid/context", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharContext(req, res, player);
        });

        // GET /api/char/:guid/history
        svr->Get("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharHistory(req, res, player);
        });

        // POST /api/char/:guid/history
        svr->Post("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharHistory(req, res, player);
        });

        // DELETE /api/char/:guid/history
        svr->Delete("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleDeleteCharHistory(req, res, player);
        });

        // GET /api/char/:guid/memory/count
        svr->Get("/api/char/:guid/memory/count", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharMemoryCount(req, res, player);
        });

        // GET /api/char/:guid/memory
        svr->Get("/api/char/:guid/memory", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharMemory(req, res, player);
        });

        // POST /api/char/:guid/memory/:id
        svr->Post("/api/char/:guid/memory/:id", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharMemory(req, res, player);
        });

        // DELETE /api/char/:guid/memory/:id
        svr->Delete("/api/char/:guid/memory/:id", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleDeleteCharMemory(req, res, player);
        });

        // GET /api/char/:guid/relationships
        svr->Get("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharRelationships(req, res, player);
        });

        // POST /api/char/:guid/relationships
        svr->Post("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharRelationships(req, res, player);
        });

        // DELETE /api/char/:guid/relationships
        svr->Delete("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleDeleteCharRelationships(req, res, player);
        });

        // GET /api/char/:guid/data
        svr->Get("/api/char/:guid/data", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharData(req, res, player);
        });

        // POST /api/char/:guid/data
        svr->Post("/api/char/:guid/data", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharData(req, res, player);
        });

        // GET /api/char/:guid/debug/request
        svr->Get("/api/char/:guid/debug/request", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandleGetCharDebugRequest(req, res, player);
        });

        // POST /api/char/:guid/whisper
        svr->Post("/api/char/:guid/whisper", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharWhisper(req, res, player);
        });

        // POST /api/char/:guid/narrate
        svr->Post("/api/char/:guid/narrate", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharNarrate(req, res, player);
        });

        // POST /api/party/narrate
        svr->Post("/api/party/narrate", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostPartyNarrate(req, res, player);
        });

        // POST /api/char/:guid/trigger
        svr->Post("/api/char/:guid/trigger", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostCharTrigger(req, res, player);
        });

        // POST /api/party/message
        svr->Post("/api/party/message", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            Player* player = AuthenticateRequest(req, res);
            if (!player) return;
            HandlePostPartyMessage(req, res, player);
        });

        // -------------------------------------------------------------------
        // WebSocket endpoint: /ws
        // -------------------------------------------------------------------
        svr->WebSocket("/ws",
            [](const httplib::Request& req, httplib::ws::WebSocket& ws) {
                std::string token = ExtractWebSocketToken(req);
                uint64_t guid = PBC_ValidateToken(token);

                PBC_Log(PBC_LogLevel::DEBUG, "WS: new connection from {} (player GUID={})",
                             req.remote_addr, guid);

                ws.send(json({{"event", "connected"}}).dump());

                std::string msg;
                while (ws.read(msg) && !s_httpShuttingDown.load())
                {
                    if (msg.compare(0, 10, "subscribe ") == 0)
                    {
                        std::string guidStr = msg.substr(10);
                        uint64_t subGuid = 0;
                        try { subGuid = std::stoull(guidStr); } catch (...) {}

                        if (subGuid != 0)
                        {
                            WsSubscribe(&ws, subGuid);
                            ws.send(json({{"event", "subscribed"}, {"guid", subGuid}}).dump());
                            PBC_Log(PBC_LogLevel::DEBUG, "WS: player GUID={} subscribed to character GUID={}",
                                     guid, subGuid);
                        }
                        else
                        {
                            ws.send(json({{"event", "error"}, {"message", "Invalid GUID"}}).dump());
                        }
                    }
                    else if (msg == "unsubscribe")
                    {
                        WsUnsubscribe(&ws);
                        ws.send(json({{"event", "unsubscribed"}}).dump());
                        PBC_Log(PBC_LogLevel::DEBUG, "WS: player GUID={} unsubscribed", guid);
                    }
                    else
                    {
                        ws.send(json({{"event", "error"}, {"message", "Unknown command. Use: subscribe <GUID> or unsubscribe"}}).dump());
                    }
                }

                WsUnsubscribe(&ws);

                if (s_httpShuttingDown.load())
                    ws.close(httplib::ws::CloseStatus::GoingAway, "server shutting down");

                PBC_Log(PBC_LogLevel::DEBUG, "WS: connection closed (player GUID={})", guid);
            },
            [](const std::vector<std::string>& protocols) -> std::string {
                for (const auto& proto : protocols)
                {
                    if (proto == "access_token")
                        return "access_token";
                }
                return "";
            }
        );

        // Set read/write timeouts
        svr->set_read_timeout(timeoutSec);
        svr->set_write_timeout(timeoutSec);

        // Try to bind before spawning the thread
        if (!svr->bind_to_port(bindAddr.c_str(), port))
        {
            PBC_Log(PBC_LogLevel::ERROR,
                      "HTTP server failed to bind to {}:{} — port may be in use or address invalid. "
                      "HTTP server disabled; the rest of the module continues normally.",
                      bindAddr, port);
            return false;
        }

        // Binding succeeded
        s_httpServer = std::move(svr);
        s_httpRunning.store(true);

        s_httpThread = std::make_unique<std::thread>([bindAddr, port]() {
            PBC_Log(PBC_LogLevel::DEFAULT, "HTTP server listening on {}:{}", bindAddr, port);
            s_httpServer->listen_after_bind();
            s_httpRunning.store(false);
            PBC_Log(PBC_LogLevel::DEFAULT, "HTTP server stopped.");
        });

        return true;
    }
    catch (const std::exception& ex)
    {
        PBC_Log(PBC_LogLevel::ERROR,
                  "HTTP server exception during startup: {}. HTTP server disabled; "
                  "the rest of the module continues normally.",
                  ex.what());
        s_httpServer.reset();
        s_httpRunning.store(false);
        return false;
    }
#endif // CPPHTTPLIB_OPENSSL_SUPPORT
}

void PBC_HttpServerStop()
{
    s_httpShuttingDown.store(true);

    if (s_httpServer)
        s_httpServer->stop();

    if (s_httpThread && s_httpThread->joinable())
        s_httpThread->detach();

    s_httpThread.reset();
    s_httpRunning.store(false);
}

bool PBC_HttpServerIsRunning()
{
    return s_httpRunning.load();
}
