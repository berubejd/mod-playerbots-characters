#include "pbc_http.h"
#include "pbc_config.h"
#include "pbc_utils.h"
#include "Log.h"

#include <httplib.h>
#include <regex>
#include <sstream>
#include <thread>
#include <atomic>
#include <memory>

// ---------------------------------------------------------------------------
// PBC_HttpClient — outbound HTTP/HTTPS POST wrapper
// ---------------------------------------------------------------------------

PBC_HttpClient::PBC_HttpClient() : m_timeoutSec(120) {}

void PBC_HttpClient::SetTimeoutSeconds(int seconds)
{
    m_timeoutSec = seconds;
}

std::string PBC_HttpClient::Post(const std::string& url,
                                 const std::string& jsonData,
                                 const std::vector<std::pair<std::string, std::string>>& extraHeaders)
{
    try
    {
        // Parse URL  e.g. https://api.deepseek.com/v1/chat/completions
        static const std::regex urlRe(R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)");
        std::smatch m;
        if (!std::regex_match(url, m, urlRe))
        {
            LOG_ERROR("server.loading", "[PBC] Invalid URL: {}", url);
            return "";
        }

        std::string proto  = m[1].str();
        std::string host   = m[2].str();
        std::string path   = m[4].matched ? m[4].str() : "/";
        int         port   = proto == "https" ? 443 : 80;
        if (m[3].matched) port = std::stoi(m[3].str());

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] HTTP {} {}:{}{}", proto, host, port, path);

        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Accept",       "application/json"},
            {"User-Agent",   "AzerothCore-PBC/1.0"}
        };
        for (const auto& hdr : extraHeaders)
            headers.emplace(hdr.first, hdr.second);

        httplib::Result res;

        if (proto == "https")
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(host, port);
            cli.enable_server_certificate_verification(false);
            cli.set_connection_timeout(m_timeoutSec);
            cli.set_read_timeout(m_timeoutSec);
            cli.set_write_timeout(m_timeoutSec);
            res = cli.Post(path, headers, jsonData, "application/json");
#else
            LOG_ERROR("server.loading", "[PBC] HTTPS requested but OpenSSL not compiled in.");
            return "";
#endif
        }
        else
        {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(m_timeoutSec);
            cli.set_read_timeout(m_timeoutSec);
            cli.set_write_timeout(m_timeoutSec);
            res = cli.Post(path, headers, jsonData, "application/json");
        }

        if (!res)
        {
            LOG_ERROR("server.loading", "[PBC] HTTP request failed (no response) for {}:{}{}", host, port, path);
            return "";
        }
        if (res->status != 200)
        {
            LOG_ERROR("server.loading", "[PBC] HTTP {} from {}:{}{}", res->status, host, port, path);
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Response body: {}", PBC_SanitizeForFmt(res->body));
            return "";
        }

        if (g_PBC_DebugEnabled)
            LOG_INFO("server.loading", "[PBC] HTTP OK, body length={}", res->body.size());

        return res->body;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR("server.loading", "[PBC] HTTP exception: {}", ex.what());
        return "";
    }
}

// ---------------------------------------------------------------------------
// PBC_HttpServer — inbound HTTP/WS server
//
// All state is kept in file-scope statics so the header stays lightweight
// (no httplib.h include needed).
// ---------------------------------------------------------------------------

static std::unique_ptr<httplib::Server>  s_httpServer;
static std::unique_ptr<std::thread>      s_httpThread;
static std::atomic<bool>                 s_httpRunning{false};

bool PBC_HttpServerStart(const std::string& bindAddr, int port, int timeoutSec)
{
    if (port <= 0)
        return false;

    try
    {
        auto svr = std::make_unique<httplib::Server>();

        // --- HTTP catch-all: return "hello" for any GET request ---
        svr->Get("/", [](const httplib::Request& /*req*/, httplib::Response& res) {
            res.set_content("hello", "text/plain");
        });

        // --- WebSocket endpoint: send "hello" on connect and on each message ---
        svr->WebSocket("/ws", [](const httplib::Request& req, httplib::ws::WebSocket& ws) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] WS: new connection from {}", req.remote_addr);

            ws.send("hello");

            std::string msg;
            while (ws.read(msg))
            {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] WS: received message, sending hello");
                ws.send("hello");
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] WS: connection closed");
        });

        // Set read/write timeouts
        svr->set_read_timeout(timeoutSec);
        svr->set_write_timeout(timeoutSec);

        // Try to bind before spawning the thread.  bind_to_port() returns
        // false on failure, which lets us handle the error gracefully.
        if (!svr->bind_to_port(bindAddr.c_str(), port))
        {
            LOG_ERROR("server.loading",
                      "[PBC] HTTP server failed to bind to {}:{} — port may be in use or address invalid. "
                      "HTTP server disabled; the rest of the module continues normally.",
                      bindAddr, port);
            return false;
        }

        // Binding succeeded — hand ownership to the file-scope unique_ptr
        // and start listening in a dedicated thread.
        s_httpServer = std::move(svr);
        s_httpRunning.store(true);

        s_httpThread = std::make_unique<std::thread>([bindAddr, port]() {
            LOG_INFO("server.loading", "[PBC] HTTP server listening on {}:{}", bindAddr, port);
            s_httpServer->listen_after_bind();
            s_httpRunning.store(false);
            LOG_INFO("server.loading", "[PBC] HTTP server stopped.");
        });

        return true;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR("server.loading",
                  "[PBC] HTTP server exception during startup: {}. HTTP server disabled; "
                  "the rest of the module continues normally.",
                  ex.what());
        s_httpServer.reset();
        s_httpRunning.store(false);
        return false;
    }
}

void PBC_HttpServerStop()
{
    if (s_httpServer)
    {
        s_httpServer->stop();
    }

    if (s_httpThread && s_httpThread->joinable())
    {
        s_httpThread->join();
    }

    s_httpServer.reset();
    s_httpThread.reset();
    s_httpRunning.store(false);
}

bool PBC_HttpServerIsRunning()
{
    return s_httpRunning.load();
}
