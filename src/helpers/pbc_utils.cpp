#include "pbc_utils.h"
#include "pbc_log.h"
#include "pbc_locales.h"
#include "Player.h"
#include "Group.h"
#include "WorldSession.h"
#include "SharedDefines.h"
#include "World.h"
#include "ObjectMgr.h"
#include "CreatureData.h"
#include "GameObjectData.h"
#include "ItemTemplate.h"
#include "QuestDef.h"

#include <fmt/core.h>
#include <chrono>
#include <thread>
#include <set>
#include <regex>
#include <algorithm>
#include <ctime>
#include <array>
#include <cstring>

// ---------------------------------------------------------------------------
// SHA-256 (self-contained, no OpenSSL dependency)
//
// Standard FIPS 180-4 implementation.  Used only as a stable content
// change-signal for disk card imports — not security-sensitive, but a real
// SHA-256 so the stored CHAR(64) hash matches external tooling expectations.
// ---------------------------------------------------------------------------
namespace {

inline uint32_t Sha256Ror(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void Sha256ProcessBlock(const uint8_t* block, uint32_t state[8])
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) |
               (uint32_t(block[i*4+2]) << 8) | uint32_t(block[i*4+3]);
    for (int i = 16; i < 64; ++i)
    {
        uint32_t s0 = Sha256Ror(w[i-15],7) ^ Sha256Ror(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = Sha256Ror(w[i-2],17) ^ Sha256Ror(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i)
    {
        uint32_t S1 = Sha256Ror(e,6) ^ Sha256Ror(e,11) ^ Sha256Ror(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + k[i] + w[i];
        uint32_t S0 = Sha256Ror(a,2) ^ Sha256Ror(a,13) ^ Sha256Ror(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

} // namespace

std::string PBC_Sha256Hex(const std::string& data)
{
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };

    const uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8;

    // Build padded message: data + 0x80 + zeros + 64-bit big-endian length.
    std::vector<uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0x00);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));

    for (size_t off = 0; off < msg.size(); off += 64)
        Sha256ProcessBlock(msg.data() + off, state);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i)
        for (int b = 3; b >= 0; --b)
        {
            uint8_t byte = static_cast<uint8_t>((state[i] >> (b * 8)) & 0xFF);
            out.push_back(hex[byte >> 4]);
            out.push_back(hex[byte & 0x0F]);
        }
    return out;
}

// ---------------------------------------------------------------------------
// Debug output helpers
// ---------------------------------------------------------------------------

std::string PBC_TruncateForDebug(const std::string& s, size_t maxLen, size_t headLen, size_t tailLen)
{
    if (s.size() <= maxLen)
        return s;
    return s.substr(0, headLen) + " ... " + s.substr(s.size() - tailLen);
}

std::string PBC_SanitizeForFmt(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '{')      out += '(';
        else if (c == '}') out += ')';
        else               out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Template substitution helpers
// ---------------------------------------------------------------------------

void PBC_NormalizeNewlines(std::string& s)
{
    // Convert \r\n → \n first (must be done before standalone \r → \n)
    size_t pos = 0;
    while ((pos = s.find("\r\n", pos)) != std::string::npos)
    {
        s.erase(pos, 1); // remove \r, keep \n
        // pos unchanged — the next char is now the \n we kept
    }

    // Convert any remaining standalone \r → \n
    pos = 0;
    while ((pos = s.find('\r', pos)) != std::string::npos)
    {
        s[pos] = '\n';
        ++pos;
    }
}

void PBC_ExpandNewlineEscapes(std::string& s)
{
    size_t pos = 0;
    while ((pos = s.find("\\n", pos)) != std::string::npos)
    {
        s.replace(pos, 2, "\n");
        pos += 1;
    }
}

