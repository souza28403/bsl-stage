#pragma once

#include "media/Frame.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::media {

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<Frame> frames;
    std::vector<double> delays;
    bool hasAlpha = false;
    std::string decoder;

    bool animated() const { return frames.size() > 1; }
    double totalDuration() const
    {
        double d = 0.0;
        for (double x : delays)
            d += x;
        return d;
    }
};

bool decodeImage(const std::filesystem::path& path, DecodedImage& out, std::string* error = nullptr);

}
