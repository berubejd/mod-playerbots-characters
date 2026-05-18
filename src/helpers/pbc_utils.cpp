#include "pbc_utils.h"
#include "pbc_log.h"
#include "Player.h"
#include "Group.h"
#include "WorldSession.h"
#include "SharedDefines.h"

#include <chrono>
#include <thread>
#include <set>
#include <regex>
#include <algorithm>
#include <ctime>

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
            PBC_Log(PBC_LogLevel::WARNING, "Unknown template token '{{{}}}' — replaced with empty string", tokenName);

        // Skip the token (replace with "")
        lastPos = matchPos + match.length();
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

// ---------------------------------------------------------------------------
// String formatting helpers
// ---------------------------------------------------------------------------

std::string PBC_NaturalList(const std::vector<std::string>& items)
{
    if (items.empty()) return "";

    std::string s = "You see ";
    if (items.size() == 1)
    {
        s += items[0];
    }
    else
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0 && i == items.size() - 1)
                s += " and ";
            else if (i > 0)
                s += ", ";
            s += items[i];
        }
    }
    s += " nearby.";
    return s;
}

std::string PBC_DefaultRelationshipText(const std::string& name)
{
    return "You don't know much about " + name + ".";
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
// Group helpers
// ---------------------------------------------------------------------------

bool PBC_BotIsGroupedWithRealPlayer(Player* bot)
{
    if (!PBC_PTR_VALID(bot)) return false;
    Group* grp = bot->GetGroup();
    if (!grp) return false;
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!PBC_PTR_VALID(member) || !member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_PTR_VALID(sess)) continue;
        if (!sess->IsBot()) return true;
    }
    return false;
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
// Enum-to-string helpers
// ---------------------------------------------------------------------------

std::string PBC_ClassStr(uint8_t cls)
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
        default:                 return "Adventurer";
    }
}

std::string PBC_RaceStr(uint8_t race)
{
    switch (race)
    {
        case RACE_HUMAN:          return "Human";
        case RACE_ORC:            return "Orc";
        case RACE_DWARF:          return "Dwarf";
        case RACE_NIGHTELF:       return "Night Elf";
        case RACE_UNDEAD_PLAYER:  return "Forsaken";
        case RACE_TAUREN:         return "Tauren";
        case RACE_GNOME:          return "Gnome";
        case RACE_TROLL:          return "Troll";
        case RACE_BLOODELF:       return "Blood Elf";
        case RACE_DRAENEI:        return "Draenei";
        default:                  return "Unknown";
    }
}

std::string PBC_GenderStr(uint8_t gender)
{
    return gender == GENDER_FEMALE ? "female" : "male";
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
