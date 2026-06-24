#ifndef MOD_PBC_LLM_H
#define MOD_PBC_LLM_H

#include <string>

// Result of a single LLM completion call
struct PBC_LLMResult
{
    bool        success;
    std::string text;       // the assistant reply (trimmed)
    int         tokensUsed; // approximate total tokens (prompt + completion), 0 if unknown
};

// ---------------------------------------------------------------------------
// API configuration — holds all parameters needed to make an LLM call.
// Populated from the main or alt model globals before calling
// PBC_CallLLMWithConfig().
// ---------------------------------------------------------------------------
struct PBC_APIConfig
{
    std::string apiType;            // "openai", "anthropic" or "ollama"
    std::string baseUrl;            // e.g. https://api.deepseek.com/v1
    std::string apiKey;             // Bearer token / x-api-key (empty = no auth header)
    std::string model;              // model identifier
    int         maxResponseTokens;  // 0 = unlimited / omit
    double      temperature;        // sampling temperature
    std::string modelExtraParameters; // raw JSON merged into request body (top level)
    int         requestTimeoutSec;  // HTTP timeout

    // --- Ollama-only (apiType == "ollama") ---------------------------------
    bool        ollamaThink        = false; // false → send think:false (suppress reasoning)
    std::string ollamaKeepAlive;            // keep_alive value (empty = omit, use Ollama default)
    int         ollamaNumCtx       = 0;     // options.num_ctx (0 = omit)
    std::string ollamaExtraOptions;         // raw JSON merged into the options object
};

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Universal synchronous LLM call — takes an explicit API configuration.
// Safe to call from any thread; does not touch game objects.
//
// maxTokensOverride:
//   0  (default) – use cfg.maxResponseTokens.
//  -1             – omit max_tokens entirely (let the model decide);
//                   intended for condensation and relationship updates
//                   where truncation would corrupt the output.
//
// preserveNewlines:
//   false (default) – newlines in the response are replaced with spaces
//                     so the reply fits on a single in-game chat line.
//   true            – newlines are kept intact; required for condensation
//                     output where each memory is on its own line.
PBC_LLMResult PBC_CallLLMWithConfig(const PBC_APIConfig& cfg,
                                     const std::string& systemPrompt,
                                     const std::string& userPrompt,
                                     int maxTokensOverride = 0,
                                     bool preserveNewlines = false);

// Convenience wrapper — uses the main model globals (g_PBC_*).
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           int maxTokensOverride = 0,
                           bool preserveNewlines = false);

// Convenience wrapper — uses the alt model globals (g_PBC_AltModel*).
PBC_LLMResult PBC_CallLLMAlt(const std::string& systemPrompt,
                               const std::string& userPrompt,
                               int maxTokensOverride = 0,
                               bool preserveNewlines = false);

// Estimate token count (rough: 1 token ≈ 4 chars).
int PBC_EstimateTokens(const std::string& text);

#endif // MOD_PBC_LLM_H
