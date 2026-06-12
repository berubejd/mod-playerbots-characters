#ifndef MOD_PBC_LOCALES_H
#define MOD_PBC_LOCALES_H

#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Locale support for prompt variable strings
//
// PBC_Localize(key)                → plain lookup, returns translated string
// PBC_Localize(key, a0)            → replaces {0} with a0
// PBC_Localize(key, a0, a1)        → replaces {0} and {1}
// PBC_Localize(key, a0, a1, a2)    → replaces {0}, {1}, {2}
// PBC_Localize(key, a0, a1, a2, a3)→ replaces {0}–{3}
//
// The English key is used as the lookup in the locale map.  If the current
// DBC.Locale has no translation (unsupported language or missing entry) the
// English key itself is used as the format template — so enUS works without
// any locale file.
// ---------------------------------------------------------------------------

using PBC_LocaleMap = std::unordered_map<std::string, std::string>;

// Returns the translation map for the given locale, or nullptr if none.
const PBC_LocaleMap* PBC_GetLocaleMap(uint8_t locale);

// ---- Base (no substitution) --------------------------------------------
std::string PBC_Localize(const std::string& english);

inline std::string PBC_Localize(const char* english)
{
    return PBC_Localize(std::string(english));
}

// ---- 1-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0)
{
    return PBC_Localize(std::string(key), a0);
}

// ---- 2-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                const std::string& a1)
{
    return PBC_Localize(std::string(key), a0, a1);
}

// ---- 3-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                const std::string& a1,
                                const std::string& a2)
{
    return PBC_Localize(std::string(key), a0, a1, a2);
}

// ---- 4-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2,
                         const std::string& a3);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                const std::string& a1,
                                const std::string& a2,
                                const std::string& a3)
{
    return PBC_Localize(std::string(key), a0, a1, a2, a3);
}

#endif // MOD_PBC_LOCALES_H
