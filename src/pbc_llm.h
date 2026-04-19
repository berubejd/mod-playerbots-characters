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
// API
// ---------------------------------------------------------------------------

// Synchronous call – blocks the calling thread until the LLM responds.
// Safe to call from any thread; does not touch game objects.
//
// maxTokensOverride:
//   0  (default) – use g_PBC_MaxResponseTokens from config.
//  -1             – omit max_tokens entirely (let the model decide);
//                   intended for condensation and relationship updates
//                   where truncation would corrupt the output.
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           int maxTokensOverride = 0);

// Estimate token count (rough: 1 token ≈ 4 chars).
int PBC_EstimateTokens(const std::string& text);

#endif // MOD_PBC_LLM_H
