#ifndef MOD_PBC_HTTP_H
#define MOD_PBC_HTTP_H

#include <string>

// Thin wrapper around cpp-httplib for synchronous HTTP/HTTPS POST requests.
// Supports Bearer token auth header.
class PBC_HttpClient
{
public:
    PBC_HttpClient();

    // POST jsonData to url, optionally adding Authorization: Bearer <apiKey>.
    // Returns the raw response body, or "" on error.
    std::string Post(const std::string& url,
                     const std::string& jsonData,
                     const std::string& apiKey = "");

    void SetTimeoutSeconds(int seconds);

private:
    int m_timeoutSec;
};

#endif // MOD_PBC_HTTP_H
