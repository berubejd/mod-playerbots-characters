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
// Common handler utilities
// ---------------------------------------------------------------------------

// Authenticate request: extract Bearer token from Authorization header,
// validate it, look up the Player*. Returns nullptr on auth failure.
// Sets res.status to 401 and returns nullptr if authentication fails.
Player* AuthenticateRequest(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// Player online guard: returns true if player is valid and in-world.
// Sets res.status to 410 (Gone) and returns false if offline.
bool PlayerOnlineGuard(Player* player, pbc_httplib::Response& res);

// Parse :guid from URL params. Returns 0 on failure (and sets 400 status).
uint64_t ParseGuidParam(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// ---------------------------------------------------------------------------
// REST endpoint handlers
//
// Handlers that take Player* require prior authentication via
// AuthenticateRequest().  Handlers without Player* are unauthenticated.
// ---------------------------------------------------------------------------

// GET / — serve frontend static files or hello JSON
void HandleGetRoot(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// GET /api/token — OTP exchange, no auth required, uses ?otp= query param
void HandleGetToken(const pbc_httplib::Request& req, pbc_httplib::Response& res);

// GET /api/player — player info (name, race, class, level)
void HandleGetPlayer(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/party — party member list
void HandleGetParty(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/config — module config (prompts, thresholds, etc.)
void HandleGetConfig(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/card — character card JSON
void HandleGetCharCard(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/context — context with annotations
void HandleGetCharContext(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/history — list history entries (paginated)
void HandleGetCharHistory(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/history — edit history entry
void HandlePostCharHistory(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// DELETE /api/char/:guid/history — delete history entry
void HandleDeleteCharHistory(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/memory/count — memory count
void HandleGetCharMemoryCount(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/memory — list memories (paginated, sortable)
void HandleGetCharMemory(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/memory/:id — edit memory
void HandlePostCharMemory(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// DELETE /api/char/:guid/memory/:id — delete memory
void HandleDeleteCharMemory(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/relationships — list relationships
void HandleGetCharRelationships(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/relationships — edit relationship
void HandlePostCharRelationships(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// DELETE /api/char/:guid/relationships — delete relationship
void HandleDeleteCharRelationships(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/data — get character data
void HandleGetCharData(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/data — update character data
void HandlePostCharData(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// GET /api/char/:guid/debug/request — debug prompt
void HandleGetCharDebugRequest(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/whisper — send whisper
void HandlePostCharWhisper(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/narrate — narrate
void HandlePostCharNarrate(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/party/narrate — party narrate
void HandlePostPartyNarrate(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/char/:guid/trigger — trigger response
void HandlePostCharTrigger(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

// POST /api/party/message — party message
void HandlePostPartyMessage(const pbc_httplib::Request& req, pbc_httplib::Response& res, Player* authenticatedPlayer);

#endif // MOD_PBC_HTTP_HANDLERS_H
