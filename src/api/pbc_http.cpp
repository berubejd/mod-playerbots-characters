#include "pbc_http.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_utils.h"
#include "Log.h"

#include <httplib.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <thread>
#include <atomic>
#include <memory>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <mutex>
#include <random>
#include <filesystem>
#include <fstream>

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#endif

#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Group.h"
#include "WorldSession.h"

#include <nlohmann/json.hpp>
#include "pbc_character.h"
#include "pbc_events.h"

using json = nlohmann::json;

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

        // Content-Type is set via the content_type parameter of cli.Post(),
        // not here — adding it to the multimap would create a duplicate header.
        httplib::Headers headers = {
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

// ===========================================================================
// Authorization layer — OTP, token encryption/decryption
// ===========================================================================

// Token lifetime: 30 days (hardcoded)
static const int64_t TOKEN_LIFETIME_SEC = 30 * 24 * 3600;

// OTP validity: 2 minutes
static const int64_t OTP_LIFETIME_SEC = 120;

// ---------------------------------------------------------------------------
// Base64 encode / decode helpers
// ---------------------------------------------------------------------------

static const char s_base64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const unsigned char* data, size_t len)
{
    std::string result;
    result.reserve(4 * ((len + 2) / 3));

    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int val = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) val |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len) val |= static_cast<unsigned int>(data[i + 2]);

        result += s_base64Chars[(val >> 18) & 0x3F];
        result += s_base64Chars[(val >> 12) & 0x3F];
        result += (i + 1 < len) ? s_base64Chars[(val >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? s_base64Chars[val & 0x3F] : '=';
    }

    return result;
}

