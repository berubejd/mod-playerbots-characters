#ifndef MOD_PBC_HTTP_HANDLERS_H
#define MOD_PBC_HTTP_HANDLERS_H

#include <string>
#include <cstdint>

namespace pbc_httplib {
struct Request;
struct Response;
}

class Player;

// ---------------------------------------------------------------------------
// Auth info returned after successful token validation.
// ---------------------------------------------------------------------------
struct PBC_AuthInfo
{
    uint32_t    accountId;
    std::string accountName;
};

// ---------------------------------------------------------------------------
// Common handler utilities
// ---------------------------------------------------------------------------

// Authenticate request: extract Bearer token from Authorization header,
// validate it, populate authInfo with account ID and name.
// Returns true on success. Sets res.status and returns false on auth failure.
bool AuthenticateRequest(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                         PBC_AuthInfo& authInfo);

// Find any online real (non-bot) player belonging to the given account.
// Returns nullptr if no such player is online.
Player* FindOnlinePlayerForAccount(uint32_t accountId);

// Find an online character (bot or real player) by GUID.
// Returns nullptr if the character is not online.
Player* FindOnlineCharacter(uint64_t guid);

// Parse :guid from URL params. Returns 0 on failure (and sets 400 status).
uint64_t ParseGuidParam(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// ---------------------------------------------------------------------------
// REST endpoint handlers
// ---------------------------------------------------------------------------

// GET / — serve frontend static files or hello JSON
void HandleGetRoot(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// GET /api/token — OTP exchange, no auth required, uses ?otp= query param
void HandleGetToken(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// GET /api/account — account info + all characters on the account
void HandleGetAccount(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                      const PBC_AuthInfo& authInfo);

// GET /api/party — party member GUIDs (filtered to the authenticated account)
void HandleGetParty(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                    const PBC_AuthInfo& authInfo);

// GET /api/config — module config (prompts, thresholds, etc.)
void HandleGetConfig(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                     const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/card — character card JSON
void HandleGetCharCard(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                       const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/context — context with annotations
void HandleGetCharContext(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                          const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/history — list history entries (paginated)
void HandleGetCharHistory(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                          const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/history — edit history entry
void HandlePostCharHistory(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                           const PBC_AuthInfo& authInfo);

// DELETE /api/char/:guid/history — delete history entry
void HandleDeleteCharHistory(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                             const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/memory/count — memory count
void HandleGetCharMemoryCount(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                              const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/memory — list memories (paginated, sortable)
void HandleGetCharMemory(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                         const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/memory/:id — edit memory
void HandlePostCharMemory(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                          const PBC_AuthInfo& authInfo);

// DELETE /api/char/:guid/memory/:id — delete memory
void HandleDeleteCharMemory(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                            const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/relationships — list relationships
void HandleGetCharRelationships(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                                const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/relationships — edit relationship
void HandlePostCharRelationships(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                                 const PBC_AuthInfo& authInfo);

// DELETE /api/char/:guid/relationships — delete relationship
void HandleDeleteCharRelationships(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                                   const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/data — get character data
void HandleGetCharData(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                       const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/data — update character data
void HandlePostCharData(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                        const PBC_AuthInfo& authInfo);

// GET /api/char/:guid/debug/request — debug prompt
void HandleGetCharDebugRequest(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                               const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/whisper — send whisper
void HandlePostCharWhisper(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                           const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/narrate — narrate
void HandlePostCharNarrate(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                           const PBC_AuthInfo& authInfo);

// POST /api/party/narrate — party narrate
void HandlePostPartyNarrate(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                            const PBC_AuthInfo& authInfo);

// POST /api/char/:guid/trigger — trigger response
void HandlePostCharTrigger(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                           const PBC_AuthInfo& authInfo);

// POST /api/party/message — party message
void HandlePostPartyMessage(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                             const PBC_AuthInfo& authInfo);

// POST /api/regen-last — regenerate the last event's responses
void HandlePostRegenLast(const pbc_httplib::Request& req, pbc_httplib::Response& res,
                         const PBC_AuthInfo& authInfo);

#endif // MOD_PBC_HTTP_HANDLERS_H
