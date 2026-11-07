#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bsl::ffgl {

using Json = nlohmann::json;

inline constexpr unsigned kTypeEffect = 0;
inline constexpr unsigned kTypeSource = 1;
inline constexpr unsigned kTypeMixer = 2;

inline constexpr unsigned kPBoolean = 0;
inline constexpr unsigned kPEvent = 1;
inline constexpr unsigned kPRed = 2;
inline constexpr unsigned kPGreen = 3;
inline constexpr unsigned kPBlue = 4;
inline constexpr unsigned kPXPos = 5;
inline constexpr unsigned kPYPos = 6;
inline constexpr unsigned kPStandard = 10;
inline constexpr unsigned kPOption = 11;
inline constexpr unsigned kPBuffer = 12;
inline constexpr unsigned kPInteger = 13;
inline constexpr unsigned kPFile = 14;
inline constexpr unsigned kPText = 100;
inline constexpr unsigned kPHue = 200;
inline constexpr unsigned kPSaturation = 201;
inline constexpr unsigned kPBrightness = 202;
inline constexpr unsigned kPAlpha = 203;

struct OptionInfo
{
    std::string name;
    float value = 0.0f;
};

struct ParamInfo
{
    int index = 0;
    std::string name;
    unsigned type = kPStandard;
    float defFloat = 0.0f;
    std::string defText;
    float min = 0.0f;
    float max = 1.0f;
    std::string group;
    unsigned usage = 0;
    bool visible = true;
    std::vector<OptionInfo> options;
    std::vector<std::string> extensions;
};

struct PluginInfo
{
    std::string path;
    std::string fileName;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;

    std::string pluginId;
    std::string name;
    unsigned type = kTypeEffect;
    unsigned apiMajor = 0, apiMinor = 0;
    unsigned versionMajor = 0, versionMinor = 0;
    std::string description;
    std::string about;
    bool supportsTime = false;
    bool supportsTopLeft = false;
    int minInputs = -1, maxInputs = -1;
    std::vector<ParamInfo> params;

    Json toJson() const;
    static std::optional<PluginInfo> fromJson(const Json& j);
};

struct ScanResult
{
    bool ok = false;
    std::string error;
    PluginInfo info;

    Json toJson() const;
    static ScanResult fromJson(const Json& j);
};

std::string makeTypeId(const std::string& pluginId, const std::string& fileName);
bool splitTypeId(const std::string& typeId, std::string& pluginId, std::string& fileName);

}
