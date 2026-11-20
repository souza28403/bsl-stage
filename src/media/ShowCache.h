#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

namespace bsl::media {

enum class ShowProfile { HapQ, HapAlpha };

struct ShowCacheInfo {
    std::uintmax_t sourceSize = 0;
    std::int64_t sourceMtime = 0;
    int width = 0;
    int height = 0;
    bool alpha = false;
    std::int64_t frames = 0;
    double fps = 0;
};

std::string showCacheKey(const std::filesystem::path& source, int maxWidth, int maxHeight);
std::filesystem::path showCacheVideo(const std::filesystem::path& dir, const std::string& key);
std::filesystem::path showCacheSidecar(const std::filesystem::path& dir, const std::string& key);
bool writeShowCacheSidecar(const std::filesystem::path& dir, const std::string& key,
                           const std::filesystem::path& source, const ShowCacheInfo& info);
std::optional<std::filesystem::path> lookupShowCache(const std::filesystem::path& dir,
                                                     const std::filesystem::path& source,
                                                     int maxWidth, int maxHeight,
                                                     ShowCacheInfo* info = nullptr);
std::optional<std::filesystem::path> lookupShowCacheBySource(const std::filesystem::path& dir,
                                                             const std::filesystem::path& source,
                                                             int maxWidth, int maxHeight,
                                                             ShowCacheInfo* info = nullptr);
std::uintmax_t estimateShowCacheBytes(int width, int height, std::int64_t frames, bool alpha);
std::uintmax_t clearShowCache(const std::filesystem::path& dir, const std::set<std::string>& keepKeys);

}
