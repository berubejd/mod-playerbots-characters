#ifndef MOD_PBC_HTTP_AUTH_H
#define MOD_PBC_HTTP_AUTH_H

#include <string>
#include <cstdint>

// Exchange an OTP for a bearer token.
// Validates the OTP, consumes it (one-time use), and returns an
// AES-256-CBC encrypted bearer token, or "" on failure.
std::string PBC_ExchangeOTP(const std::string& otp);

// Validate a bearer token and return the associated player GUID.
// Returns 0 if the token is invalid or expired.
uint64_t PBC_ValidateToken(const std::string& token);

// Remove expired tokens from the map (call periodically).
void PBC_CleanupExpiredTokens();

#endif // MOD_PBC_HTTP_AUTH_H
