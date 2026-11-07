#pragma once

#include "dmx/DmxMap.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bsl::dmx {

struct FixtureMode
{
    std::string name;
    bool master = false;
    LayerMode mode = LayerMode::Standard;
    int fxCount = 0;
    std::vector<ChannelRow> rows;
    int channels = 0;
};

struct FixtureType
{
    std::string name;
    std::string shortName;
    std::string id;
    std::string description;
    bool master = false;
    std::vector<FixtureMode> modes;
};

std::vector<FixtureType> fixtureTypes(const Config& cfg);

std::string gdtfDescription(const FixtureType& type, const std::string& isoDate);
std::vector<std::uint8_t> gdtfArchive(const FixtureType& type, const std::string& isoDate);
std::string gdtfFileName(const FixtureType& type);

std::string csvChannels(const std::vector<FixtureType>& types);
std::string csvPatch(const Config& cfg, const std::function<std::string(int)>& layerName);
std::string htmlChannelTable(const std::vector<FixtureType>& types, const Config& cfg, const std::function<std::string(int)>& layerName);

struct ExportResult
{
    bool ok = false;
    std::string error;
    std::filesystem::path folder;
    std::vector<std::filesystem::path> files;
};
ExportResult exportProfiles(const std::filesystem::path& parentFolder, const Config& cfg,
                            const std::function<std::string(int)>& layerName);

struct ZipFile
{
    std::string name;
    std::vector<std::uint8_t> data;
};
std::vector<std::uint8_t> zipStore(const std::vector<ZipFile>& files);
bool zipRead(const std::vector<std::uint8_t>& bytes, std::vector<ZipFile>& files, std::string* error);
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);
bool xmlWellFormed(const std::string& xml, std::string* error);

}
