#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bsl::plugin {

struct CheckOptions
{
    int width = 640;
    int height = 360;
    double time = 0.0;
    int fps = 30;
    bool audio = false;
    std::filesystem::path inputImage;
    std::vector<std::pair<std::string, std::string>> sets;
    std::filesystem::path pngOut;
    bool keepPixels = false;
};

struct CheckError
{
    int line = 0;
    std::string message;
    std::string sourceLine;
};

struct ParamInfo
{
    std::string name;
    std::string label;
    std::string type;
    std::string def;
    std::string range;
};

struct CheckStats
{
    double meanR = 0, meanG = 0, meanB = 0, meanA = 0;
    double litFraction = 0;
    int maxChannel = 0;
    double msPerFrame = 0;
    double msAt1080p = 0;
};

struct CheckResult
{
    bool parsed = false;
    bool compiled = false;
    bool rendered = false;
    bool blank = false;
    std::string name;
    std::string kind;
    std::string description;
    std::string credit;
    std::vector<std::string> categories;
    int passes = 0;
    std::vector<ParamInfo> params;
    std::vector<std::string> imported;
    std::vector<CheckError> errors;
    std::string rawError;
    std::vector<std::string> warnings;
    CheckStats stats;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;

    bool ok() const { return parsed && compiled && rendered && errors.empty(); }
    std::string toText(const std::string& fileLabel) const;
    std::string toJson(const std::string& fileLabel) const;
};

CheckResult checkIsfSource(const std::string& fragment, const std::string& vertex, const std::string& name,
                           const std::filesystem::path& baseDir, const CheckOptions& options);
CheckResult checkIsfFile(const std::filesystem::path& file, const CheckOptions& options);

}
