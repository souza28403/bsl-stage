#pragma once

#include "fx/FxGl.h"
#include "render/Fbo.h"

#include <glad/gl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

class PreviewCapture
{
public:
    void touch() { lastRequest_.store(nowMs()); }
    bool wanted();
    void capture(GLuint tex, int w, int h, bool topOrigin);
    bool jpeg(std::string& out, int waitMs = 500);
    bool hasFrame() const { return seq_.load() != 0; }
    std::uint64_t sequence() const { return seq_.load(); }
    void reset();

private:
    static std::int64_t nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::atomic<std::int64_t> lastRequest_{-1000000};
    std::atomic<std::int64_t> lastCapture_{-1000000};
    std::atomic<std::uint64_t> seq_{0};

    std::unique_ptr<Fbo> small_;
    TexBlitter blit_;

    std::mutex mutex_;
    std::vector<unsigned char> rgb_;
    int w_ = 0, h_ = 0;
    std::uint64_t encodedSeq_ = 0;
    std::string jpeg_;
};

}
