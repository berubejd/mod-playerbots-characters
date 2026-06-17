#include "pbc_locales.h"
#include "World.h"
#include "Common.h"

// ---------------------------------------------------------------------------
// Forward declarations — each locale .cpp file provides exactly one of these
// ---------------------------------------------------------------------------
const PBC_LocaleMap* PBC_GetLocaleMap_deDE();
const PBC_LocaleMap* PBC_GetLocaleMap_ruRU();

// ---------------------------------------------------------------------------
// PBC_GetLocaleMap
// ---------------------------------------------------------------------------
const PBC_LocaleMap* PBC_GetLocaleMap(uint8_t locale)
{
    switch (locale)
    {
        case LOCALE_deDE: return PBC_GetLocaleMap_deDE();
        case LOCALE_ruRU: return PBC_GetLocaleMap_ruRU();
        default:          return nullptr;   // enUS or unsupported — use English
    }
}

// ---------------------------------------------------------------------------
// Internal: resolve the template string (localised or fallback to key)
// ---------------------------------------------------------------------------
static std::string PBC_ResolveTemplate(const std::string& key)
{
    const PBC_LocaleMap* map = PBC_GetLocaleMap(static_cast<uint8_t>(sWorld->GetDefaultDbcLocale()));
    if (!map)
        return key;

    auto it = map->find(key);
    return (it != map->end()) ? it->second : key;
}

// ---------------------------------------------------------------------------
// Internal: replace "{0}".."{3}" placeholders in tmpl with given strings
// ---------------------------------------------------------------------------
static void PBC_ReplacePlaceholder(std::string& tmpl, const std::string& token, const std::string& value)
{
    size_t pos = 0;
    while ((pos = tmpl.find(token, pos)) != std::string::npos)
    {
        tmpl.replace(pos, token.size(), value);
        pos += value.size();
    }
}

// ---------------------------------------------------------------------------
// Internal: resolve gendered constructions of the form
//
//     [m:male form|f:female form]
//
// The resolver picks the male or female half according to `gender`.  When the
// gender is unknown / not applicable (PBC_GENDER_UNSPECIFIED, the default)
// the male form is used.  Templates without any such block are left
// untouched, so the gender parameter is harmless for strings that don't use
// it.  The construction is removed entirely (replaced by the chosen form).
// ---------------------------------------------------------------------------
static void PBC_ResolveGender(std::string& tmpl, uint8_t gender)
{
    size_t pos = 0;
    while ((pos = tmpl.find("[m:", pos)) != std::string::npos)
    {
        size_t end = tmpl.find(']', pos);
        if (end == std::string::npos)
            break;   // malformed — leave the rest untouched

        // Extract the inner content between "[m:" and "]".
        std::string inner = tmpl.substr(pos + 3, end - (pos + 3));

        size_t sep = inner.find("|f:");
        std::string maleForm;
        std::string femaleForm;
        if (sep != std::string::npos)
        {
            maleForm   = inner.substr(0, sep);
            femaleForm = inner.substr(sep + 3);
        }
        else
        {
            // No female half — both genders use the male form.
            maleForm   = inner;
            femaleForm = inner;
        }

        bool useFemale = (gender == PBC_GENDER_FEMALE);
        const std::string& chosen = useFemale ? femaleForm : maleForm;

        tmpl.replace(pos, end - pos + 1, chosen);
        pos += chosen.size();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string PBC_Localize(const std::string& english, uint8_t gender)
{
    std::string tmpl = PBC_ResolveTemplate(english);
    PBC_ResolveGender(tmpl, gender);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         uint8_t gender)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ResolveGender(tmpl, gender);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         uint8_t gender)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ResolveGender(tmpl, gender);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    PBC_ReplacePlaceholder(tmpl, "{1}", a1);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2,
                         uint8_t gender)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ResolveGender(tmpl, gender);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    PBC_ReplacePlaceholder(tmpl, "{1}", a1);
    PBC_ReplacePlaceholder(tmpl, "{2}", a2);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2,
                         const std::string& a3,
                         uint8_t gender)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ResolveGender(tmpl, gender);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    PBC_ReplacePlaceholder(tmpl, "{1}", a1);
    PBC_ReplacePlaceholder(tmpl, "{2}", a2);
    PBC_ReplacePlaceholder(tmpl, "{3}", a3);
    return tmpl;
}
