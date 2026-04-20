#include "pbc_llm.h"
#include "pbc_config.h"
#include "pbc_http.h"
#include "Log.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Token estimator (rough: 1 token ≈ 4 characters)
// ---------------------------------------------------------------------------
int PBC_EstimateTokens(const std::string& text)
{
    return static_cast<int>(text.size()) / 4 + 1;
}

// ---------------------------------------------------------------------------
// Internal trim
// ---------------------------------------------------------------------------
static std::string Trim(std::string s)
{
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// ---------------------------------------------------------------------------
// PBC_CallLLM  – synchronous, OpenAI-compatible /chat/completions
// Safe to call from any thread; does not touch game objects.
// ---------------------------------------------------------------------------
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           int maxTokensOverride)
{
    PBC_LLMResult result{ false, "", 0 };

    std::string url = g_PBC_BaseUrl;
    if (!url.empty() && url.back() == '/')
        url.pop_back();
    url += "/chat/completions";

    json body;
    body["model"]             = g_PBC_Model;
    body["temperature"]       = g_PBC_Temperature;
    // maxTokensOverride == -1  → omit max_tokens (condensation / relationship calls)
    // maxTokensOverride ==  0  → use config value
    // maxTokensOverride  >  0  → use the override value
    if (maxTokensOverride == -1)
    {
        // intentionally omitted — let the model decide when to stop
    }
    else if (maxTokensOverride > 0)
        body["max_tokens"] = maxTokensOverride;
    else if (g_PBC_MaxResponseTokens > 0)
        body["max_tokens"] = g_PBC_MaxResponseTokens;

    json messages = json::array();
    if (!systemPrompt.empty())
        messages.push_back({ {"role", "system"}, {"content", systemPrompt} });
    messages.push_back({ {"role", "user"}, {"content", userPrompt} });
    body["messages"] = messages;

    std::string bodyStr = body.dump();

    // Append provider-specific extra parameters from config.
    // PBC.ModelExtraParameters is a raw JSON fragment (key:value pairs) that is
    // spliced into the request body before the closing '}'.  Single quotes in
    // the config value are automatically replaced with double quotes, so the
    // user does not need to escape them.
    // Example (DeepSeek): 'frequency_penalty':0.5,'presence_penalty':0.2
    // Example (GLM):       'frequency_penalty':0.5,'thinking':{'type':'disabled'}
    if (!g_PBC_ModelExtraParameters.empty())
    {
        std::string extra = g_PBC_ModelExtraParameters;
        std::replace(extra.begin(), extra.end(), '\'', '"');
        bodyStr.pop_back(); // remove trailing '}'
        bodyStr += "," + extra + "}";
    }

    if (g_PBC_DebugEnabled && g_PBC_DebugShowFullRequest)
    {
        LOG_INFO("server.loading", "[PBC] LLM request body:\n{}", bodyStr);
    }

    constexpr int MAX_ATTEMPTS = 2;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        if (attempt > 1)
            LOG_INFO("server.loading", "[PBC] LLM: retrying request (attempt {}/{})...", attempt, MAX_ATTEMPTS);

        PBC_HttpClient http;
        http.SetTimeoutSeconds(g_PBC_RequestTimeoutSec);
        std::string responseBody = http.Post(url, bodyStr, g_PBC_ApiKey);

        if (responseBody.empty())
        {
            LOG_ERROR("server.loading", "[PBC] LLM: empty response from API (attempt {}/{}).", attempt, MAX_ATTEMPTS);
            continue;
        }

        if (g_PBC_DebugEnabled && g_PBC_DebugShowFullRequest)
        {
            LOG_INFO("server.loading", "[PBC] LLM response body:\n{}", responseBody);
        }

        try
        {
            json resp = json::parse(responseBody);

            if (resp.contains("error"))
            {
                LOG_ERROR("server.loading", "[PBC] LLM API error (attempt {}/{}): {}",
                          attempt, MAX_ATTEMPTS, resp["error"].value("message", responseBody));
                continue;
            }

            std::string text = resp["choices"][0]["message"]["content"].get<std::string>();
            text = Trim(text);
            // Strip surrounding quotes added by some models
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                text = text.substr(1, text.size() - 2);
            // Replace newlines with spaces so the reply fits on a single chat line
            for (char& c : text)
                if (c == '\n' || c == '\r') c = ' ';

            int tokensUsed = 0;
            if (resp.contains("usage") && resp["usage"].contains("total_tokens"))
                tokensUsed = resp["usage"]["total_tokens"].get<int>();

            result.success    = true;
            result.text       = text;
            result.tokensUsed = tokensUsed;

            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] LLM reply ({} tokens): {}", tokensUsed, text);

            return result;
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR("server.loading", "[PBC] JSON parse error (attempt {}/{}): {}", attempt, MAX_ATTEMPTS, ex.what());
            if (g_PBC_DebugEnabled)
                LOG_INFO("server.loading", "[PBC] Raw response: {}", responseBody);
        }
    }

    return result;
}