static bool Base64Decode(const std::string& encoded, std::vector<unsigned char>& out)
{
    // Build decode table
    static bool tableBuilt = false;
    static int dtable[256];
    if (!tableBuilt)
    {
        std::fill(dtable, dtable + 256, -1);
        for (int i = 0; i < 64; i++)
            dtable[static_cast<unsigned char>(s_base64Chars[i])] = i;
        tableBuilt = true;
    }

    out.clear();
    if (encoded.size() % 4 != 0)
        return false;

    size_t padding = 0;
    if (!encoded.empty() && encoded.back() == '=') padding++;
    if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') padding++;

    out.reserve((encoded.size() / 4) * 3 - padding);

    int val = 0;
    int valb = -8;
    for (unsigned char c : encoded)
    {
        if (dtable[c] == -1)
        {
            if (c == '=')
                break;
            return false; // invalid character
        }
        val = (val << 6) + dtable[c];
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Base64URL helpers — convert between standard base64 and base64url encoding.
// Base64URL uses '-' instead of '+', '_' instead of '/', and omits '=' padding.
// This produces strings that are safe for use in WebSocket subprotocol names
// and URLs without encoding.
// ---------------------------------------------------------------------------

static std::string Base64ToBase64Url(std::string b64)
{
    for (char& c : b64)
    {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    // Remove padding
    while (!b64.empty() && b64.back() == '=')
        b64.pop_back();
    return b64;
}

static std::string Base64UrlToBase64(std::string b64url)
{
    for (char& c : b64url)
    {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // Restore padding
    switch (b64url.size() % 4)
    {
        case 2: b64url += "=="; break;
        case 3: b64url += "="; break;
        case 0: break;
        case 1: return ""; // invalid
    }
    return b64url;
}

// ---------------------------------------------------------------------------
// AES-256-CBC token encryption / decryption (requires OpenSSL)
//
// Token format: base64url( IV[16] + AES-256-CBC(GUID[8] + timestamp[8] + PKCS7_padding) )
// Base64URL uses '-' instead of '+', '_' instead of '/', and omits '=' padding,
// making tokens safe for use in WebSocket subprotocol names.
// Key derivation: SHA-256 of the private key string → 32 bytes for AES-256
// ---------------------------------------------------------------------------

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT

static std::vector<unsigned char> DeriveAESKey(const std::string& privateKey)
{
    std::vector<unsigned char> key(32);
    SHA256(reinterpret_cast<const unsigned char*>(privateKey.data()),
           privateKey.size(), key.data());
    return key;
}

// Create an encrypted token for a player GUID.
// Returns "" if OpenSSL is not available or encryption fails.
static std::string CreateToken(uint64_t playerGuid)
{
    auto key = DeriveAESKey(g_PBC_HttpServerPrivateKey);

    // Prepare plaintext: GUID (8 bytes) + timestamp (8 bytes)
    unsigned char plaintext[16];
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::memcpy(plaintext, &playerGuid, 8);
    std::memcpy(plaintext + 8, &now, 8);

    // Generate random IV
    unsigned char iv[16];
    if (RAND_bytes(iv, 16) != 1)
    {
        LOG_ERROR("server.loading", "[PBC] CreateToken: RAND_bytes failed");
        return "";
    }

    // Encrypt
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        LOG_ERROR("server.loading", "[PBC] CreateToken: EVP_CIPHER_CTX_new failed");
        return "";
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR("server.loading", "[PBC] CreateToken: EVP_EncryptInit_ex failed");
        return "";
    }

    // Output buffer: 16 bytes plaintext + up to 16 bytes PKCS7 padding
    unsigned char ciphertext[32];
    int outLen1 = 0;
    if (EVP_EncryptUpdate(ctx, ciphertext, &outLen1, plaintext, 16) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR("server.loading", "[PBC] CreateToken: EVP_EncryptUpdate failed");
        return "";
    }

    int outLen2 = 0;
    if (EVP_EncryptFinal_ex(ctx, ciphertext + outLen1, &outLen2) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR("server.loading", "[PBC] CreateToken: EVP_EncryptFinal_ex failed");
        return "";
    }

    EVP_CIPHER_CTX_free(ctx);

    // Combine IV + ciphertext
    int totalLen = outLen1 + outLen2;
    std::vector<unsigned char> tokenData(iv, iv + 16);
    tokenData.insert(tokenData.end(), ciphertext, ciphertext + totalLen);

    return Base64ToBase64Url(Base64Encode(tokenData.data(), tokenData.size()));
}

// Validate a token and extract the player GUID.
// Returns 0 on failure (invalid token, expired, decryption error).
static uint64_t ValidateToken(const std::string& token)
{
    if (token.empty())
        return 0;

    auto key = DeriveAESKey(g_PBC_HttpServerPrivateKey);

    // Convert base64url to standard base64, then decode
    std::string b64 = Base64UrlToBase64(token);
    if (b64.empty())
        return 0;
    std::vector<unsigned char> tokenData;
    if (!Base64Decode(b64, tokenData))
        return 0;

    // Need at least IV (16) + one AES block (16) = 32 bytes
    if (tokenData.size() < 32)
        return 0;

    const unsigned char* iv = tokenData.data();
    const unsigned char* ciphertext = tokenData.data() + 16;
    int cipherLen = static_cast<int>(tokenData.size()) - 16;

    // Decrypt
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }

    unsigned char plaintext[32];
    int outLen1 = 0;
    if (EVP_DecryptUpdate(ctx, plaintext, &outLen1, ciphertext, cipherLen) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }

    int outLen2 = 0;
    int ret = EVP_DecryptFinal_ex(ctx, plaintext + outLen1, &outLen2);
    EVP_CIPHER_CTX_free(ctx);

    if (ret != 1)
        return 0; // Decryption failed (wrong key or corrupted data)

    int totalLen = outLen1 + outLen2;
    if (totalLen < 16)
        return 0;

    // Extract GUID and timestamp
    uint64_t playerGuid;
    uint64_t timestamp;
    std::memcpy(&playerGuid, plaintext, 8);
    std::memcpy(&timestamp, plaintext + 8, 8);

    // Check token expiry
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (static_cast<int64_t>(timestamp) + TOKEN_LIFETIME_SEC < now)
        return 0; // Token expired

    return playerGuid;
}

#else // !CPPHTTPLIB_OPENSSL_SUPPORT

static std::string CreateToken(uint64_t /*playerGuid*/)
{
    LOG_ERROR("server.loading", "[PBC] CreateToken: OpenSSL not available — token creation impossible");
    return "";
}

static uint64_t ValidateToken(const std::string& /*token*/)
{
    return 0;
}

#endif // CPPHTTPLIB_OPENSSL_SUPPORT

// ---------------------------------------------------------------------------
// OTP storage and helpers
// ---------------------------------------------------------------------------

struct OTPEntry
{
    uint64_t playerGuid;
    time_t   expiresAt;
};

static std::mutex s_otpMutex;
static std::unordered_map<std::string, OTPEntry> s_otpStore;

// Clean up expired OTPs (must be called with s_otpMutex held)
static void CleanupExpiredOTPsLocked()
{
    time_t now = std::time(nullptr);
    for (auto it = s_otpStore.begin(); it != s_otpStore.end(); )
    {
        if (it->second.expiresAt < now)
            it = s_otpStore.erase(it);
        else
            ++it;
    }
}

std::string PBC_HttpServerGenerateOTP(uint64_t playerGuid)
{
    std::lock_guard<std::mutex> lock(s_otpMutex);
    CleanupExpiredOTPsLocked();

    std::uniform_int_distribution<int> dist(0, 999999);
    std::string otp;
    int attempts = 0;
    do
    {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "%06d", dist(PBC_GetRNG()));
        otp = buf;
        attempts++;
    } while (s_otpStore.find(otp) != s_otpStore.end() && attempts < 10);

    if (attempts >= 10)
    {
        LOG_ERROR("server.loading", "[PBC] Failed to generate unique OTP after 10 attempts");
        return "";
    }

    time_t expiresAt = std::time(nullptr) + OTP_LIFETIME_SEC;
    s_otpStore[otp] = {playerGuid, expiresAt};
    return otp;
}

// Validate and consume an OTP. Returns 0 on failure.
static uint64_t ValidateAndConsumeOTP(const std::string& otp)
{
    std::lock_guard<std::mutex> lock(s_otpMutex);
    auto it = s_otpStore.find(otp);
    if (it == s_otpStore.end())
        return 0;

    uint64_t guid = it->second.playerGuid;
    time_t expiresAt = it->second.expiresAt;

    // Remove the OTP (one-time use)
    s_otpStore.erase(it);

    // Check expiry
    if (std::time(nullptr) > expiresAt)
        return 0;

    return guid;
}

// ---------------------------------------------------------------------------
// Player info helpers (for /api/player endpoint)
//
// These duplicate the static helpers from pbc_character.cpp since those are
// file-scoped.  The implementations are trivial switch statements.
// ---------------------------------------------------------------------------

static const char* HttpGenderStr(uint8_t gender)
{
    switch (gender)
    {
        case GENDER_MALE:   return "Male";
        case GENDER_FEMALE: return "Female";
        default:            return "Unknown";
    }
}

static const char* HttpRaceStr(uint8_t race)
{
    switch (race)
    {
        case RACE_HUMAN:          return "Human";
        case RACE_ORC:            return "Orc";
        case RACE_DWARF:          return "Dwarf";
        case RACE_NIGHTELF:       return "Night Elf";
        case RACE_UNDEAD_PLAYER:  return "Undead";
        case RACE_TAUREN:         return "Tauren";
        case RACE_GNOME:          return "Gnome";
        case RACE_TROLL:          return "Troll";
        case RACE_BLOODELF:       return "Blood Elf";
        case RACE_DRAENEI:        return "Draenei";
        default:                  return "Unknown";
    }
}

static const char* HttpClassStr(uint8_t cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:      return "Warrior";
        case CLASS_PALADIN:      return "Paladin";
        case CLASS_HUNTER:       return "Hunter";
        case CLASS_ROGUE:        return "Rogue";
        case CLASS_PRIEST:       return "Priest";
        case CLASS_DEATH_KNIGHT: return "Death Knight";
        case CLASS_SHAMAN:       return "Shaman";
        case CLASS_MAGE:         return "Mage";
        case CLASS_WARLOCK:      return "Warlock";
        case CLASS_DRUID:        return "Druid";
        default:                 return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Extract Bearer token from Authorization header.
// Returns "" if the header is missing or malformed.
// ---------------------------------------------------------------------------
static std::string ExtractBearerToken(const httplib::Request& req)
{
    std::string auth = req.get_header_value("Authorization");
    if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
        return auth.substr(7);
    return "";
}

// ---------------------------------------------------------------------------
// Common API request validation helper.
//
// Validates the Bearer token, parses the "guid" path parameter, and
// optionally checks that the guid belongs to an online character (bot).
// When requireOnlinePlayer is true, also verifies that the authenticated
// player is still online — returns 410 Gone if not.
// Returns true on success (all validated fields are written to the output
// parameters).  On failure, writes an error response and returns false.
// ---------------------------------------------------------------------------
struct PBC_ApiContext
{
    uint64_t authGuid = 0;   // GUID from the bearer token
    uint64_t charGuid = 0;   // GUID from the URL path (0 if not required)
    Player*  bot      = nullptr; // Resolved bot Player* (nullptr if not required)
};

static bool PBC_ValidateApiRequest(const httplib::Request& req, httplib::Response& res,
                                    bool requireGuid, bool requireOnlineBot,
                                    PBC_ApiContext& ctx,
                                    bool requireOnlinePlayer = true)
{
    ctx = PBC_ApiContext();

    // 1. Auth
    std::string token = ExtractBearerToken(req);
    if (token.empty())
    {
        res.status = 401;
        res.set_content("{\"error\":\"Missing Authorization header. "
                        "Use Authorization: Bearer <token>\"}",
                        "application/json");
        return false;
    }

    ctx.authGuid = ValidateToken(token);
    if (ctx.authGuid == 0)
    {
        res.status = 401;
        res.set_content("{\"error\":\"Invalid or expired token\"}", "application/json");
        return false;
    }

    // 2. Online player check
    if (requireOnlinePlayer)
    {
        ObjectGuid playerObjGuid(ctx.authGuid);
        Player* authPlayer = ObjectAccessor::FindPlayer(playerObjGuid);
        if (!authPlayer)
        {
            res.status = 410;
            res.set_content("{\"error\":\"player_offline\"}", "application/json");
            return false;
        }
    }

    // 3. GUID from URL path parameter
    if (requireGuid)
    {
        std::string guidStr;
        auto it = req.path_params.find("guid");
        if (it != req.path_params.end())
            guidStr = it->second;

        if (guidStr.empty())
        {
            res.status = 400;
            res.set_content("{\"error\":\"Missing guid in URL path\"}", "application/json");
            return false;
        }

        try { ctx.charGuid = std::stoull(guidStr); } catch (...) {}
        if (ctx.charGuid == 0)
        {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid guid in URL path\"}", "application/json");
            return false;
        }
    }

    // 4. Online bot check
    if (requireOnlineBot)
    {
        ObjectGuid objGuid(ctx.charGuid);
        Player* bot = ObjectAccessor::FindPlayer(objGuid);
        if (!bot)
        {
            res.status = 404;
            res.set_content("{\"error\":\"Character is not online\"}", "application/json");
            return false;
        }

        WorldSession* session = bot->GetSession();
        if (!session || !session->IsBot())
        {
            res.status = 400;
            res.set_content("{\"error\":\"Specified guid is not a character\"}", "application/json");
            return false;
        }

        ctx.bot = bot;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Extract token from Sec-WebSocket-Protocol header (browser WebSocket auth).
//
// The browser client sends: new WebSocket(url, ['access_token', token])
// Which produces: Sec-WebSocket-Protocol: access_token, <token>
//
// We parse the comma-separated list and return the second element if the
// first is "access_token".
// ---------------------------------------------------------------------------
static std::string ExtractWebSocketToken(const httplib::Request& req)
{
    std::string proto = req.get_header_value("Sec-WebSocket-Protocol");
    if (proto.empty())
        return "";

    // Parse comma-separated list
    std::vector<std::string> parts;
    std::stringstream ss(proto);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        // Trim whitespace
        size_t start = part.find_first_not_of(" \t");
        size_t end = part.find_last_not_of(" \t");
        if (start != std::string::npos)
            parts.push_back(part.substr(start, end - start + 1));
    }

    // Expect: ["access_token", "<token>"]
    if (parts.size() >= 2 && parts[0] == "access_token")
        return parts[1];

    return "";
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
static std::atomic<bool>                 s_httpShuttingDown{false};

// ---------------------------------------------------------------------------
// WS subscription state
//
// Each WS connection can subscribe to at most one character GUID at a time.
// s_wsConnectionSubs maps a WebSocket pointer to its subscribed GUID (0 = none).
// s_wsGuidSubs maps a character GUID to the set of WebSocket pointers subscribed
// to it.  Both maps are protected by s_wsSubMutex.
// ---------------------------------------------------------------------------

static std::mutex s_wsSubMutex;
static std::unordered_map<httplib::ws::WebSocket*, uint64_t> s_wsConnectionSubs;
static std::unordered_map<uint64_t, std::unordered_set<httplib::ws::WebSocket*>> s_wsGuidSubs;

// Subscribe a WS connection to a character GUID.  Replaces any existing
// subscription for this connection.
static void WsSubscribe(httplib::ws::WebSocket* ws, uint64_t botGuid)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    // Remove old subscription if any
    auto it = s_wsConnectionSubs.find(ws);
    if (it != s_wsConnectionSubs.end() && it->second != 0)
    {
        s_wsGuidSubs[it->second].erase(ws);
        if (s_wsGuidSubs[it->second].empty())
            s_wsGuidSubs.erase(it->second);
    }

    // Set new subscription
    s_wsConnectionSubs[ws] = botGuid;
    if (botGuid != 0)
        s_wsGuidSubs[botGuid].insert(ws);
}

// Remove all subscription state for a WS connection (called on disconnect).
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

// Internal: send a raw JSON string to all WS subscribers of a character GUID.
// Holds s_wsSubMutex for the duration of sending to prevent the WebSocket
// objects from being destroyed while we're using them.
static void WsSendToSubscribers(uint64_t botGuid, const std::string& jsonMessage)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    auto it = s_wsGuidSubs.find(botGuid);
    if (it == s_wsGuidSubs.end())
        return;

    // Collect failed sends for cleanup
    std::vector<httplib::ws::WebSocket*> toRemove;

    for (auto* ws : it->second)
    {
        if (!ws->send(jsonMessage))
            toRemove.push_back(ws);
    }

    // Clean up failed connections
    for (auto* ws : toRemove)
    {
        it->second.erase(ws);
        s_wsConnectionSubs.erase(ws);
    }

    if (it->second.empty())
        s_wsGuidSubs.erase(it);
}

// ---------------------------------------------------------------------------
// Public WS notification functions
// ---------------------------------------------------------------------------

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

bool PBC_HttpServerStart(const std::string& bindAddr, int port, int timeoutSec)
{
    if (port <= 0)
        return false;

    s_httpShuttingDown.store(false);

    // Private key is required for the authorization layer
    if (g_PBC_HttpServerPrivateKey.empty())
    {
        LOG_ERROR("server.loading",
                  "[PBC] HTTP server not started: PBC.HttpServerPrivateKey is not set. "
                  "A private key is required for the authorization layer.");
        return false;
    }

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    LOG_ERROR("server.loading",
              "[PBC] HTTP server not started: OpenSSL is not compiled in. "
              "The authorization layer requires OpenSSL for token encryption.");
    return false;
#else
    try
    {
        auto svr = std::make_unique<httplib::Server>();

        // -------------------------------------------------------------------
        // Pre-routing handler: WebSocket authorization
        //
        // Rejects WebSocket upgrade requests to /ws that don't carry a valid
        // token in the Sec-WebSocket-Protocol header.  This prevents the
        // 101 upgrade response from being sent for unauthorized connections.
        // -------------------------------------------------------------------
        svr->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res)
            -> httplib::Server::HandlerResponse
        {
            // Only intercept WebSocket upgrade requests to /ws
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

                uint64_t guid = ValidateToken(token);
                if (guid == 0)
                {
                    res.status = 401;
                    res.set_content("{\"error\":\"Invalid or expired token\"}", "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                // Token is valid — let the WebSocket upgrade proceed
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] WS: authenticated player GUID={}", guid);
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });

        // -------------------------------------------------------------------
        // Frontend static file serving
        //
        // If PBC.HttpServerFrontendPath is set and the directory exists,
        // serve static files from it.  GET / serves index.html.
        // For SPA routing, any non-API, non-WS, non-file GET request
        // also serves index.html (via set_error_handler).
        //
        // If the frontend path is not configured or doesn't exist, fall
        // back to a simple "hello" health-check on GET /.
        // -------------------------------------------------------------------
        bool frontendServing = false;
        if (!g_PBC_HttpServerFrontendPath.empty())
        {
            std::string frontendPath = g_PBC_HttpServerFrontendPath;
            // Resolve to canonical path to prevent path traversal
            std::error_code ec;
            auto canonicalPath = std::filesystem::canonical(frontendPath, ec);
            if (!ec && std::filesystem::is_directory(canonicalPath))
            {
                std::string canonicalStr = canonicalPath.string();
                svr->set_mount_point("/", canonicalStr);

                // SPA fallback: when a GET request results in a 404 (no route
                // handler and no static file matched), serve index.html instead
                // for non-API, non-WS paths.  This enables client-side routing.
                svr->set_error_handler([canonicalStr](const httplib::Request& req, httplib::Response& res) {
                    // Only intercept 404 GET requests for non-API paths
                    if (res.status != 404 || req.method != "GET")
                        return;

                    // Don't intercept API or WebSocket paths
                    if (req.path.size() >= 4 && req.path.substr(0, 4) == "/api")
                        return;
                    if (req.path.size() >= 3 && req.path.substr(0, 3) == "/ws")
                        return;

                    // Serve index.html for SPA client-side routing
                    std::string indexPath = canonicalStr + "/index.html";
                    std::error_code ec2;
                    auto resolvedIndex = std::filesystem::canonical(indexPath, ec2);
                    if (ec2 || !std::filesystem::is_regular_file(resolvedIndex))
                        return; // Leave the 404 as-is

                    // Verify the resolved path is within the frontend directory
                    std::string resolvedStr = resolvedIndex.string();
                    if (resolvedStr.size() < canonicalStr.size() ||
                        resolvedStr.substr(0, canonicalStr.size()) != canonicalStr)
                        return; // Leave the 403/404 as-is

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
                LOG_INFO("server.loading", "[PBC] Frontend serving enabled from '{}' (canonical: '{}')",
                         frontendPath, canonicalStr);
            }
            else
            {
                LOG_WARN("server.loading",
                         "[PBC] Frontend path '{}' does not exist or is not a directory. "
                         "Frontend serving disabled.", frontendPath);
            }
        }

        if (!frontendServing)
        {
            // No frontend — simple health check
            svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
                res.set_content("hello", "text/plain");
            });
        }

        // -------------------------------------------------------------------
        // GET /api/token?otp=XXXXXX
        //
        // Exchanges a valid one-time password for a bearer token.
        // The token is an AES-256-CBC encrypted payload containing the
        // player GUID and a timestamp, base64-encoded.
        // -------------------------------------------------------------------
        svr->Get("/api/token", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            std::string otp = req.get_param_value("otp");
            if (otp.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing otp parameter\"}", "application/json");
                return;
            }

            uint64_t guid = ValidateAndConsumeOTP(otp);
            if (guid == 0)
            {
                res.status = 401;
                res.set_content("{\"error\":\"Invalid or expired OTP\"}", "application/json");
                return;
            }

            std::string token = CreateToken(guid);
            if (token.empty())
            {
                res.status = 500;
                res.set_content("{\"error\":\"Failed to create token\"}", "application/json");
                return;
            }

            res.set_content("{\"token\":\"" + token + "\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/player
        //
        // Returns basic info about the authenticated player.
        // Requires a valid Authorization: Bearer <token> header.
        // -------------------------------------------------------------------
        svr->Get("/api/player", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, false, false, ctx, /*requireOnlinePlayer=*/false))
                return;

            // Look up the player by GUID
            ObjectGuid objGuid(ctx.authGuid);
            Player* player = ObjectAccessor::FindPlayer(objGuid);
            if (!player)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Player is not online\"}", "application/json");
                return;
            }

            // Build JSON response with basic player info
            json response;
            response["name"]   = player->GetName();
            response["gender"] = HttpGenderStr(player->getGender());
            response["race"]   = HttpRaceStr(player->getRace());
            response["class"]  = HttpClassStr(player->getClass());
            response["level"]  = player->GetLevel();

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/party
        //
        // Returns online party members for the authenticated player.
        // Requires a valid Authorization: Bearer <token> header.
        // The authenticated player must be online.
        // -------------------------------------------------------------------
        svr->Get("/api/party", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, false, false, ctx))
                return;

            // Look up the player by GUID
            ObjectGuid objGuid(ctx.authGuid);
            Player* player = ObjectAccessor::FindPlayer(objGuid);
            if (!player)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Player is not online\"}", "application/json");
                return;
            }

            // Build party array with online group members
            json party = json::array();
            Group* grp = player->GetGroup();
            if (grp)
            {
                for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (!member || !member->IsInWorld() || member == player) continue;

                    WorldSession* ms = member->GetSession();
                    if (!ms) continue;

                    bool isCharacter = ms->IsBot();

                    json memberJson;
                    memberJson["name"]      = member->GetName();
                    memberJson["gender"]    = HttpGenderStr(member->getGender());
                    memberJson["race"]      = HttpRaceStr(member->getRace());
                    memberJson["class"]     = HttpClassStr(member->getClass());
                    memberJson["level"]     = member->GetLevel();
                    memberJson["character"] = isCharacter;
                    if (isCharacter)
                        memberJson["guid"] = member->GetGUID().GetCounter();

                    party.push_back(memberJson);
                }
            }

            json response;
            response["party"] = party;

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/narrate
        //
        // Adds a Narrator line to the specified character's history without
        // producing any character events.  Equivalent to the .chars narrate
        // command.  Triggers a "history" WS event for the character.
        //
        // Request body: JSON with a "message" field.
        //
        // Requires auth.  The target character must be online.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/narrate", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string message = body.value("message", "");
            if (message.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
                return;
            }

            std::string histLine = PBC_MakeHistLine(message);
            PBC_AppendHistory(ctx.charGuid, histLine);

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API narrate: character GUID={} message=\"{}\"", ctx.charGuid, message);

            res.set_content("{\"status\":\"ok\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/party/narrate
        //
        // Adds a Narrator line to every character in the authenticated
        // player's group without producing any character events.
        // Equivalent to the .chars narrate-party command.  Triggers a
        // "history" WS event for every character in the group.
        //
        // Request body: JSON with a "message" field.
        //
        // Requires auth.  The authenticated player must be online and in
        // a group.
        // -------------------------------------------------------------------
        svr->Post("/api/party/narrate", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, false, false, ctx))
                return;

            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string message = body.value("message", "");
            if (message.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
                return;
            }

            // Look up the player by GUID
            ObjectGuid objGuid(ctx.authGuid);
            Player* player = ObjectAccessor::FindPlayer(objGuid);
            if (!player)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Player is not online\"}", "application/json");
                return;
            }

            Group* grp = player->GetGroup();
            if (!grp)
            {
                res.status = 400;
                res.set_content("{\"error\":\"Player is not in a group\"}", "application/json");
                return;
            }

            std::string histLine = PBC_MakeHistLine(message);
            int count = 0;

            for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsInWorld()) continue;
                WorldSession* sess = member->GetSession();
                if (!sess || !sess->IsBot()) continue;

                PBC_AppendHistory(member->GetGUID().GetCounter(), histLine);
                ++count;
            }

            if (count == 0)
            {
                res.status = 400;
                res.set_content("{\"error\":\"No characters found in your group\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API party narrate: player GUID={} characters={}", ctx.authGuid, count);

            json response;
            response["status"]          = "ok";
            response["characters_count"] = count;
            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/party/message
        //
        // Emulates a party message sent by the authenticated player.
        // Adds the message to the history of all characters in the group
        // and triggers the same character answer logic as an in-game
        // party chat message (roll chance, LLM call, in-game reply).
        //
        // Request body: JSON with a "message" field.
        //
        // Requires auth.  The authenticated player must be online and in
        // a group.  Returns immediately with "queued" status.
        // -------------------------------------------------------------------
        svr->Post("/api/party/message", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, false, false, ctx))
                return;

            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string message = body.value("message", "");
            if (message.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
                return;
            }

            // Look up the player by GUID to verify they're online and in a group
            ObjectGuid objGuid(ctx.authGuid);
            Player* player = ObjectAccessor::FindPlayer(objGuid);
            if (!player)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Player is not online\"}", "application/json");
                return;
            }

            Group* grp = player->GetGroup();
            if (!grp)
            {
                res.status = 400;
                res.set_content("{\"error\":\"Player is not in a group\"}", "application/json");
                return;
            }

            // Queue the party message request for the main thread
            {
                PBC_PendingPartyMessageRequest pm;
                pm.senderName = player->GetName();
                pm.senderGuid = ctx.authGuid;
                pm.message    = message;

                std::lock_guard<std::mutex> lock(g_PBC_PendingPartyMessageRequestsMutex);
                g_PBC_PendingPartyMessageRequests.push(std::move(pm));
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API party message queued: player GUID={}", ctx.authGuid);

            res.set_content("{\"status\":\"queued\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/card
        //
        // Returns the character card (base card with variable substitution).
        // The character must be online.  Requires auth.
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/card", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            Player* bot = ctx.bot;

            json response;

            // Build character card (with variable substitution)
            const std::string& name = bot->GetName();
            auto cardIt = g_PBC_CharacterCards.find(name);
            if (cardIt != g_PBC_CharacterCards.end())
                response["card"] = PBC_SubstituteVars(cardIt->second, bot, "", false);
            else
                response["card"] = PBC_SubstituteVars(g_PBC_DefaultCharacterDescription, bot, "", false);

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/relationships
        //
        // Returns the character's current relationships as an object mapping
        // character names to relationship descriptions.  The character must
        // be online.  Requires auth.
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            json relationships = json::object();
            {
                std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
                auto relIt = g_PBC_Relationships.find(ctx.charGuid);
                if (relIt != g_PBC_Relationships.end())
                    for (const auto& kv : relIt->second)
                        relationships[kv.first] = kv.second.text;
            }

            json response;
            response["relationships"] = relationships;

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/relationships?name=
        //
        // Edit a single relationship for a character.  The relationship is
        // identified by the target character name (the key in the
        // relationships object from GET /api/char/:guid/relationships).
        // Both the in-memory relationship and the database row are updated.
        //
        // Request body: JSON with "text" (new relationship text) and
        // "original" (current text for desync detection).  If "original"
        // does not match the server's copy, 409 Conflict is returned.
        //
        // Requires auth.  The character must be online.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            // Parse name parameter (target character name)
            std::string targetName = req.get_param_value("name");
            if (targetName.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing name parameter\"}", "application/json");
                return;
            }

            // Parse JSON body
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string newText = body.value("text", "");
            if (newText.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'text' field in request body\"}", "application/json");
                return;
            }
            std::string originalText = body.value("original", "");
            if (originalText.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'original' field in request body\"}", "application/json");
                return;
            }

            PBC_HistoryResult result = PBC_UpdateRelationship(ctx.charGuid, targetName, newText, originalText);
            if (result == PBC_HistoryResult::NotFound)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Relationship not found\"}", "application/json");
                return;
            }
            if (result == PBC_HistoryResult::Desync)
            {
                res.status = 409;
                res.set_content("{\"error\":\"desync\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API relationship edit: character GUID={} target={}", ctx.charGuid, targetName);

            res.set_content("{\"status\":\"updated\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // DELETE /api/char/:guid/relationships?name=
        //
        // Delete a single relationship for a character.  The relationship is
        // identified by the target character name (the key in the
        // relationships object from GET /api/char/:guid/relationships).
        // Both the in-memory relationship and the database row are deleted.
        //
        // Request body: JSON with "original" (current text for desync
        // detection).  If "original" does not match the server's copy,
        // 409 Conflict is returned.
        //
        // Requires auth.  The character must be online.
        // -------------------------------------------------------------------
        svr->Delete("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            // Parse name parameter (target character name)
            std::string targetName = req.get_param_value("name");
            if (targetName.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing name parameter\"}", "application/json");
                return;
            }

            // Parse JSON body for desync detection
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string originalText = body.value("original", "");
            if (originalText.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'original' field in request body\"}", "application/json");
                return;
            }

            PBC_HistoryResult result = PBC_DeleteRelationship(ctx.charGuid, targetName, originalText);
            if (result == PBC_HistoryResult::NotFound)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Relationship not found\"}", "application/json");
                return;
            }
            if (result == PBC_HistoryResult::Desync)
            {
                res.status = 409;
                res.set_content("{\"error\":\"desync\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API relationship delete: character GUID={} target={}", ctx.charGuid, targetName);

            res.set_content("{\"status\":\"deleted\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/context
        //
        // Returns the fully-built context string for a character, with
        // template variable names left in place before their substituted
        // values (e.g. "{char_name}Jon" instead of just "Jon").  This
        // allows the frontend to identify which variable produced each
        // part of the output and render appropriate icons.  The LLM
        // request uses the non-annotated version.  The character must be
        // online.  Requires auth.
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/context", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            json response;
            response["context"] = PBC_SubstituteVars(g_PBC_CharacterContext, ctx.bot, "", false, true);

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/history?page=&limit=
        //
        // Returns character chat history from the in-memory cache as a JSON
        // array.  Each message is an object with "id" (1-based index) and
        // "text", ordered by order of appearance (chronological).  Pagination
        // works from the end to the beginning (most recent messages first),
        // because the recent history will be displayed first in the UI.
        //
        // Path parameters:
        //   guid  — character GUID (in URL path)
        //
        // Query parameters:
        //   page  — page number, 1-based (default 1); page 1 = most recent
        //   limit — messages per page (default 50, max 200). Omit or set to
        //           0 to return all messages (no pagination).
        //
        // Requires auth.  Works even when the character is offline (reads
        // from the in-memory history map).
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse pagination parameters
            // limit = 0 (or omitted) means return all messages (no pagination)
            int page  = 1;
            int limit = 0;

            std::string pageStr  = req.get_param_value("page");
            std::string limitStr = req.get_param_value("limit");

            if (!pageStr.empty())
            {
                try { page = std::stoi(pageStr); } catch (...) { page = 1; }
                if (page < 1) page = 1;
            }
            if (!limitStr.empty())
            {
                try { limit = std::stoi(limitStr); } catch (...) { limit = 50; }
                if (limit < 0)  limit = 0;
                if (limit > 200) limit = 200;
            }

            // Read from in-memory history (thread-safe via mutex)
            size_t total = 0;
            json messages = json::array();

            {
                std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
                auto it = g_PBC_ChatHistory.find(ctx.charGuid);
                if (it != g_PBC_ChatHistory.end())
                {
                    total = it->second.size();

                    if (total > 0)
                    {
                        if (limit == 0)
                        {
                            // No pagination: return all messages
                            for (size_t i = 0; i < total; ++i)
                            {
                                messages.push_back({{"id", i + 1}, {"text", it->second[i]}});
                            }
                        }
                        else
                        {
                            // Pagination from the end: page 1 = last `limit` messages,
                            // page 2 = the `limit` messages before that, etc.
                            // Messages within each page are in chronological order.
                            size_t skipFromEnd = static_cast<size_t>((page - 1) * limit);

                            // If this page is beyond the available data, return empty
                            if (skipFromEnd < total)
                            {
                                size_t endIdx   = total - skipFromEnd;
                                size_t startIdx = (endIdx > static_cast<size_t>(limit))
                                                  ? endIdx - static_cast<size_t>(limit)
                                                  : 0;

                                for (size_t i = startIdx; i < endIdx; ++i)
                                    messages.push_back({{"id", i + 1}, {"text", it->second[i]}});
                            }
                        }
                    }
                }
            }

            int totalPages = (limit > 0 && total > 0)
                             ? static_cast<int>((total + limit - 1) / limit)
                             : (total > 0 ? 1 : 0);

            json response;
            response["messages"]    = messages;
            response["page"]        = page;
            response["limit"]       = limit;
            response["total"]       = total;
            response["total_pages"] = totalPages;

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/whisper
        //
        // Post a private message event for a specified character.  This does
        // not produce a whisper in-game directly — instead it queues a
        // whisper request that is processed identically to an in-game
        // whisper on the main thread: the character rolls to respond, the
        // LLM is called, the reply is whispered in-game, and both the
        // incoming message and the reply are added to the character's
        // history.
        //
        // The sender is the authenticated player (from the bearer token).
        // The target is the character specified by the guid in the URL path.
        //
        // Request body: JSON with a "message" field.
        //
        // Requires auth.  The target character must be online.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/whisper", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            // Look up the sender (authenticated player) — must be online
            Player* sender = ObjectAccessor::FindPlayer(ObjectGuid(ctx.authGuid));
            if (!sender)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Sender player is not online\"}", "application/json");
                return;
            }
            std::string senderName = sender->GetName();

            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string message = body.value("message", "");
            if (message.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
                return;
            }

            // Queue the whisper request for the main thread
            {
                PBC_PendingWhisperRequest wr;
                wr.message    = message;
                wr.senderGuid = ctx.authGuid;
                wr.targetGuid = ctx.charGuid;

                std::lock_guard<std::mutex> lock(g_PBC_PendingWhisperRequestsMutex);
                g_PBC_PendingWhisperRequests.push(std::move(wr));
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API whisper queued: player GUID={} -> character GUID={}", ctx.authGuid, ctx.charGuid);

            res.set_content("{\"status\":\"queued\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/trigger
        //
        // Triggers a response from the specified character. The character
        // responds as a party message if they are in a group, or as a say
        // otherwise. The trigger event (*you feel the urge to say something*)
        // is NOT written into the character's history.
        //
        // No request body required.
        //
        // Requires auth.  The target character must be online.
        // Returns immediately with "queued" status.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/trigger", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, true, ctx))
                return;

            // Queue the trigger request for the main thread
            {
                PBC_PendingTriggerRequest tr;
                tr.targetGuid = ctx.charGuid;

                std::lock_guard<std::mutex> lock(g_PBC_PendingTriggerRequestsMutex);
                g_PBC_PendingTriggerRequests.push(std::move(tr));
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API trigger queued: character GUID={}", ctx.charGuid);

            res.set_content("{\"status\":\"queued\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/history?id=
        //
        // Edit a single message in a character's chat history.  The message
        // is identified by its 1-based id (the "id" field returned by
        // GET /api/char/:guid/history).  Both the in-memory history and the database
        // row are updated.
        //
        // Request body: JSON with "message" (new text) and "original"
        // (current text for desync detection).  If "original" does not
        // match the server's copy, 409 Conflict is returned.
        //
        // Requires auth.  The authenticated player must be online.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse id parameter (1-based, matching the "id" field from GET /api/char/:guid/history)
            std::string idStr = req.get_param_value("id");
            if (idStr.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing id parameter\"}", "application/json");
                return;
            }

            size_t id = 0;
            try { id = std::stoull(idStr); } catch (...) {}
            if (id == 0)
            {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid id parameter (must be >= 1)\"}", "application/json");
                return;
            }
            size_t index = id - 1; // Convert to 0-based index

            // Parse JSON body
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string newMessage = body.value("message", "");
            if (newMessage.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
                return;
            }
            std::string originalMessage = body.value("original", "");
            if (originalMessage.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'original' field in request body\"}", "application/json");
                return;
            }

            PBC_HistoryResult result = PBC_UpdateHistoryLine(ctx.charGuid, index, newMessage, originalMessage);
            if (result == PBC_HistoryResult::NotFound)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Message id out of range\"}", "application/json");
                return;
            }
            if (result == PBC_HistoryResult::Desync)
            {
                res.status = 409;
                res.set_content("{\"error\":\"desync\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API history edit: character GUID={} index={}", ctx.charGuid, index);

            res.set_content("{\"status\":\"updated\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // DELETE /api/char/:guid/history?id=
        //
        // Delete a single message from a character's chat history.  The
        // message is identified by its 1-based id (the "id" field
        // returned by GET /api/char/:guid/history).  Both the in-memory history and
        // the database row are deleted.
        //
        // Request body: JSON with "original" (current text for desync
        // detection).  If "original" does not match the server's copy,
        // 409 Conflict is returned.
        //
        // Requires auth.  The authenticated player must be online.
        // -------------------------------------------------------------------
        svr->Delete("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse id parameter (1-based, matching the "id" field from GET /api/char/:guid/history)
            std::string idStr = req.get_param_value("id");
            if (idStr.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing id parameter\"}", "application/json");
                return;
            }

            size_t id = 0;
            try { id = std::stoull(idStr); } catch (...) {}
            if (id == 0)
            {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid id parameter (must be >= 1)\"}", "application/json");
                return;
            }
            size_t index = id - 1; // Convert to 0-based index

            // Parse JSON body for desync detection
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string originalMessage = body.value("original", "");
            if (originalMessage.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'original' field in request body\"}", "application/json");
                return;
            }

            PBC_HistoryResult result = PBC_DeleteHistoryLine(ctx.charGuid, index, originalMessage);
            if (result == PBC_HistoryResult::NotFound)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Message id out of range\"}", "application/json");
                return;
            }
            if (result == PBC_HistoryResult::Desync)
            {
                res.status = 409;
                res.set_content("{\"error\":\"desync\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API history delete: character GUID={} index={}", ctx.charGuid, index);

            res.set_content("{\"status\":\"deleted\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/memory/count
        //
        // Returns the number of memory entries for a character.
        // Requires auth.  Works even when the character is offline.
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/memory/count", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            size_t count = 0;
            {
                std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
                auto it = g_PBC_Memories.find(ctx.charGuid);
                if (it != g_PBC_Memories.end())
                    count = it->second.size();
            }

            json response;
            response["count"] = count;
            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/memory?order_by=&order_dir=&page=&limit=
        //
        // Returns character memories from the in-memory cache as a JSON array.
        // Each memory is an object with "id" (DB row id), "memory_text", and
        // "importance".
        //
        // Query parameters:
        //   order_by  — field to sort by: "id" (default), "memory_text",
        //               "importance"
        //   order_dir — "asc" or "desc" (default "desc" for id, "desc" for
        //               importance, "asc" for memory_text)
        //   page      — page number, 1-based (default 1)
        //   limit     — items per page (1–200, default 50). Omit or set to 0
        //               to return all memories (no pagination).
        //
        // Requires auth.  Works even when the character is offline (reads
        // from the in-memory memories map).
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/memory", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse query parameters
            std::string orderBy  = req.get_param_value("order_by");
            std::string orderDir = req.get_param_value("order_dir");
            int page  = 1;
            int limit = 0;

            std::string pageStr  = req.get_param_value("page");
            std::string limitStr = req.get_param_value("limit");

            if (!pageStr.empty())
            {
                try { page = std::stoi(pageStr); } catch (...) { page = 1; }
                if (page < 1) page = 1;
            }
            if (!limitStr.empty())
            {
                try { limit = std::stoi(limitStr); } catch (...) { limit = 50; }
                if (limit < 0)  limit = 0;
                if (limit > 200) limit = 200;
            }

            // Validate order_by
            if (orderBy.empty()) orderBy = "id";
            if (orderBy != "id" && orderBy != "memory_text" && orderBy != "importance")
            {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid order_by. Must be id, memory_text, or importance\"}", "application/json");
                return;
            }

            // Validate / default order_dir
            if (orderDir.empty())
            {
                // Default direction depends on the field
                orderDir = (orderBy == "memory_text") ? "asc" : "desc";
            }
            if (orderDir != "asc" && orderDir != "desc")
            {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid order_dir. Must be asc or desc\"}", "application/json");
                return;
            }

            bool desc = (orderDir == "desc");

            // Read from in-memory store (thread-safe via mutex)
            size_t total = 0;
            json memories = json::array();

            {
                std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
                auto it = g_PBC_Memories.find(ctx.charGuid);
                if (it != g_PBC_Memories.end())
                {
                    // Build a sortable copy
                    std::vector<const PBC_MemoryEntry*> entries;
                    entries.reserve(it->second.size());
                    for (const auto& e : it->second)
                        entries.push_back(&e);

                    // Sort
                    std::sort(entries.begin(), entries.end(),
                        [orderBy, desc](const PBC_MemoryEntry* a, const PBC_MemoryEntry* b)
                        {
                            bool less;
                            if (orderBy == "id")
                                less = a->dbId < b->dbId;
                            else if (orderBy == "importance")
                                less = a->importance < b->importance;
                            else // memory_text
                                less = a->text < b->text;
                            return desc ? !less : less;
                        });

                    total = entries.size();

                    if (total > 0)
                    {
                        if (limit == 0)
                        {
                            // No pagination: return all
                            for (const auto* e : entries)
                                memories.push_back({{"id", e->dbId}, {"memory_text", e->text}, {"importance", e->importance}});
                        }
                        else
                        {
                            // Paginate from the beginning (sorted order)
                            size_t startIdx = static_cast<size_t>((page - 1) * limit);
                            if (startIdx < total)
                            {
                                size_t endIdx = std::min(startIdx + static_cast<size_t>(limit), total);
                                for (size_t i = startIdx; i < endIdx; ++i)
                                    memories.push_back({{"id", entries[i]->dbId}, {"memory_text", entries[i]->text}, {"importance", entries[i]->importance}});
                            }
                        }
                    }
                }
            }

            int totalPages = (limit > 0 && total > 0)
                             ? static_cast<int>((total + limit - 1) / limit)
                             : (total > 0 ? 1 : 0);

            json response;
            response["memories"]    = memories;
            response["page"]        = page;
            response["limit"]       = limit;
            response["total"]       = total;
            response["total_pages"] = totalPages;

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/memory/:id
        //
        // Edit a single memory for a character.  The memory is identified by
        // its DB row id (the "id" field returned by GET /api/char/:guid/memory).
        // Both the in-memory entry and the database row are updated.
        //
        // Request body: JSON with "memory_text" (new text), "importance"
        // (new importance 1-10), and "original" (current text for desync
        // detection).  If "original" does not match the server's copy,
        // 409 Conflict is returned.
        //
        // Requires auth.  Works even when the character is offline.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/memory/:id", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse memory id from URL path parameter
            std::string memIdStr;
            auto it = req.path_params.find("id");
            if (it != req.path_params.end())
                memIdStr = it->second;

            uint64_t memId = 0;
            try { memId = std::stoull(memIdStr); } catch (...) {}
            if (memId == 0)
            {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid memory id in URL path\"}", "application/json");
                return;
            }

            // Parse JSON body
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string newText = body.value("memory_text", "");
            if (newText.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'memory_text' field in request body\"}", "application/json");
                return;
            }
            uint8_t newImportance = 5;
            if (body.contains("importance"))
            {
                try { newImportance = static_cast<uint8_t>(std::clamp(body["importance"].get<int>(), 1, 10)); } catch (...) {}
            }
            std::string originalText = body.value("original", "");
            if (originalText.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'original' field in request body\"}", "application/json");
                return;
            }

            PBC_HistoryResult result = PBC_UpdateMemory(ctx.charGuid, memId, newText, newImportance, originalText);
            if (result == PBC_HistoryResult::NotFound)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Memory not found\"}", "application/json");
                return;
            }
            if (result == PBC_HistoryResult::Desync)
            {
                res.status = 409;
                res.set_content("{\"error\":\"desync\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API memory edit: character GUID={} memory id={}", ctx.charGuid, memId);

            res.set_content("{\"status\":\"updated\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // DELETE /api/char/:guid/memory/:id
        //
        // Delete a single memory for a character.  The memory is identified
        // by its DB row id (the "id" field returned by
        // GET /api/char/:guid/memory).  Both the in-memory entry and the
        // database row are deleted.
        //
        // Request body: JSON with "original" (current text for desync
        // detection).  If "original" does not match the server's copy,
        // 409 Conflict is returned.
        //
        // Requires auth.  Works even when the character is offline.
        // -------------------------------------------------------------------
        svr->Delete("/api/char/:guid/memory/:id", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse memory id from URL path parameter
            std::string memIdStr;
            auto it = req.path_params.find("id");
            if (it != req.path_params.end())
                memIdStr = it->second;

            uint64_t memId = 0;
            try { memId = std::stoull(memIdStr); } catch (...) {}
            if (memId == 0)
            {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid memory id in URL path\"}", "application/json");
                return;
            }

            // Parse JSON body for desync detection
            json body;
            try { body = json::parse(req.body); } catch (...) {}
            std::string originalText = body.value("original", "");
            if (originalText.empty())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or empty 'original' field in request body\"}", "application/json");
                return;
            }

            PBC_HistoryResult result = PBC_DeleteMemory(ctx.charGuid, memId, originalText);
            if (result == PBC_HistoryResult::NotFound)
            {
                res.status = 404;
                res.set_content("{\"error\":\"Memory not found\"}", "application/json");
                return;
            }
            if (result == PBC_HistoryResult::Desync)
            {
                res.status = 409;
                res.set_content("{\"error\":\"desync\"}", "application/json");
                return;
            }

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] API memory delete: character GUID={} memory id={}", ctx.charGuid, memId);

            res.set_content("{\"status\":\"deleted\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // GET /api/char/:guid/data
        //
        // Returns character parameters as a JSON array.  Currently only
        // roll_modifier is supported.
        //
        // Response: {"data": [{"key": "roll_modifier", "value": 0}]}
        //
        // Requires auth.  Works even when the character is offline.
        // -------------------------------------------------------------------
        svr->Get("/api/char/:guid/data", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            int32_t rollMod = 0;
            {
                std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
                auto it = g_PBC_RollChanceModifiers.find(ctx.charGuid);
                if (it != g_PBC_RollChanceModifiers.end())
                    rollMod = it->second;
            }

            json data = json::array();
            data.push_back({{"key", "roll_modifier"}, {"value", rollMod}});

            json response;
            response["data"] = data;

            res.set_content(response.dump(), "application/json");
        });

        // -------------------------------------------------------------------
        // POST /api/char/:guid/data
        //
        // Updates character parameters.  Accepts the same JSON array format
        // as returned by GET /api/char/:guid/data.  Currently only
        // "roll_modifier" is supported (range -100 to 100).
        //
        // Request body: {"data": [{"key": "roll_modifier", "value": 5}]}
        //
        // Requires auth.  Works even when the character is offline.
        // -------------------------------------------------------------------
        svr->Post("/api/char/:guid/data", [](const httplib::Request& req, httplib::Response& res) {
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] HTTP: {} {} from {}", req.method, req.path, req.remote_addr);

            PBC_ApiContext ctx;
            if (!PBC_ValidateApiRequest(req, res, true, false, ctx))
                return;

            // Parse JSON body
            json body;
            try { body = json::parse(req.body); } catch (...) {}

            if (!body.contains("data") || !body["data"].is_array())
            {
                res.status = 400;
                res.set_content("{\"error\":\"Missing or invalid 'data' array in request body\"}", "application/json");
                return;
            }

            for (const auto& item : body["data"])
            {
                std::string key = item.value("key", "");
                if (key == "roll_modifier")
                {
                    int32_t value = 0;
                    try { value = item["value"].get<int32_t>(); } catch (...) {}
                    if (value < -100 || value > 100)
                    {
                        res.status = 400;
                        res.set_content("{\"error\":\"roll_modifier must be between -100 and 100\"}", "application/json");
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
                        if (value == 0)
                            g_PBC_RollChanceModifiers.erase(ctx.charGuid);
                        else
                            g_PBC_RollChanceModifiers[ctx.charGuid] = value;
                    }
                    DB_UpsertRollChanceModifier(ctx.charGuid, value);

                    if (g_PBC_DebugEnabled)
                        LOG_INFO("server.loading", "[PBC] API data update: character GUID={} roll_modifier={}", ctx.charGuid, value);
                }
                // Unknown keys are silently ignored for forward compatibility
            }

            res.set_content("{\"status\":\"updated\"}", "application/json");
        });

        // -------------------------------------------------------------------
        // WebSocket endpoint: /ws
        //
        // Authentication is handled by the pre_routing_handler above.
        // The token is re-validated here to extract the player GUID for
        // connection metadata.
        // -------------------------------------------------------------------
        svr->WebSocket("/ws",
            // WebSocket handler
            [](const httplib::Request& req, httplib::ws::WebSocket& ws) {
                // Re-extract the token to identify the player
                std::string token = ExtractWebSocketToken(req);
                uint64_t guid = ValidateToken(token);

                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] WS: new connection from {} (player GUID={})",
                             req.remote_addr, guid);

                ws.send(json({{"event", "connected"}}).dump());

                std::string msg;
                while (ws.read(msg) && !s_httpShuttingDown.load())
                {
                    // Parse "subscribe <GUID>" command
                    if (msg.compare(0, 10, "subscribe ") == 0)
                    {
                        std::string guidStr = msg.substr(10);
                        uint64_t subGuid = 0;
                        try { subGuid = std::stoull(guidStr); } catch (...) {}

                        if (subGuid != 0)
                        {
                            WsSubscribe(&ws, subGuid);
                            ws.send(json({{"event", "subscribed"}, {"guid", subGuid}}).dump());

                            if (g_PBC_DebugEnabled)
                                LOG_INFO("server.loading", "[PBC] WS: player GUID={} subscribed to character GUID={}",
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

                        if (g_PBC_DebugEnabled)
                            LOG_INFO("server.loading", "[PBC] WS: player GUID={} unsubscribed", guid);
                    }
                    else
                    {
                        ws.send(json({{"event", "error"}, {"message", "Unknown command. Use: subscribe <GUID> or unsubscribe"}}).dump());
                    }
                }

                // Clean up subscription on disconnect
                WsUnsubscribe(&ws);

                // Close the connection if we're shutting down
                if (s_httpShuttingDown.load())
                    ws.close(httplib::ws::CloseStatus::GoingAway, "server shutting down");

                if (g_PBC_DebugEnabled)
                    LOG_INFO("server.loading", "[PBC] WS: connection closed (player GUID={})", guid);
            },
            // Sub-protocol selector: return "access_token" to confirm the
            // selected subprotocol in the WebSocket handshake response.
            [](const std::vector<std::string>& protocols) -> std::string {
                // If the client offered "access_token" as a subprotocol, accept it
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
#endif // CPPHTTPLIB_OPENSSL_SUPPORT
}

void PBC_HttpServerStop()
{
    s_httpShuttingDown.store(true);

    if (s_httpServer)
    {
        s_httpServer->stop();
    }

    if (s_httpThread && s_httpThread->joinable())
    {
        // The server thread may be blocked in a WebSocket handler's
        // ws.read() call.  Server::stop() closes the listening socket
        // but does NOT close active client connections, so the thread
        // may not finish until the WS read timeout expires (default
        // 300 s).  Detach the thread to avoid hanging the shutdown
        // process — the OS will clean it up when the process exits.
        s_httpThread->detach();
    }

    s_httpThread.reset();
    // Do not reset s_httpServer — the detached thread may still
    // reference it.  It will be cleaned up when the process exits.
    s_httpRunning.store(false);
}

bool PBC_HttpServerIsRunning()
{
    return s_httpRunning.load();
}
