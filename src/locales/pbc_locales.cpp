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
// Public API
// ---------------------------------------------------------------------------

std::string PBC_Localize(const std::string& english)
{
    return PBC_ResolveTemplate(english);
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    PBC_ReplacePlaceholder(tmpl, "{1}", a1);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    PBC_ReplacePlaceholder(tmpl, "{1}", a1);
    PBC_ReplacePlaceholder(tmpl, "{2}", a2);
    return tmpl;
}

std::string PBC_Localize(const std::string& key,
                         const std::string& a0,
                         const std::string& a1,
                         const std::string& a2,
                         const std::string& a3)
{
    std::string tmpl = PBC_ResolveTemplate(key);
    PBC_ReplacePlaceholder(tmpl, "{0}", a0);
    PBC_ReplacePlaceholder(tmpl, "{1}", a1);
    PBC_ReplacePlaceholder(tmpl, "{2}", a2);
    PBC_ReplacePlaceholder(tmpl, "{3}", a3);
    return tmpl;
}
