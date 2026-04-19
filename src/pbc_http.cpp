#include "pbc_http.h"
#include "pbc_config.h"
#include "Log.h"

#include <httplib.h>
#include <regex>
#include <sstream>

PBC_HttpClient::PBC_HttpClient() : m_timeoutSec(120) {}

void PBC_HttpClient::SetTimeoutSeconds(int seconds)
{
    m_timeoutSec = seconds;
}

std::string PBC_HttpClient::Post(const std::string& url,
                                 const std::string& jsonData,
                                 const std::string& apiKey)
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
        if (!apiKey.empty())
            headers.emplace("Authorization", "Bearer " + apiKey);

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
                LOG_INFO("server.loading", "[PBC] Response body: {}", res->body);
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
