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
    std::string apiType;            // "openai" or "anthropic"
    std::string baseUrl;            // e.g. https://api.deepseek.com/v1
    std::string apiKey;             // Bearer token / x-api-key (empty = no auth header)
    std::string model;              // model identifier
    int         maxResponseTokens;  // 0 = unlimited / omit
    double      temperature;        // sampling temperature
    std::string modelExtraParameters; // raw JSON merged into request body
    int         requestTimeoutSec;  // HTTP timeout
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
PBC_LLMResult PBC_CallLLMWithConfig(const PBC_APIConfig& cfg,
                                     const std::string& systemPrompt,
                                     const std::string& userPrompt,
                                     int maxTokensOverride = 0);

// Convenience wrapper — uses the main model globals (g_PBC_*).
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           int maxTokensOverride = 0);

// Convenience wrapper — uses the alt model globals (g_PBC_AltModel*).
PBC_LLMResult PBC_CallLLMAlt(const std::string& systemPrompt,
                              const std::string& userPrompt,
                              int maxTokensOverride = 0);

// Estimate token count (rough: 1 token ≈ 4 chars).
int PBC_EstimateTokens(const std::string& text);

#endif // MOD_PBC_LLM_H
