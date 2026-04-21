#include "pbc_llm.h"
#include "pbc_config.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "Log.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

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
// Case-insensitive string comparison
// ---------------------------------------------------------------------------
static bool IEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// PBC_CallLLM  – synchronous LLM call
//   openai:     POST {baseUrl}/chat/completions  (OpenAI-compatible)
//   anthropic:  POST {baseUrl}/messages          (Anthropic Messages API)
// Safe to call from any thread; does not touch game objects.
// ---------------------------------------------------------------------------
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           int maxTokensOverride)
{
    PBC_LLMResult result{ false, "", 0 };

    const bool isAnthropic = IEquals(g_PBC_APIType, "anthropic");

    // --- Build URL --------------------------------------------------------
    std::string url = g_PBC_BaseUrl;
    if (!url.empty() && url.back() == '/')
        url.pop_back();
    url += isAnthropic ? "/messages" : "/chat/completions";

    // --- Build request body -----------------------------------------------
    json body;
    body["model"] = g_PBC_Model;

    if (isAnthropic)
    {
        // Anthropic Messages API format
        // system is a top-level field, not part of the messages array
        if (!systemPrompt.empty())
            body["system"] = systemPrompt;

        json messages = json::array();
        messages.push_back({ {"role", "user"}, {"content", userPrompt} });
        body["messages"] = messages;

        // max_tokens is required for Anthropic — unlike OpenAI, it cannot be omitted.
        // maxTokensOverride == -1 (condensation/relationship): use a generous limit
        //   so the model isn't truncated mid-output.
        // maxTokensOverride ==  0: use config value (PBC.MaxResponseLength).
        // maxTokensOverride  >  0: use the explicit override.
        if (maxTokensOverride == -1)
            body["max_tokens"] = 4096;
        else if (maxTokensOverride > 0)
            body["max_tokens"] = maxTokensOverride;
        else if (g_PBC_MaxResponseTokens > 0)
            body["max_tokens"] = g_PBC_MaxResponseTokens;
        else
            body["max_tokens"] = 4096;

        if (g_PBC_Temperature >= 0.0)
            body["temperature"] = g_PBC_Temperature;
    }
    else
    {
        // OpenAI-compatible /chat/completions format
        body["temperature"] = g_PBC_Temperature;

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
    }

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
        LOG_INFO("server.loading", "[PBC] LLM request body:\n{}", PBC_TruncateForDebug(bodyStr));
    }

    // --- Build headers ----------------------------------------------------
    std::vector<std::pair<std::string, std::string>> headers;

    if (isAnthropic)
    {
        // Anthropic uses x-api-key header and requires anthropic-version
        if (!g_PBC_ApiKey.empty())
            headers.emplace_back("x-api-key", g_PBC_ApiKey);
        headers.emplace_back("anthropic-version", "2023-06-01");
    }
    else
    {
        // OpenAI-compatible uses Authorization: Bearer
        if (!g_PBC_ApiKey.empty())
            headers.emplace_back("Authorization", "Bearer " + g_PBC_ApiKey);
    }

    // --- Execute request --------------------------------------------------
    constexpr int MAX_ATTEMPTS = 2;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        if (attempt > 1)
        {
            LOG_INFO("server.loading", "[PBC] LLM: waiting 3s before retry (attempt {}/{})...", attempt, MAX_ATTEMPTS);
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        PBC_HttpClient http;
        http.SetTimeoutSeconds(g_PBC_RequestTimeoutSec);
        std::string responseBody = http.Post(url, bodyStr, headers);

        if (responseBody.empty())
        {
            LOG_ERROR("server.loading", "[PBC] LLM: empty response from API (attempt {}/{}).", attempt, MAX_ATTEMPTS);
            continue;
        }

        if (g_PBC_DebugEnabled && g_PBC_DebugShowFullRequest)
        {
            LOG_INFO("server.loading", "[PBC] LLM response body:\n{}", PBC_TruncateForDebug(responseBody));
        }

        try
        {
            json resp = json::parse(responseBody);

            if (resp.contains("error"))
            {
                std::string errMsg;
                if (resp["error"].is_object() && resp["error"].contains("message"))
                    errMsg = resp["error"]["message"].get<std::string>();
                else
                    errMsg = responseBody;
                LOG_ERROR("server.loading", "[PBC] LLM API error (attempt {}/{}): {}",
                          attempt, MAX_ATTEMPTS, errMsg);
                continue;
            }

            std::string text;
            int tokensUsed = 0;

            if (isAnthropic)
            {
                // Anthropic response: content[0].text
                if (!resp.contains("content") || !resp["content"].is_array() || resp["content"].empty())
                {
                    LOG_ERROR("server.loading", "[PBC] LLM: unexpected Anthropic response format (attempt {}/{}).", attempt, MAX_ATTEMPTS);
                    continue;
                }
                text = resp["content"][0]["text"].get<std::string>();

                // Usage: input_tokens + output_tokens
                if (resp.contains("usage"))
                {
                    int inputTokens  = resp["usage"].value("input_tokens", 0);
                    int outputTokens = resp["usage"].value("output_tokens", 0);
                    tokensUsed = inputTokens + outputTokens;
                }
            }
            else
            {
                // OpenAI response: choices[0].message.content
                text = resp["choices"][0]["message"]["content"].get<std::string>();

                if (resp.contains("usage") && resp["usage"].contains("total_tokens"))
                    tokensUsed = resp["usage"]["total_tokens"].get<int>();
            }

            text = Trim(text);
            // Strip surrounding quotes added by some models
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                text = text.substr(1, text.size() - 2);
            // Replace newlines with spaces so the reply fits on a single chat line
            for (char& c : text)
                if (c == '\n' || c == '\r') c = ' ';

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