void PBC_ReplaceToken(std::string& s, const std::string& key, const std::string& value)
{
    std::string token = "{" + key + "}";
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::string::npos)
    {
        // When the value is empty and the token sits alone on its own line
        // (at start of string or after a newline, followed by a newline),
        // remove the trailing newline together with the token to avoid
        // leaving an empty line behind.
        if (value.empty() && (pos == 0 || s[pos - 1] == '\n'))
        {
            size_t after = pos + token.size();
            if (after < s.size() && s[after] == '\n')
            {
                s.erase(pos, token.size() + 1);
                continue;
            }
        }
        s.replace(pos, token.size(), value);
        pos += value.size();
    }
}

void PBC_CleanUnknownTokens(std::string& s)
{
    // Match {word} patterns — any remaining tokens that were not replaced.
    static const std::regex tokenPattern(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
    std::set<std::string> warned;
    std::smatch match;
    std::string result;
    size_t lastPos = 0;
    size_t searchPos = 0;

    while (std::regex_search(s.cbegin() + searchPos, s.cend(), match, tokenPattern))
    {
        size_t matchPos = searchPos + match.position();
        std::string tokenName = match[1].str();

        // Append text before the match
        result.append(s, lastPos, matchPos - lastPos);

        // Log warning once per unique unknown token
        if (warned.insert(tokenName).second)
            PBC_Log(PBC_LogLevel::PBC_WARNING, "Unknown template token '{{{}}}' — replaced with empty string", tokenName);

        // Skip the token (replace with "")
        size_t tokenEnd = matchPos + match.length();

        // If this token sits alone on its own line (at start of string or after
        // a newline, followed by a newline), remove the trailing newline too
        // to avoid leaving an empty line behind.
        if ((matchPos == 0 || s[matchPos - 1] == '\n') && tokenEnd < s.size() && s[tokenEnd] == '\n')
            lastPos = tokenEnd + 1;
        else
            lastPos = tokenEnd;

        searchPos = lastPos;
    }

    if (lastPos > 0)
    {
        // Append remaining text after the last match
        result.append(s, lastPos);
        s = std::move(result);
    }
    // If lastPos == 0, no unknown tokens were found — leave s unchanged.
}

void PBC_StripEmptyAnnotatedLines(std::string& s)
{
    // Annotated lines look like "[token_name]content\n".
    // If content after stripping the annotation marker is empty/whitespace,
    // the entire line (including its trailing newline) is removed.
    static const std::regex annotPattern(R"(\[[a-zA-Z_][a-zA-Z0-9_]*\])");

    std::string result;
    result.reserve(s.size());

    size_t lineStart = 0;
    while (lineStart < s.size())
    {
        size_t lineEnd = s.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = s.size();

        std::string line(s, lineStart, lineEnd - lineStart);
        std::string stripped = std::regex_replace(line, annotPattern, "");

        // Keep the line if it has visible content after stripping annotations
        bool hasContent = false;
        for (char c : stripped)
        {
            if (c != ' ' && c != '\t' && c != '\r')
            {
                hasContent = true;
                break;
            }
        }

        if (hasContent)
        {
            if (!result.empty()) result += '\n';
            result += line;
        }

        lineStart = lineEnd + 1; // skip the newline
    }

    s = std::move(result);
}

// ---------------------------------------------------------------------------
// String formatting helpers
// ---------------------------------------------------------------------------

std::string PBC_NaturalList(const std::vector<std::string>& items)
{
    if (items.empty()) return "";

    std::string s = PBC_Localize("You see ");
    if (items.size() == 1)
    {
        s += items[0];
    }
    else
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0 && i == items.size() - 1)
                s += PBC_Localize(" and ");
            else if (i > 0)
                s += ", ";
            s += items[i];
        }
    }
    s += PBC_Localize(" nearby.");
    return s;
}

std::string PBC_DefaultRelationshipText(const std::string& name)
{
    return PBC_Localize("You don't know much about {0}.", name);
}


// ---------------------------------------------------------------------------
// Chat / message helpers
// ---------------------------------------------------------------------------

