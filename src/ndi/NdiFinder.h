#pragma once

#include "ndi/NdiRuntime.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bsl::ndi {

struct SourceInfo
{
    std::string name;
    std::string url;
    bool operator==(const SourceInfo&) const = default;
};

class Finder
{
public:
    explicit Finder(Runtime& runtime);
    ~Finder();
    Finder(const Finder&) = delete;
    Finder& operator=(const Finder&) = delete;

    static Finder& shared();
    static void shutdownShared();

    void start();
    void stop();
    bool running() const { return running_.load(); }

    std::vector<SourceInfo> sources() const;
    unsigned long long generation() const { return generation_.load(); }

private:
    void loop();

    Runtime& rt_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<unsigned long long> generation_{0};
    mutable std::mutex mutex_;
    std::vector<SourceInfo> sources_;
};

}
