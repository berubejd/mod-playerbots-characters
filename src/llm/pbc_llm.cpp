#include "pbc_llm.h"
#include "pbc_config.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"

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

// Merge PBC.ModelExtraParameters into the request body via JSON parse/update.
// String-splicing would duplicate keys (e.g. a top-level "options" fragment for
// Ollama would replace the bounded options object built above).
//
// Precedence (intentional): dedicated config keys win over ModelExtraParameters.
// temperature, max_tokens / num_predict, num_ctx, think, keep_alive, system, and
// core request shape are set above from PBC.Temperature, PBC.MaxResponseLength,
// maxTokensOverride, and Ollama-specific settings — they are NOT overridden when
// the same keys appear in ModelExtraParameters. Use ModelExtraParameters only for
// provider-specific extras (frequency_penalty, thinking, format, tools, ...).
static void MergeModelExtraParameters(json& body,
                                      const std::string& rawExtra,
                                      bool isOllama,
                                      bool isAnthropic)
{
    if (rawExtra.empty())
        return;

    std::string extra = rawExtra;
    std::replace(extra.begin(), extra.end(), '\'', '"');
    try
    {
        json extraJson = json::parse("{" + extra + "}");

        if (isOllama)
        {
            // Sampling knobs in ModelExtraParameters belong in options; merge
            // them there so temperature/num_ctx/num_predict caps are preserved.
            if (extraJson.contains("options") && extraJson["options"].is_object())
            {
                json extraOptions = std::move(extraJson["options"]);
                extraJson.erase("options");

                if (!body.contains("options") || !body["options"].is_object())
                    body["options"] = json::object();

                json bounded = json::object();
                for (const char* key : { "temperature", "num_ctx", "num_predict" })
                {
                    if (body["options"].contains(key))
                        bounded[key] = body["options"][key];
                }

                body["options"].update(extraOptions);
                for (auto& [key, val] : bounded.items())
                    body["options"][key] = val;
            }
            else if (extraJson.contains("options"))
            {
                PBC_Log(PBC_LogLevel::PBC_WARNING,
                          "Ollama: PBC.ModelExtraParameters 'options' must be a JSON object; "
                          "use PBC.OllamaExtraOptions for sampling knobs — ignoring invalid 'options'.");
                extraJson.erase("options");
            }

            for (auto& [key, val] : extraJson.items())
            {
                // Core request shape and dedicated Ollama settings win.
                if (key == "model" || key == "messages" || key == "stream")
                    continue;
                if (key == "options" && body.contains("options") && body["options"].is_object())
                    continue;
                if (key == "think" && body.contains("think"))
                    continue;
                if (key == "keep_alive" && body.contains("keep_alive"))
                    continue;
                body[key] = val;
            }
        }
        else
        {
            for (auto& [key, val] : extraJson.items())
            {
                if (key == "model" || key == "messages")
                    continue;
                if (isAnthropic && key == "system" && body.contains("system"))
                    continue;
                // Intentional: dedicated settings above win — do not let extras override.
                if (key == "max_tokens" && body.contains("max_tokens"))
                    continue;
                if (key == "temperature" && body.contains("temperature"))
                    continue;
                body[key] = val;
            }
        }
    }
    catch (const std::exception& ex)
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING,
                  "Failed to parse ModelExtraParameters ('{}'): {} — ignoring.",
                  PBC_SanitizeForFmt(rawExtra), ex.what());
    }
}

