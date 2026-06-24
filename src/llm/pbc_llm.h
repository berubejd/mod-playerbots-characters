#ifndef MOD_PBC_LLM_H
#define MOD_PBC_LLM_H

#include "pbc_json.h"
#include <string>
#include <vector>

// Result of a single LLM completion call
struct PBC_LLMResult
{
    bool        success;
    std::string text;       // the assistant reply (trimmed)
    int         tokensUsed; // approximate total tokens (prompt + completion), 0 if unknown
};

// One message in a multi-turn conversation (for few-shot prompting).
// role is "user" or "assistant"; the system prompt is passed separately.
struct PBC_ChatTurn
{
    std::string role;
    std::string content;
};

// ---------------------------------------------------------------------------
// API configuration — holds all parameters needed to make an LLM call.
// Populated from a connection file (or synthesized from legacy config) and
// stored in the connection registry (see pbc_config.h).
// ---------------------------------------------------------------------------
struct PBC_APIConfig
{
    std::string     apiType;            // "openai", "anthropic", or "ollama"
    std::string     baseUrl;            // e.g. https://api.openai.com/v1
    std::string     apiKey;             // Bearer token / x-api-key (empty = no auth header)
    std::string     model;              // model identifier
    int             requestTimeoutSec;  // HTTP timeout
    pbc_json        requestParameters;  // extra params merged into every request body

    // When true, constrain the provider to emit a valid JSON object
    // (Ollama format:"json", OpenAI response_format json_object; Anthropic has
    // no native mode and relies on the prompt).  Used by structured card calls.
    // Not read from connection files — callers set it on a local copy.
    bool            jsonMode = false;
};

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Universal synchronous LLM call — takes an explicit API configuration.
// Safe to call from any thread; does not touch game objects.
//
// preserveNewlines:
//   false (default) – newlines in the response are replaced with spaces
//                     so the reply fits on a single in-game chat line.
//   true            – newlines are kept intact; required for condensation
//                     output where each memory is on its own line.
PBC_LLMResult PBC_CallLLMWithConfig(const PBC_APIConfig& cfg,
                                     const std::string& systemPrompt,
                                     const std::string& userPrompt,
                                     bool preserveNewlines = false);

// Multi-turn variant — `turns` is the full user/assistant sequence (few-shot
// example pairs followed by the live user message).  Honors cfg.jsonMode.
PBC_LLMResult PBC_CallLLMConversation(const PBC_APIConfig& cfg,
                                       const std::string& systemPrompt,
                                       const std::vector<PBC_ChatTurn>& turns,
                                       bool preserveNewlines = false);

// Convenience wrapper — uses the "default" connection from the registry.
PBC_LLMResult PBC_CallLLM(const std::string& systemPrompt,
                           const std::string& userPrompt,
                           bool preserveNewlines = false);

// Estimate token count (rough: 1 token ≈ 4 chars).
int PBC_EstimateTokens(const std::string& text);

#endif // MOD_PBC_LLM_H
