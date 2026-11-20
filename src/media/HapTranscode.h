#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace bsl::media {

struct HapJob {
    std::filesystem::path source;
    std::filesystem::path target;
    int maxWidth = 0;
    int maxHeight = 0;
    bool light = false;
};

struct HapResult {
    bool ok = false;
    std::string error;
    int width = 0;
    int height = 0;
    bool alpha = false;
    std::int64_t frames = 0;
    double fps = 0;
};

HapResult transcodeToHap(const HapJob& job, const std::function<void(double)>& progress,
                         const std::atomic<bool>& cancel);

}
