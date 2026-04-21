#include "pbc_utils.h"

#include <chrono>
#include <thread>

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
// Mention counting
// ---------------------------------------------------------------------------

uint32_t PBC_CountMentions(const std::deque<std::string>& history, const std::string& name)
{
    uint32_t total = 0;
    for (const auto& line : history)
    {
        size_t pos = 0;
        while ((pos = line.find(name, pos)) != std::string::npos)
        {
            ++total;
            pos += name.size();
        }
    }
    return total;
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
