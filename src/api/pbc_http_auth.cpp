#include "pbc_http.h"          // PBC_HttpServerGenerateOTP declaration
#include "pbc_http_auth.h"     // PBC_ExchangeOTP, PBC_ValidateToken, PBC_GetAccountName
#include "pbc_config.h"        // g_PBC_HttpServerPrivateKey
#include "pbc_log.h"           // PBC_Log
#include "pbc_utils.h"         // PBC_GetRNG

#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#endif

#include "AccountMgr.h"
#include "DatabaseEnv.h"

// ===========================================================================
// Constants
// ===========================================================================

// Token lifetime: 1 year (hardcoded)
static const int64_t TOKEN_LIFETIME_SEC = 365 * 24 * 3600;

// OTP validity: 2 minutes
static const int64_t OTP_LIFETIME_SEC = 120;

// ===========================================================================
// Base64 encode / decode helpers
// ===========================================================================

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
            return false;
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

// ===========================================================================
// Base64URL helpers
// ===========================================================================

static std::string Base64ToBase64Url(std::string b64)
{
    for (char& c : b64)
    {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
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
    switch (b64url.size() % 4)
    {
        case 2: b64url += "=="; break;
        case 3: b64url += "="; break;
        case 0: break;
        case 1: return ""; // invalid
    }
    return b64url;
}

// ===========================================================================
// AES-256-CBC token encryption / decryption (requires OpenSSL)
// ===========================================================================

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT

static std::vector<unsigned char> DeriveAESKey(const std::string& privateKey)
{
    std::vector<unsigned char> key(32);
    SHA256(reinterpret_cast<const unsigned char*>(privateKey.data()),
           privateKey.size(), key.data());
    return key;
}

// Create an encrypted token for an account ID.
// Token payload: [accountId:4 bytes][reserved:4 bytes][timestamp:8 bytes] = 16 bytes
// Returns "" if OpenSSL is not available or encryption fails.
static std::string CreateToken(uint32_t accountId)
{
    auto key = DeriveAESKey(g_PBC_HttpServerPrivateKey);

    unsigned char plaintext[16];
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::memcpy(plaintext, &accountId, 4);
    std::memset(plaintext + 4, 0, 4);  // reserved
    std::memcpy(plaintext + 8, &now, 8);

    unsigned char iv[16];
    if (RAND_bytes(iv, 16) != 1)
    {
        PBC_Log(PBC_LogLevel::ERROR, "CreateToken: RAND_bytes failed");
        return "";
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        PBC_Log(PBC_LogLevel::ERROR, "CreateToken: EVP_CIPHER_CTX_new failed");
        return "";
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        PBC_Log(PBC_LogLevel::ERROR, "CreateToken: EVP_EncryptInit_ex failed");
        return "";
    }

    unsigned char ciphertext[32];
    int outLen1 = 0;
    if (EVP_EncryptUpdate(ctx, ciphertext, &outLen1, plaintext, 16) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        PBC_Log(PBC_LogLevel::ERROR, "CreateToken: EVP_EncryptUpdate failed");
        return "";
    }

    int outLen2 = 0;
    if (EVP_EncryptFinal_ex(ctx, ciphertext + outLen1, &outLen2) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        PBC_Log(PBC_LogLevel::ERROR, "CreateToken: EVP_EncryptFinal_ex failed");
        return "";
    }

    EVP_CIPHER_CTX_free(ctx);

    int totalLen = outLen1 + outLen2;
    std::vector<unsigned char> tokenData(iv, iv + 16);
    tokenData.insert(tokenData.end(), ciphertext, ciphertext + totalLen);

    return Base64ToBase64Url(Base64Encode(tokenData.data(), tokenData.size()));
}

// Validate a token and extract the account ID.
// Returns 0 on failure (invalid token, expired, decryption error).
static uint32_t ValidateTokenImpl(const std::string& token)
{
    if (token.empty())
        return 0;

    auto key = DeriveAESKey(g_PBC_HttpServerPrivateKey);

    std::string b64 = Base64UrlToBase64(token);
    if (b64.empty())
        return 0;
    std::vector<unsigned char> tokenData;
    if (!Base64Decode(b64, tokenData))
        return 0;

    if (tokenData.size() < 32)
        return 0;

    const unsigned char* iv = tokenData.data();
    const unsigned char* ciphertext = tokenData.data() + 16;
    int cipherLen = static_cast<int>(tokenData.size()) - 16;

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
        return 0;

    int totalLen = outLen1 + outLen2;
    if (totalLen < 16)
        return 0;

    uint32_t accountId;
    uint64_t timestamp;
    std::memcpy(&accountId, plaintext, 4);
    std::memcpy(&timestamp, plaintext + 8, 8);

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (static_cast<int64_t>(timestamp) + TOKEN_LIFETIME_SEC < now)
        return 0;

    return accountId;
}

#else // !CPPHTTPLIB_OPENSSL_SUPPORT

static std::string CreateToken(uint32_t /*accountId*/)
{
    PBC_Log(PBC_LogLevel::ERROR, "CreateToken: OpenSSL not available — token creation impossible");
    return "";
}

static uint32_t ValidateTokenImpl(const std::string& /*token*/)
{
    return 0;
}

#endif // CPPHTTPLIB_OPENSSL_SUPPORT

// ===========================================================================
// OTP storage and helpers
// ===========================================================================

struct OTPEntry
{
    uint32_t accountId;
    time_t   expiresAt;
};

static std::mutex s_otpMutex;
static std::unordered_map<std::string, OTPEntry> s_otpStore;

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

// ===========================================================================
// Public functions
// ===========================================================================

std::string PBC_HttpServerGenerateOTP(uint32_t accountId)
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
        PBC_Log(PBC_LogLevel::ERROR, "Failed to generate unique OTP after 10 attempts");
        return "";
    }

    time_t expiresAt = std::time(nullptr) + OTP_LIFETIME_SEC;
    s_otpStore[otp] = {accountId, expiresAt};
    return otp;
}

std::string PBC_ExchangeOTP(const std::string& otp)
{
    if (otp.empty())
        return "";

    // Validate and consume the OTP
    uint32_t accountId;
    {
        std::lock_guard<std::mutex> lock(s_otpMutex);
        auto it = s_otpStore.find(otp);
        if (it == s_otpStore.end())
            return "";

        accountId = it->second.accountId;
        time_t expiresAt = it->second.expiresAt;

        // Remove the OTP (one-time use)
        s_otpStore.erase(it);

        // Check expiry
        if (std::time(nullptr) > expiresAt)
            return "";
    }

    // Create the bearer token
    std::string token = CreateToken(accountId);
    return token;
}

uint32_t PBC_ValidateToken(const std::string& token)
{
    return ValidateTokenImpl(token);
}

std::string PBC_GetAccountName(uint32_t accountId)
{
    std::string name;
    if (AccountMgr::GetName(accountId, name))
        return name;
    return "";
}

void PBC_CleanupExpiredTokens()
{
    // Token expiry is checked on every validation via the embedded timestamp,
    // so there is no separate expiry map to clean.  This function is a no-op
    // placeholder for future use if an in-memory token blacklist is added.
}
