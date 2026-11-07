#include "fx/bsl/BslPreview.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace bsl::fx::bsllight {

bool PreviewCapture::wanted()
{
    const std::int64_t now = nowMs();
    if (now - lastRequest_.load() > 5000)
        return false;
    return now - lastCapture_.load() >= 100;
}

void PreviewCapture::reset()
{
    std::lock_guard lk(mutex_);
    seq_.store(0);
    rgb_.clear();
    jpeg_.clear();
    encodedSeq_ = 0;
    small_.reset();
}

void PreviewCapture::capture(GLuint tex, int w, int h, bool topOrigin)
{
    if (tex == 0 || w <= 0 || h <= 0)
        return;
    lastCapture_.store(nowMs());
    const int tw = std::min(640, w);
    const int th = std::max(1, static_cast<int>(std::lround(static_cast<double>(tw) * h / w)));
    try {
        if (!small_ || small_->width() != tw || small_->height() != th)
            small_ = std::make_unique<Fbo>(tw, th);
    } catch (const std::exception&) {
        return;
    }
    blit_.copy(tex, w, h, *small_, true, !topOrigin);
    std::vector<unsigned char> rgba(static_cast<size_t>(tw) * static_cast<size_t>(th) * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, small_->id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::vector<unsigned char> rgb(static_cast<size_t>(tw) * static_cast<size_t>(th) * 3);
    for (size_t i = 0, n = static_cast<size_t>(tw) * static_cast<size_t>(th); i < n; ++i) {
        const unsigned a = rgba[i * 4 + 3];
        for (size_t c = 0; c < 3; ++c)
            rgb[i * 3 + c] = static_cast<unsigned char>((rgba[i * 4 + c] * a + 32u * (255u - a)) / 255u);
    }
    std::lock_guard lk(mutex_);
    rgb_ = std::move(rgb);
    w_ = tw;
    h_ = th;
    seq_.fetch_add(1);
}

bool PreviewCapture::jpeg(std::string& out, int waitMs)
{
    touch();
    if (!hasFrame() || nowMs() - lastCapture_.load() > 1500) {
        const std::uint64_t before = seq_.load();
        for (int waited = 0; waited < waitMs && seq_.load() == before; waited += 20)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::lock_guard lk(mutex_);
    if (rgb_.empty() || seq_.load() == 0)
        return false;
    if (encodedSeq_ != seq_.load()) {
        jpeg_.clear();
        auto sink = [](void* ctx, void* data, int size) {
            static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), static_cast<size_t>(size));
        };
        if (!stbi_write_jpg_to_func(sink, &jpeg_, w_, h_, 3, rgb_.data(), 70))
            return false;
        encodedSeq_ = seq_.load();
    }
    out = jpeg_;
    return !out.empty();
}

}
