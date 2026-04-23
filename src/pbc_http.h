#ifndef MOD_PBC_HTTP_H
#define MOD_PBC_HTTP_H

#include <string>
#include <vector>
#include <utility>

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

#endif // MOD_PBC_HTTP_H
