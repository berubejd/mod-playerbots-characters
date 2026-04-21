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

#endif // MOD_PBC_HTTP_H
