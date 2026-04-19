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
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt);

// Estimate token count (rough: 1 token ≈ 4 chars).
int PBC_EstimateTokens(const std::string& text);

#endif // MOD_PBC_LLM_H
