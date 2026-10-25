#include "media/Thumbnailer.h"

#include "media/DocThumb.h"
#include "media/FrameConverter.h"
#include "media/ImageDecoder.h"
#include "media/MediaFormats.h"
#include "media/VideoDecoder.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace bsl::media {

namespace {

// Escala 'src' (RGBA ou YUV420P/YUVA420P) para caber em dstW x dstH, centralizado, em RGBA.
bool fitToRgba(const Frame& src, int dstW, int dstH, Frame& dst)
{
    if (src.empty())
        return false;
    const double s = std::min(static_cast<double>(dstW) / src.width, static_cast<double>(dstH) / src.height);
    const int w = std::clamp(static_cast<int>(std::lround(src.width * s)), 1, dstW);
    const int h = std::clamp(static_cast<int>(std::lround(src.height * s)), 1, dstH);

    AVPixelFormat sf = AV_PIX_FMT_RGBA;
    if (src.format == PixelFormat::YUV420P)
        sf = AV_PIX_FMT_YUV420P;
    else if (src.format == PixelFormat::YUVA420P)
        sf = AV_PIX_FMT_YUVA420P;

    SwsContext* sws = sws_getContext(src.width, src.height, sf, w, h, AV_PIX_FMT_RGBA, SWS_AREA, nullptr, nullptr, nullptr);
    if (!sws)
        return false;
    if (src.isYuv()) {
        const int cs = src.colorSpace == ColorSpace::BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        sws_setColorspaceDetails(sws, sws_getCoefficients(cs), src.range == ColorRange::Full ? 1 : 0,
                                 sws_getCoefficients(cs), 1, 0, 1 << 16, 1 << 16);
    }

    dst.allocate(dstW, dstH, PixelFormat::RGBA8);
    std::fill(dst.planes[0].begin(), dst.planes[0].end(), static_cast<std::uint8_t>(0));
    const int x0 = (dstW - w) / 2;
    const int y0 = (dstH - h) / 2;

    const uint8_t* srcData[4] = {nullptr, nullptr, nullptr, nullptr};
    int srcStride[4] = {0, 0, 0, 0};
    for (int i = 0; i < Frame::planeCount(src.format); ++i) {
        srcData[i] = src.planes[static_cast<size_t>(i)].data();
        srcStride[i] = src.strides[static_cast<size_t>(i)];
    }
    uint8_t* dstData[4] = {dst.planes[0].data() + static_cast<size_t>(y0) * static_cast<size_t>(dst.strides[0]) +
                               static_cast<size_t>(x0) * 4,
                           nullptr, nullptr, nullptr};
    int dstStride[4] = {dst.strides[0], 0, 0, 0};
    const int rows = sws_scale(sws, srcData, srcStride, 0, src.height, dstData, dstStride);
    sws_freeContext(sws);
    dst.colorSpace = ColorSpace::BT709;
    dst.range = ColorRange::Full;
    return rows > 0;
}

} // namespace

Thumbnailer::Thumbnailer(int width, int height) : width_(width), height_(height)
{
    initFfmpegLogging();
    thread_ = std::thread([this] { run(false); });
    docThread_ = std::thread([this] { run(true); });
}

Thumbnailer::~Thumbnailer()
{
    cancelDocThumbnails();
    {
        std::lock_guard lk(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    if (docThread_.joinable())
        docThread_.join();
}

void Thumbnailer::request(const std::filesystem::path& path)
{
    {
        std::lock_guard lk(m_);
        if (!queued_.insert(path).second)
            return;
        if (isPagedKind(classify(path)))
            docRequests_.push_back(path);
        else
            requests_.push_back(path);
    }
    cv_.notify_all();
}

std::optional<Thumbnail> Thumbnailer::poll()
{
    std::lock_guard lk(m_);
    if (results_.empty())
        return std::nullopt;
    Thumbnail t = std::move(results_.front());
    results_.pop_front();
    return t;
}

size_t Thumbnailer::pending() const
{
    std::lock_guard lk(m_);
    return requests_.size() + docRequests_.size() + inFlight_ + results_.size();
}

void Thumbnailer::run(bool docs)
{
    while (true) {
        std::filesystem::path path;
        {
            std::unique_lock lk(m_);
            auto& queue = docs ? docRequests_ : requests_;
            cv_.wait(lk, [&] { return stop_ || !queue.empty(); });
            if (stop_)
                return;
            path = std::move(queue.front());
            queue.pop_front();
            ++inFlight_;
        }
        Thumbnail t;
        try {
            t = generate(path);
        } catch (...) {
            t = Thumbnail{};
        }
        t.path = path;
        {
            std::lock_guard lk(m_);
            --inFlight_;
            queued_.erase(path);
            results_.push_back(std::move(t));
        }
    }
}

Thumbnail Thumbnailer::generate(const std::filesystem::path& path)
{
    Thumbnail out;
    out.path = path;
    switch (classify(path)) {
    case MediaKind::Video: {
        VideoDecoder dec;
        VideoDecoderOptions opt;
        opt.lowPower = true; // miniatura nao precisa de loop filter nem de resolucao cheia
        opt.targetWidth = width_ * 2;
        opt.targetHeight = height_ * 2;
        opt.queueSize = 2;
        if (!dec.open(path, opt))
            return out;
        const double dur = dec.info().duration;
        if (dur > 0.0)
            dec.seek(dur * 0.10);
        auto f = dec.nextFrame(8000);
        if (!f)
            return out;
        out.ok = fitToRgba(*f, width_, height_, out.frame);
        break;
    }
    case MediaKind::Image: {
        DecodedImage img;
        if (!decodeImage(path, img) || img.frames.empty())
            return out;
        out.ok = fitToRgba(img.frames.front(), width_, height_, out.frame);
        break;
    }
    case MediaKind::Document:
    case MediaKind::Office:
        out.ok = renderDocThumbnail(path, width_, height_, out.frame);
        break;
    case MediaKind::Web: // sem miniatura: a interface desenha uma etiqueta
    case MediaKind::Unknown:
        break;
    }
    if (!out.ok)
        out.frame = Frame{};
    return out;
}

} // namespace bsl::media
