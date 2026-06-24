#include "pbc_memory.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_utils.h"
#include "pbc_log.h"

#include <cctype>
#include <mutex>

std::string PBC_MoodFromCategory(const std::string& category)
{
    if (category == PBC_Cat::Item)           return "pleased";
    if (category == PBC_Cat::Duel)           return "competitive";
    if (category == PBC_Cat::LevelUp)        return "proud";
    if (category == PBC_Cat::Combat)         return "tense";
    if (category == PBC_Cat::QuestTaken)     return "eager";
    if (category == PBC_Cat::QuestCompleted) return "satisfied";
    // chat, location, general → neutral (no stamped mood)
    return "";
}

void PBC_ProcessMoodRefine(PBC_EventItem& ev)
{
    if (ev.moodHistoryId == 0)
        return;
    if (g_PBC_MoodSystemPrompt.empty() || g_PBC_MoodUserPrompt.empty())
        return;  // prompts not provided → the model-free mood stands

    std::string usr = g_PBC_MoodUserPrompt;
    PBC_ExpandNewlineEscapes(usr);
    PBC_ReplaceToken(usr, "event", ev.moodEventText);
    PBC_CleanUnknownTokens(usr);

    // A one-word reply through the (fast, cheap) utility connection, with the
    // output capped so a rambling model can't burn tokens.
    const PBC_APIConfig* base = PBC_GetConnection("utility");
    if (!base)
        return;
    PBC_APIConfig cfg = *base;
    if (!cfg.requestParameters.is_object())
        cfg.requestParameters = pbc_json::object();
    cfg.requestParameters["max_tokens"] = 16;
    if (cfg.requestParameters.contains("options") && cfg.requestParameters["options"].is_object())
        cfg.requestParameters["options"]["num_predict"] = 16;  // Ollama equivalent

    PBC_LLMResult res = PBC_CallLLMWithConfig(cfg, g_PBC_MoodSystemPrompt, usr);
    if (!res.success || res.text.empty())
        return;

    // Extract a single lowercase mood word (letters only).
    std::string mood;
    for (char c : res.text)
    {
        if (std::isalpha(static_cast<unsigned char>(c)))
            mood.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!mood.empty())
            break;  // stop at the first non-letter once the word has started
    }
    if (mood.empty())
        return;
    if (mood.size() > 32)
        mood.resize(32);

    DB_UpdateHistoryMood(ev.moodHistoryId, mood);
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto it = g_PBC_History.find(ev.moodHistoryId);
        if (it != g_PBC_History.end())
            it->second.mood = mood;
    }
    PBC_Log(PBC_LogLevel::PBC_DEBUG, "MoodRefine: history id={} mood='{}'", ev.moodHistoryId, mood);
}