// ---------------------------------------------------------------------------
// PBC_CallLLMWithConfig  – universal synchronous LLM call
//   openai:     POST {baseUrl}/chat/completions  (OpenAI-compatible)
//   anthropic:  POST {baseUrl}/messages          (Anthropic Messages API)
// Safe to call from any thread; does not touch game objects.
// ---------------------------------------------------------------------------
PBC_LLMResult PBC_CallLLMWithConfig(const PBC_APIConfig& cfg,
                                     const std::string& systemPrompt,
                                     const std::string& userPrompt,
                                     int maxTokensOverride,
                                     bool preserveNewlines)
{
    PBC_LLMResult result{ false, "", 0 };

    // Clean any unknown {token} placeholders and log warnings.
    std::string sysPrompt  = systemPrompt;
    std::string usrPrompt  = userPrompt;
    PBC_CleanUnknownTokens(sysPrompt);
    PBC_CleanUnknownTokens(usrPrompt);

    const bool isAnthropic = IEquals(cfg.apiType, "anthropic");
    const bool isOllama    = IEquals(cfg.apiType, "ollama");

    // --- Build URL --------------------------------------------------------
    std::string url = cfg.baseUrl;
    if (!url.empty() && url.back() == '/')
        url.pop_back();
    if (isAnthropic)
        url += "/messages";
    else if (isOllama)
        url += "/api/chat";
    else
        url += "/chat/completions";

    // --- Build request body -----------------------------------------------
    json body;
    body["model"] = cfg.model;

    if (isAnthropic)
    {
        // Anthropic Messages API format
        // system is a top-level field, not part of the messages array
        if (!sysPrompt.empty())
            body["system"] = sysPrompt;

        json messages = json::array();
        messages.push_back({ {"role", "user"}, {"content", usrPrompt} });
        body["messages"] = messages;

        // max_tokens is required for Anthropic — unlike OpenAI, it cannot be omitted.
        // maxTokensOverride == -1 (condensation/relationship): use a generous limit
        //   so the model isn't truncated mid-output.
        // maxTokensOverride ==  0: use config value (cfg.maxResponseTokens).
        // maxTokensOverride  >  0: use the explicit override.
        if (maxTokensOverride == -1)
            body["max_tokens"] = 4096;
        else if (maxTokensOverride > 0)
            body["max_tokens"] = maxTokensOverride;
        else if (cfg.maxResponseTokens > 0)
            body["max_tokens"] = cfg.maxResponseTokens;
        else
            body["max_tokens"] = 4096;

        if (cfg.temperature >= 0.0)
            body["temperature"] = cfg.temperature;
    }
    else if (isOllama)
    {
        // Ollama native /api/chat format.
        // Messages mirror the OpenAI layout, but runtime/sampling parameters
        // live in a nested "options" object — top-level copies are ignored by
        // Ollama. think/keep_alive are genuine top-level fields.
        json messages = json::array();
        if (!sysPrompt.empty())
            messages.push_back({ {"role", "system"}, {"content", sysPrompt} });
        messages.push_back({ {"role", "user"}, {"content", usrPrompt} });
        body["messages"] = messages;

        // The single-shot client reads one JSON body; /api/chat defaults to a
        // streamed NDJSON response, so streaming must be disabled.
        body["stream"] = false;

        // think:false suppresses reasoning tokens (the latency fix).  Sent in
        // both states so a thinking model can be explicitly opted in.
        body["think"] = cfg.ollamaThink;

        // keep_alive keeps the model resident; omitted when unset so Ollama
        // applies its own default.  Ollama accepts either a JSON number
        // (seconds; negative = keep loaded indefinitely, 0 = unload now) or a
        // duration string ("5m", "1h").  A purely numeric value MUST be sent as
        // a number — as a string ("-1", "300") it has no duration unit and
        // Ollama rejects it, so it would never keep/unload as intended.
        if (!cfg.ollamaKeepAlive.empty())
        {
            const std::string& ka = cfg.ollamaKeepAlive;
            const size_t digitsStart = (ka[0] == '-' || ka[0] == '+') ? 1 : 0;
            const bool isNumeric = digitsStart < ka.size() &&
                ka.find_first_not_of("0123456789", digitsStart) == std::string::npos;
            if (isNumeric)
            {
                try
                {
                    body["keep_alive"] = std::stoll(ka);
                }
                catch (const std::exception&)
                {
                    body["keep_alive"] = ka; // out-of-range; fall back to string
                }
            }
            else
            {
                body["keep_alive"] = ka;
            }
        }

        json options = json::object();

        // Merge user-supplied sampling options FIRST (top_p, top_k,
        // repeat_penalty, seed, stop, ...).  Single quotes are accepted as in
        // ModelExtraParameters.  These are applied before the dedicated settings
        // below so that temperature/num_ctx/num_predict — which have their own
        // config keys and carry the bounding guarantee — always take precedence
        // and cannot be silently overridden from the extra fragment.
        if (!cfg.ollamaExtraOptions.empty())
        {
            std::string extra = cfg.ollamaExtraOptions;
            std::replace(extra.begin(), extra.end(), '\'', '"');
            try
            {
                json extraJson = json::parse("{" + extra + "}");
                options.update(extraJson);
            }
            catch (const std::exception& ex)
            {
                PBC_Log(PBC_LogLevel::PBC_WARNING,
                          "Ollama: failed to parse PBC.OllamaExtraOptions ('{}'): {} — ignoring.",
                          PBC_SanitizeForFmt(cfg.ollamaExtraOptions), ex.what());
            }
        }

        if (cfg.temperature >= 0.0)
            options["temperature"] = cfg.temperature;
        if (cfg.ollamaNumCtx > 0)
            options["num_ctx"] = cfg.ollamaNumCtx;

        // num_predict caps generation.  Unlike the OpenAI branch (which omits
        // max_tokens and lets a cloud model stop on its own), Ollama treats a
        // missing/negative num_predict as open-ended generation bounded only by
        // num_ctx — on local hardware that routinely overruns the request
        // timeout.  So, like the Anthropic branch, num_predict is ALWAYS set:
        //   -1 → generous bound (4096), >0 → explicit override,
        //   config value when positive, else the same generous bound (4096).
        // This guarantees no Ollama request is ever unbounded.
        if (maxTokensOverride == -1)
            options["num_predict"] = 4096;
        else if (maxTokensOverride > 0)
            options["num_predict"] = maxTokensOverride;
        else if (cfg.maxResponseTokens > 0)
            options["num_predict"] = cfg.maxResponseTokens;
        else
            options["num_predict"] = 4096;

        if (!options.empty())
            body["options"] = options;
    }
    else
    {
        // OpenAI-compatible /chat/completions format
        body["temperature"] = cfg.temperature;

        // maxTokensOverride == -1  → omit max_tokens (condensation / relationship calls)
        // maxTokensOverride ==  0  → use config value
        // maxTokensOverride  >  0  → use the override value
        if (maxTokensOverride == -1)
        {
            // intentionally omitted — let the model decide when to stop
        }
        else if (maxTokensOverride > 0)
            body["max_tokens"] = maxTokensOverride;
        else if (cfg.maxResponseTokens > 0)
            body["max_tokens"] = cfg.maxResponseTokens;

        json messages = json::array();
        if (!sysPrompt.empty())
            messages.push_back({ {"role", "system"}, {"content", sysPrompt} });
        messages.push_back({ {"role", "user"}, {"content", usrPrompt} });
        body["messages"] = messages;
    }

    MergeModelExtraParameters(body, cfg.modelExtraParameters, isOllama, isAnthropic);

    std::string bodyStr = body.dump();

    // --- Build headers ----------------------------------------------------
    std::vector<std::pair<std::string, std::string>> headers;

    if (isAnthropic)
    {
        // Anthropic uses x-api-key header and requires anthropic-version
        if (!cfg.apiKey.empty())
            headers.emplace_back("x-api-key", cfg.apiKey);
        headers.emplace_back("anthropic-version", "2023-06-01");
    }
    else
    {
        // OpenAI-compatible uses Authorization: Bearer
        if (!cfg.apiKey.empty())
            headers.emplace_back("Authorization", "Bearer " + cfg.apiKey);
    }

    if (g_PBC_DebugShowFullRequest)
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM request body:\n{}", PBC_SanitizeForFmt(PBC_TruncateForDebug(bodyStr)));
    }

    // --- Execute request --------------------------------------------------
    constexpr int MAX_ATTEMPTS = 2;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        if (attempt > 1)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM: waiting 3s before retry (attempt {}/{})...", attempt, MAX_ATTEMPTS);
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        PBC_HttpClient http;
        http.SetTimeoutSeconds(cfg.requestTimeoutSec);
        std::string responseBody = http.Post(url, bodyStr, headers);

        if (responseBody.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: empty response from API (attempt {}/{}).", attempt, MAX_ATTEMPTS);
            continue;
        }

        if (g_PBC_DebugShowFullRequest)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM response body:\n{}", PBC_SanitizeForFmt(PBC_TruncateForDebug(responseBody)));
        }

        try
        {
            json resp = json::parse(responseBody);

            if (resp.contains("error"))
            {
                std::string errMsg;
                if (resp["error"].is_object() && resp["error"].contains("message"))
                    errMsg = resp["error"]["message"].get<std::string>();
                else if (resp["error"].is_string())
                    errMsg = resp["error"].get<std::string>();   // Ollama returns a bare string
                else
                    errMsg = responseBody;
                PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM API error (attempt {}/{}): {}",
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
                    PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: unexpected Anthropic response format (attempt {}/{}).", attempt, MAX_ATTEMPTS);
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
            else if (isOllama)
            {
                // Ollama response: message.content (must be a string; null/missing is invalid)
                const json& msg = resp["message"];
                if (!msg.is_object() || !msg.contains("content") || !msg["content"].is_string())
                {
                    PBC_Log(PBC_LogLevel::PBC_ERROR, "LLM: unexpected Ollama response format (attempt {}/{}).", attempt, MAX_ATTEMPTS);
                    continue;
                }
                text = msg["content"].get<std::string>();

                // Usage: prompt_eval_count (prompt) + eval_count (completion)
                int promptTokens = resp.value("prompt_eval_count", 0);
                int evalTokens   = resp.value("eval_count", 0);
                tokensUsed = promptTokens + evalTokens;
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
            // (unless the caller explicitly needs them, e.g. condensation output)
            if (!preserveNewlines)
                for (char& c : text)
                    if (c == '\n' || c == '\r') c = ' ';

            result.success    = true;
            result.text       = text;
            result.tokensUsed = tokensUsed;

            PBC_Log(PBC_LogLevel::PBC_DEBUG, "LLM reply ({} tokens): {}", tokensUsed, PBC_SanitizeForFmt(text));

            return result;
        }
        catch (const std::exception& ex)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "JSON parse error (attempt {}/{}): {}", attempt, MAX_ATTEMPTS, ex.what());
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Raw response: {}", PBC_SanitizeForFmt(responseBody));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Convenience wrappers — populate PBC_APIConfig from the appropriate globals
// ---------------------------------------------------------------------------

PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           int maxTokensOverride,
                           bool preserveNewlines)
{
    PBC_APIConfig cfg;
    cfg.apiType              = g_PBC_APIType;
    cfg.baseUrl              = g_PBC_BaseUrl;
    cfg.apiKey               = g_PBC_ApiKey;
    cfg.model                = g_PBC_Model;
    cfg.maxResponseTokens    = g_PBC_MaxResponseTokens;
    cfg.temperature          = g_PBC_Temperature;
    cfg.modelExtraParameters = g_PBC_ModelExtraParameters;
    cfg.requestTimeoutSec    = g_PBC_RequestTimeoutSec;
    cfg.ollamaThink          = g_PBC_OllamaThink;
    cfg.ollamaKeepAlive      = g_PBC_OllamaKeepAlive;
    cfg.ollamaNumCtx         = g_PBC_OllamaNumCtx;
    cfg.ollamaExtraOptions   = g_PBC_OllamaExtraOptions;
    return PBC_CallLLMWithConfig(cfg, systemPrompt, userPrompt, maxTokensOverride, preserveNewlines);
}