std::string PBC_SanitizeChatMessage(const std::string& msg)
{
    static const std::regex linkPattern(
        R"((?:\|c[0-9a-fA-F]{8})?\|H[^|]+\|h(\[[^\]]*\])\|h(?:\|r)?)",
        std::regex::optimize);

    std::string result;
    result.reserve(msg.size());

    auto it  = msg.cbegin();
    auto end = msg.cend();
    std::smatch m;

    while (std::regex_search(it, end, m, linkPattern))
    {
        result.append(it, m.prefix().second);
        result += m[1].str();
        it = m.suffix().first;
    }
    result.append(it, end);
    return result;
}

bool PBC_MentionsCharacter(const std::string& msg, const std::string& charName)
{
    std::string lower = msg, lname = charName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    return lower.find(lname) != std::string::npos;
}

// ---------------------------------------------------------------------------
// RNG
// ---------------------------------------------------------------------------

std::mt19937& PBC_GetRNG()
{
    thread_local std::mt19937 rng([]{
        // Use a dummy variable to get a stack address before constructing the seed.
        int dummy;
        std::seed_seq seq({
            std::random_device{}(),
            static_cast<uint32_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()),
            static_cast<uint32_t>(
                std::hash<std::thread::id>{}(std::this_thread::get_id())),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&dummy))
        });
        std::mt19937 engine(seq);
        engine.discard(1024);          // warm up the generator
        return engine;
    }());
    return rng;
}

bool PBC_RollChance(uint32_t chance)
{
    if (chance == 0) return false;
    std::uniform_int_distribution<uint32_t> dist(0, 99);
    return dist(PBC_GetRNG()) < chance;
}

// ---------------------------------------------------------------------------
// Locale helpers
// ---------------------------------------------------------------------------

uint8_t PBC_GetDbcLocale()
{
    return static_cast<uint8_t>(sWorld->GetDefaultDbcLocale());
}

std::string_view PBC_DbcString(char const* const* localeStrArray)
{
    if (!localeStrArray)
        return {};

    uint8_t locale = PBC_GetDbcLocale();

    // Try configured locale first
    if (locale < TOTAL_LOCALES && localeStrArray[locale] && localeStrArray[locale][0] != '\0')
        return std::string_view(localeStrArray[locale]);

    // Fall back to enUS (index 0)
    if (localeStrArray[0] && localeStrArray[0][0] != '\0')
        return std::string_view(localeStrArray[0]);

    return {};
}

std::string PBC_GetCreatureName(uint32_t entry)
{
    // Try locale override first
    if (CreatureLocale const* cl = sObjectMgr->GetCreatureLocale(entry))
    {
        uint8_t locale = PBC_GetDbcLocale();
        std::string_view locName = ObjectMgr::GetLocaleString(cl->Name, locale);
        if (!locName.empty())
            return std::string(locName);
    }

    // Fall back to enUS template name
    if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry))
        return ct->Name;

    return {};
}

std::string PBC_GetGameObjectName(uint32_t entry)
{
    // Try locale override first
    if (GameObjectLocale const* gl = sObjectMgr->GetGameObjectLocale(entry))
    {
        uint8_t locale = PBC_GetDbcLocale();
        std::string_view locName = ObjectMgr::GetLocaleString(gl->Name, locale);
        if (!locName.empty())
            return std::string(locName);
    }

    // Fall back to enUS template name
    if (GameObjectTemplate const* gt = sObjectMgr->GetGameObjectTemplate(entry))
        return gt->name;

    return {};
}

std::string PBC_GetItemName(uint32_t entry)
{
    // Try locale override first
    if (ItemLocale const* il = sObjectMgr->GetItemLocale(entry))
    {
        uint8_t locale = PBC_GetDbcLocale();
        std::string_view locName = ObjectMgr::GetLocaleString(il->Name, locale);
        if (!locName.empty())
            return std::string(locName);
    }

    // Fall back to enUS template name
    if (ItemTemplate const* tmpl = sObjectMgr->GetItemTemplate(entry))
        return tmpl->Name1;

    return {};
}

