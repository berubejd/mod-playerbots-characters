#ifndef MOD_PBC_HTTP_H
#define MOD_PBC_HTTP_H

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

// Thin wrapper around cpp-httplib for synchronous HTTP/HTTPS POST requests.
// Supports custom auth headers (Bearer token, x-api-key, etc.).
class PBC_HttpClient
{
public:
    PBC_HttpClient();

    // POST jsonData to url with optional extra headers.
    // extraHeaders is a list of name/value pairs added to the request.
    // Returns the raw response body, or "" on error.
    std::string Post(const std::string& url,
                     const std::string& jsonData,
                     const std::vector<std::pair<std::string, std::string>>& extraHeaders = {});

    void SetTimeoutSeconds(int seconds);

private:
    int m_timeoutSec;
};

// ---------------------------------------------------------------------------
// HTTP/WS server — inbound request handling.
//
// Start() binds to the given address:port and spawns a listener thread that
// serves both plain HTTP and WebSocket connections.  Stop() shuts the server
// down and joins the thread.  All httplib internals are hidden inside the
// .cpp file so the header stays lightweight.
// ---------------------------------------------------------------------------

// Start the HTTP/WS server.  Returns true if binding succeeded.
// On failure the server is not running — the caller should treat it as disabled.
bool PBC_HttpServerStart(const std::string& bindAddr, int port, int timeoutSec);

// Stop the server (if running) and join the listener thread.
void PBC_HttpServerStop();

// Returns true if the server is currently listening.
bool PBC_HttpServerIsRunning();

// ---------------------------------------------------------------------------
// OTP generation (called from the main thread by .chars web command).
//
// Generates a random 6-digit one-time password for the given account ID.
// The OTP is valid for 2 minutes and can be exchanged for a bearer token
// via the /api/token endpoint.  Returns the OTP string, or "" on error.
// ---------------------------------------------------------------------------
std::string PBC_HttpServerGenerateOTP(uint32_t accountId);

// ---------------------------------------------------------------------------
// WebSocket event notifications (thread-safe, no-op when server is not running)
//
// Called from the event thread or main thread to push real-time notifications
// to WS clients that are subscribed to a specific character GUID.
// ---------------------------------------------------------------------------

// Notify subscribed WS clients of a simple event type ("thinks", "relationship", "additions").
void PBC_WsNotify(uint64_t botGuid, const std::string& eventType);

// Notify subscribed WS clients of a history event with message data.
// messageId is the 1-based history index (matching the "id" field from GET /api/history).
void PBC_WsNotifyHistory(uint64_t botGuid, size_t messageId, const std::string& text);

#endif // MOD_PBC_HTTP_H