PBC_LLMResult PBC_CallLLMAlt(const std::string& systemPrompt,
                               const std::string& userPrompt,
                               int maxTokensOverride,
                               bool preserveNewlines)
{
    PBC_APIConfig cfg;
    cfg.apiType              = g_PBC_AltModelAPIType;
    cfg.baseUrl              = g_PBC_AltModelBaseUrl;
    cfg.apiKey               = g_PBC_AltModelApiKey;
    cfg.model                = g_PBC_AltModel;
    cfg.maxResponseTokens    = g_PBC_AltModelMaxResponseTokens;
    cfg.temperature          = g_PBC_AltModelTemperature;
    cfg.modelExtraParameters = g_PBC_AltModelModelExtraParameters;
    cfg.requestTimeoutSec    = g_PBC_AltModelRequestTimeoutSec;
    cfg.ollamaThink          = g_PBC_AltModelOllamaThink;
    cfg.ollamaKeepAlive      = g_PBC_AltModelOllamaKeepAlive;
    cfg.ollamaNumCtx         = g_PBC_AltModelOllamaNumCtx;
    cfg.ollamaExtraOptions   = g_PBC_AltModelOllamaExtraOptions;
    return PBC_CallLLMWithConfig(cfg, systemPrompt, userPrompt, maxTokensOverride, preserveNewlines);
}