static std::string PBC_GetQuestLocaleString(uint32_t questId,
    std::vector<std::string> QuestLocale::* field,
    std::string const& (Quest::*fallback)() const = nullptr)
{
    // Try locale override first
    if (QuestLocale const* ql = sObjectMgr->GetQuestLocale(questId))
    {
        uint8_t locale = PBC_GetDbcLocale();
        std::string_view locText = ObjectMgr::GetLocaleString(ql->*field, locale);
        if (!locText.empty())
            return std::string(locText);
    }

    // Fall back to enUS quest text
    if (fallback)
    {
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            return (quest->*fallback)();
    }

    return {};
}

std::string PBC_GetQuestTitle(uint32_t questId)
{
    return PBC_GetQuestLocaleString(questId, &QuestLocale::Title, &Quest::GetTitle);
}

std::string PBC_GetQuestDetails(uint32_t questId)
{
    return PBC_GetQuestLocaleString(questId, &QuestLocale::Details, &Quest::GetDetails);
}

std::string PBC_GetQuestObjectives(uint32_t questId)
{
    return PBC_GetQuestLocaleString(questId, &QuestLocale::Objectives, &Quest::GetObjectives);
}

std::string PBC_GetQuestOfferRewardText(uint32_t questId)
{
    return PBC_GetQuestLocaleString(questId, &QuestLocale::OfferRewardText, &Quest::GetOfferRewardText);
}

std::string PBC_GetQuestCompletedText(uint32_t questId)
{
    return PBC_GetQuestLocaleString(questId, &QuestLocale::CompletedText, &Quest::GetCompletedText);
}

// ---------------------------------------------------------------------------
// Enum-to-string helpers
// ---------------------------------------------------------------------------

std::string PBC_ClassStr(uint8_t cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:      return PBC_Localize("Warrior");
        case CLASS_PALADIN:      return PBC_Localize("Paladin");
        case CLASS_HUNTER:       return PBC_Localize("Hunter");
        case CLASS_ROGUE:        return PBC_Localize("Rogue");
        case CLASS_PRIEST:       return PBC_Localize("Priest");
        case CLASS_DEATH_KNIGHT: return PBC_Localize("Death Knight");
        case CLASS_SHAMAN:       return PBC_Localize("Shaman");
        case CLASS_MAGE:         return PBC_Localize("Mage");
        case CLASS_WARLOCK:      return PBC_Localize("Warlock");
        case CLASS_DRUID:        return PBC_Localize("Druid");
        default:                 return PBC_Localize("Adventurer");
    }
}

std::string PBC_RaceStr(uint8_t race)
{
    switch (race)
    {
        case RACE_HUMAN:          return PBC_Localize("Human");
        case RACE_ORC:            return PBC_Localize("Orc");
        case RACE_DWARF:          return PBC_Localize("Dwarf");
        case RACE_NIGHTELF:       return PBC_Localize("Night Elf");
        case RACE_UNDEAD_PLAYER:  return PBC_Localize("Forsaken");
        case RACE_TAUREN:         return PBC_Localize("Tauren");
        case RACE_GNOME:          return PBC_Localize("Gnome");
        case RACE_TROLL:          return PBC_Localize("Troll");
        case RACE_BLOODELF:       return PBC_Localize("Blood Elf");
        case RACE_DRAENEI:        return PBC_Localize("Draenei");
        default:                  return PBC_Localize("Unknown");
    }
}

std::string PBC_GenderStr(uint8_t gender)
{
    return gender == GENDER_FEMALE ? PBC_Localize("female") : PBC_Localize("male");
}

// ---------------------------------------------------------------------------
// Time formatting helpers
// ---------------------------------------------------------------------------

std::string PBC_FormatDateTime(time_t t)
{
    if (t <= 0)
        return "";
    struct tm* lt = localtime(&t);
    if (!lt)
        return "";
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt);
    return buf;
}

std::string PBC_FormatDate(time_t t)
{
    if (t <= 0)
        return "";
    struct tm* lt = localtime(&t);
    if (!lt)
        return "";
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", lt);
    return buf;
}
