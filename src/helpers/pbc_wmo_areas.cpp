#include "pbc_wmo_areas.h"
#include "Log.h"
#include "World.h"

#include <fstream>
#include <cstring>

// ---------------------------------------------------------------------------
// DBC binary format constants for WMOAreaTable
//
// Record layout (28 fields × 4 bytes = 112 bytes per record):
//   Field  0: uint32 ID
//   Field  1: int32  rootId
//   Field  2: int32  adtId
//   Field  3: int32  groupId
//   Fields 4-8: uint32 (unused)
//   Field  9: uint32 Flags
//   Field 10: uint32 areaId
//   Fields 11-26: uint32 string offsets (16 locales) into string block
//   Field 27: uint32 name flags
// ---------------------------------------------------------------------------

static constexpr uint32_t WMO_DBC_FIELD_COUNT = 28;
static constexpr uint32_t WMO_DBC_RECORD_SIZE = WMO_DBC_FIELD_COUNT * 4;
static constexpr uint32_t WMO_DBC_STRING_OFFSET_FIELD = 11; // enUS locale index

std::map<PBC_WmoAreaKey, std::string> g_PBC_WmoAreaNames;

uint32_t PBC_LoadWMOAreaNames()
{
    g_PBC_WmoAreaNames.clear();

    std::string dbcPath = sWorld->GetDataPath() + "dbc/WMOAreaTable.dbc";

    std::ifstream file(dbcPath, std::ios::binary);
    if (!file.is_open())
    {
        LOG_WARN("server.loading", "[PBC] Could not open WMOAreaTable.dbc at '{}' — WMO area names will not be available.", dbcPath);
        return 0;
    }

    // Read DBC header (20 bytes)
    char magic[4];
    uint32_t recordCount, fieldCount, recordSize, stringBlockSize;

    file.read(magic, 4);
    if (std::memcmp(magic, "WDBC", 4) != 0)
    {
        LOG_ERROR("server.loading", "[PBC] WMOAreaTable.dbc has invalid magic — skipping.");
        return 0;
    }

    file.read(reinterpret_cast<char*>(&recordCount), 4);
    file.read(reinterpret_cast<char*>(&fieldCount), 4);
    file.read(reinterpret_cast<char*>(&recordSize), 4);
    file.read(reinterpret_cast<char*>(&stringBlockSize), 4);

    if (fieldCount != WMO_DBC_FIELD_COUNT)
    {
        LOG_ERROR("server.loading", "[PBC] WMOAreaTable.dbc has unexpected field count {} (expected {}) — skipping.", fieldCount, WMO_DBC_FIELD_COUNT);
        return 0;
    }

    if (recordSize != WMO_DBC_RECORD_SIZE)
    {
        LOG_ERROR("server.loading", "[PBC] WMOAreaTable.dbc has unexpected record size {} (expected {}) — skipping.", recordSize, WMO_DBC_RECORD_SIZE);
        return 0;
    }

    // Read all records + string block in one go
    uint32_t recordsSize = recordCount * recordSize;
    std::vector<char> data(recordsSize + stringBlockSize);
    file.read(data.data(), data.size());
    if (!file)
    {
        LOG_ERROR("server.loading", "[PBC] WMOAreaTable.dbc read error — skipping.");
        return 0;
    }
    file.close();

    const char* records = data.data();
    const char* stringBlock = data.data() + recordsSize;

    // Determine which locale index to use (same as the server's DBC locale)
    // Locale 0 = enUS. We try the server's default locale first, fall back to enUS.
    LocaleConstant locale = sWorld->GetDefaultDbcLocale();
    uint32_t localeField = WMO_DBC_STRING_OFFSET_FIELD + static_cast<uint32_t>(locale);
    if (localeField >= WMO_DBC_STRING_OFFSET_FIELD + 16)
        localeField = WMO_DBC_STRING_OFFSET_FIELD; // fallback to enUS

    uint32_t loaded = 0;
    for (uint32_t i = 0; i < recordCount; ++i)
    {
        const char* rec = records + i * recordSize;

        int32_t rootId, adtId, groupId;
        std::memcpy(&rootId,   rec + 1 * 4, 4);
        std::memcpy(&adtId,    rec + 2 * 4, 4);
        std::memcpy(&groupId,  rec + 3 * 4, 4);

        // Read string offset for the chosen locale
        uint32_t strOffset;
        std::memcpy(&strOffset, rec + localeField * 4, 4);

        if (strOffset == 0 || strOffset >= stringBlockSize)
            continue; // no name for this locale

        const char* name = stringBlock + strOffset;
        if (name[0] == '\0')
            continue; // empty string

        PBC_WmoAreaKey key{rootId, adtId, groupId};
        g_PBC_WmoAreaNames[key] = name;
        ++loaded;
    }

    LOG_INFO("server.loading", "[PBC] Loaded {} WMO area names from WMOAreaTable.dbc.", loaded);
    return loaded;
}

std::string PBC_GetWmoAreaName(int32_t rootId, int32_t adtId, int32_t groupId)
{
    // Try exact match first
    auto it = g_PBC_WmoAreaNames.find(PBC_WmoAreaKey{rootId, adtId, groupId});
    if (it != g_PBC_WmoAreaNames.end() && !it->second.empty())
        return it->second;

    // Fall back to groupId=-1 wildcard entry (used in DBC as default name for all groups in a WMO root/ADT)
    it = g_PBC_WmoAreaNames.find(PBC_WmoAreaKey{rootId, adtId, -1});
    if (it != g_PBC_WmoAreaNames.end() && !it->second.empty())
        return it->second;

    return {};
}
