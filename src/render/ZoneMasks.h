#pragma once

#include <glad/gl.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace bsl {

struct ZoneMask
{
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    int zones = 3;
};

class ZoneMasks
{
public:
    static ZoneMasks& instance();

    void beginFrame();
    void publish(const void* owner, int source, const ZoneMask& mask);
    void retract(const void* owner);

    bool find(int source, ZoneMask& out) const;
    bool available(int source) const;

private:
    struct Entry
    {
        const void* owner = nullptr;
        int source = -1;
        ZoneMask mask;
        bool fresh = false;
        std::uint64_t seq = 0;
    };
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::uint64_t seq_ = 0;
};

}
