#pragma once

#include "output/PixelMap.h"

#include <cstdint>
#include <mutex>

namespace bsl::fx::bsllight {

class PixelMapFeed
{
public:
    static PixelMapFeed& instance();

    void set(const pixelmap::Config& config);
    pixelmap::Config config() const;
    std::uint64_t version() const;

private:
    mutable std::mutex mutex_;
    pixelmap::Config config_;
    std::uint64_t version_ = 1;
};

}
