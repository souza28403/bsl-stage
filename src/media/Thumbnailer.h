#pragma once

#include "media/Frame.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace bsl::media {

struct Thumbnail {
    std::filesystem::path path;
    Frame frame;
    bool ok = false;
};

class Thumbnailer {
public:
    explicit Thumbnailer(int width = 160, int height = 90);
    ~Thumbnailer();
    Thumbnailer(const Thumbnailer&) = delete;
    Thumbnailer& operator=(const Thumbnailer&) = delete;

    void request(const std::filesystem::path& path);

    std::optional<Thumbnail> poll();

    size_t pending() const;

private:
    void run(bool docs);
    Thumbnail generate(const std::filesystem::path& path);

    int width_;
    int height_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::filesystem::path> requests_;
    std::deque<std::filesystem::path> docRequests_;
    std::set<std::filesystem::path> queued_;
    std::deque<Thumbnail> results_;
    size_t inFlight_ = 0;
    bool stop_ = false;
    std::thread thread_;
    std::thread docThread_;
};

}
