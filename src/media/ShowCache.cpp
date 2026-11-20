#include "media/ShowCache.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace bsl::media {

namespace fs = std::filesystem;

namespace {

constexpr std::int32_t kFormatVersion = 1;

void mix(std::uint64_t& h, const void* data, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

template <class T>
void mixValue(std::uint64_t& h, T v) {
    mix(h, &v, sizeof(v));
}

std::string normalizedPath(const fs::path& source) {
    std::error_code ec;
    fs::path abs = fs::absolute(source, ec);
    if (ec) abs = source;
    const auto u8 = abs.lexically_normal().generic_u8string();
    std::string s(u8.begin(), u8.end());
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

bool fileStat(const fs::path& p, std::uintmax_t& size, std::int64_t& mtime) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec) return false;
    size = fs::file_size(p, ec);
    if (ec) return false;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return false;
    mtime = static_cast<std::int64_t>(t.time_since_epoch().count());
    return true;
}

bool readSidecar(const fs::path& path, nlohmann::json& j) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    j = nlohmann::json::parse(f, nullptr, false);
    return j.is_object();
}

ShowCacheInfo infoFromJson(const nlohmann::json& j) {
    ShowCacheInfo i;
    i.sourceSize = j.value("sourceSize", std::uintmax_t{0});
    i.sourceMtime = j.value("sourceMtime", std::int64_t{0});
    i.width = j.value("width", 0);
    i.height = j.value("height", 0);
    i.alpha = j.value("alpha", false);
    i.frames = j.value("frames", std::int64_t{0});
    i.fps = j.value("fps", 0.0);
    return i;
}

bool videoUsable(const fs::path& mov) {
    std::error_code ec;
    if (!fs::is_regular_file(mov, ec) || ec) return false;
    const auto sz = fs::file_size(mov, ec);
    return !ec && sz > 0;
}

}

std::string showCacheKey(const fs::path& source, int maxWidth, int maxHeight) {
    try {
        std::uintmax_t size = 0;
        std::int64_t mtime = 0;
        if (!fileStat(source, size, mtime)) return {};
        std::uint64_t h = 14695981039346656037ULL;
        const std::string path = normalizedPath(source);
        mix(h, path.data(), path.size());
        mixValue<std::uint64_t>(h, size);
        mixValue<std::int64_t>(h, mtime);
        mixValue<std::int32_t>(h, maxWidth);
        mixValue<std::int32_t>(h, maxHeight);
        mixValue<std::int32_t>(h, kFormatVersion);
        static const char* digits = "0123456789abcdef";
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i) {
            out[static_cast<std::size_t>(i)] = digits[h & 0xF];
            h >>= 4;
        }
        return out;
    } catch (...) {
        return {};
    }
}

fs::path showCacheVideo(const fs::path& dir, const std::string& key) {
    return dir / (key + ".mov");
}

fs::path showCacheSidecar(const fs::path& dir, const std::string& key) {
    return dir / (key + ".json");
}

bool writeShowCacheSidecar(const fs::path& dir, const std::string& key, const fs::path& source,
                           const ShowCacheInfo& info) {
    try {
        if (key.empty()) return false;
        std::error_code ec;
        fs::create_directories(dir, ec);
        nlohmann::json j;
        j["source"] = normalizedPath(source);
        j["sourceSize"] = info.sourceSize;
        j["sourceMtime"] = info.sourceMtime;
        j["width"] = info.width;
        j["height"] = info.height;
        j["alpha"] = info.alpha;
        j["frames"] = info.frames;
        j["fps"] = info.fps;
        const fs::path finalPath = showCacheSidecar(dir, key);
        fs::path tmp = finalPath;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            const std::string text = j.dump(2);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
            f.flush();
            if (!f) {
                f.close();
                fs::remove(tmp, ec);
                return false;
            }
        }
        fs::rename(tmp, finalPath, ec);
        if (ec) {
            std::error_code ec2;
            fs::remove(finalPath, ec2);
            ec.clear();
            fs::rename(tmp, finalPath, ec);
            if (ec) {
                fs::remove(tmp, ec2);
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<fs::path> lookupShowCache(const fs::path& dir, const fs::path& source, int maxWidth,
                                        int maxHeight, ShowCacheInfo* info) {
    try {
        std::uintmax_t size = 0;
        std::int64_t mtime = 0;
        if (!fileStat(source, size, mtime)) return lookupShowCacheBySource(dir, source, maxWidth, maxHeight, info);
        const std::string key = showCacheKey(source, maxWidth, maxHeight);
        if (key.empty()) return std::nullopt;
        const fs::path mov = showCacheVideo(dir, key);
        if (!videoUsable(mov)) return std::nullopt;
        nlohmann::json j;
        if (!readSidecar(showCacheSidecar(dir, key), j)) return std::nullopt;
        const ShowCacheInfo got = infoFromJson(j);
        if (got.sourceSize != size || got.sourceMtime != mtime) return std::nullopt;
        if (info) *info = got;
        return mov;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<fs::path> lookupShowCacheBySource(const fs::path& dir, const fs::path& source, int maxWidth,
                                                int maxHeight, ShowCacheInfo* info) {
    try {
        (void)maxWidth;
        (void)maxHeight;
        const std::string want = normalizedPath(source);
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        if (ec) return std::nullopt;
        for (const fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::path p = it->path();
            if (p.extension() != ".json") continue;
            nlohmann::json j;
            if (!readSidecar(p, j)) continue;
            if (j.value("source", std::string()) != want) continue;
            const fs::path mov = showCacheVideo(dir, p.stem().string());
            if (!videoUsable(mov)) continue;
            if (info) *info = infoFromJson(j);
            return mov;
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::uintmax_t estimateShowCacheBytes(int width, int height, std::int64_t frames, bool alpha) {
    if (width <= 0 || height <= 0 || frames <= 0) return 0;
    const double bytesPerPixel = alpha ? 1.0 : 1.0;
    const double total = static_cast<double>(width) * height * static_cast<double>(frames) * bytesPerPixel * 1.10;
    return static_cast<std::uintmax_t>(total);
}

std::uintmax_t clearShowCache(const fs::path& dir, const std::set<std::string>& keepKeys) {
    std::uintmax_t freed = 0;
    try {
        std::error_code ec;
        if (!fs::is_directory(dir, ec) || ec) return 0;
        fs::directory_iterator it(dir, ec);
        if (ec) return 0;
        for (const fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::path p = it->path();
            std::error_code e;
            if (!it->is_regular_file(e) || e) continue;
            const std::string ext = p.extension().string();
            std::string stem = p.stem().string();
            if (ext == ".part" || ext == ".tmp") {
                const auto dot = stem.find('.');
                if (dot != std::string::npos) stem = stem.substr(0, dot);
            } else if (ext != ".mov" && ext != ".json") {
                continue;
            }
            if (keepKeys.count(stem)) continue;
            const auto sz = fs::file_size(p, e);
            e.clear();
            if (fs::remove(p, e) && !e) freed += sz;
        }
    } catch (...) {
    }
    return freed;
}

}
