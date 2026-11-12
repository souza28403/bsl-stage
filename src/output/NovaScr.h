#pragma once

#include "output/PixelMap.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bsl::novascr {

inline constexpr int kMaxCabinets = 4096;

struct Cabinet
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int port = 0;
    int order = 0;
    int col = -1;
    int row = -1;

    bool operator==(const Cabinet&) const = default;
};

struct Result
{
    bool ok = false;
    std::string error;
    int version = 0;
    int cols = 0;
    int rows = 0;
    int width = 0;
    int height = 0;
    bool hasPorts = false;
    std::vector<Cabinet> cabinets;
    std::vector<std::string> warnings;
};

Result parse(const std::uint8_t* data, std::size_t size);
Result parse(const std::vector<std::uint8_t>& bytes);
Result parseFile(const std::string& utf8Path);

int portCount(const Result& r);

enum class ImportMode { Replace, AddBeside };

bool applyToPixelMap(const Result& r, pixelmap::Config& cfg, ImportMode mode, std::string* error = nullptr);

}
