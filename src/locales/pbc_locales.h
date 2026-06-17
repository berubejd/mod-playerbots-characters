#ifndef MOD_PBC_LOCALES_H
#define MOD_PBC_LOCALES_H

#include <cstdint>
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
// Every overload above also accepts an optional trailing `gender` parameter
// (see PBC_GENDER_* below).  When a localised template contains a gendered
// construction of the form
//
//     [m:male form|f:female form]
//
// the resolver picks the matching half according to `gender`.  If the gender
// is unknown / not applicable (PBC_GENDER_UNSPECIFIED, the default) the male
// form is used.  Templates without any `[m:…|f:…]` block are unaffected, so
// the gender parameter is harmless for strings that don't use it.
//
// The English key is used as the lookup in the locale map.  If the current
// DBC.Locale has no translation (unsupported language or missing entry) the
// English key itself is used as the format template — so enUS works without
// any locale file.
// ---------------------------------------------------------------------------

using PBC_LocaleMap = std::unordered_map<std::string, std::string>;

// Gender values mirror the core Gender enum (GENDER_MALE = 0, GENDER_FEMALE =
// 1, GENDER_NONE = 2) so that Player::GetGender() can be passed directly.
// PBC_GENDER_UNSPECIFIED is the default and means "no gender given" — the
// resolver falls back to the male form in that case.
enum PBC_Gender : uint8_t
{
    PBC_GENDER_MALE        = 0,
    PBC_GENDER_FEMALE      = 1,
    PBC_GENDER_NONE        = 2,
    PBC_GENDER_UNSPECIFIED = 0xFF
};

// Returns the translation map for the given locale, or nullptr if none.
const PBC_LocaleMap* PBC_GetLocaleMap(uint8_t locale);

// ---- Base (no substitution) --------------------------------------------
std::string PBC_Localize(const std::string& english,
                         uint8_t gender = PBC_GENDER_UNSPECIFIED);

inline std::string PBC_Localize(const char* english,
                                uint8_t gender = PBC_GENDER_UNSPECIFIED)
{
    return PBC_Localize(std::string(english), gender);
}

// ---- 1-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         uint8_t gender = PBC_GENDER_UNSPECIFIED);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                uint8_t gender = PBC_GENDER_UNSPECIFIED)
{
    return PBC_Localize(std::string(key), a0, gender);
}

// ---- 2-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         uint8_t gender = PBC_GENDER_UNSPECIFIED);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                const std::string& a1,
                                uint8_t gender = PBC_GENDER_UNSPECIFIED)
{
    return PBC_Localize(std::string(key), a0, a1, gender);
}

// ---- 3-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2,
                         uint8_t gender = PBC_GENDER_UNSPECIFIED);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                const std::string& a1,
                                const std::string& a2,
                                uint8_t gender = PBC_GENDER_UNSPECIFIED)
{
    return PBC_Localize(std::string(key), a0, a1, a2, gender);
}

// ---- 4-argument substitution -------------------------------------------
std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2,
                         const std::string& a3,
                         uint8_t gender = PBC_GENDER_UNSPECIFIED);

inline std::string PBC_Localize(const char* key,
                                const std::string& a0,
                                const std::string& a1,
                                const std::string& a2,
                                const std::string& a3,
                                uint8_t gender = PBC_GENDER_UNSPECIFIED)
{
    return PBC_Localize(std::string(key), a0, a1, a2, a3, gender);
}

#endif // MOD_PBC_LOCALES_H
