#include "media/FrameConverter.h"

#include <algorithm>
#include <cstring>
#include <mutex>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace bsl::media {

void initFfmpegLogging()
{
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_ERROR); });
}

ColorSpace colorSpaceOf(const AVFrame* f)
{
    switch (f->colorspace) {
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return ColorSpace::BT709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_SMPTE240M:
        return ColorSpace::BT601;
    default:
        return (f->height >= 720 || f->width >= 1280) ? ColorSpace::BT709 : ColorSpace::BT601;
    }
}

ColorRange colorRangeOf(const AVFrame* f)
{
    if (f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P)
        return ColorRange::Full;
    return ColorRange::Limited;
}

FrameConverter::~FrameConverter()
{
    if (sws_)
        sws_freeContext(sws_);
}

bool FrameConverter::convert(const AVFrame* src, int outW, int outH, bool forceRgba, Frame& dst, int scaleFlags)
{
    if (!src || src->width <= 0 || src->height <= 0)
        return false;
    if (outW <= 0 || outH <= 0) {
        outW = src->width;
        outH = src->height;
    }
    const AVPixelFormat sf = static_cast<AVPixelFormat>(src->format);
    const bool yuvDirect = sf == AV_PIX_FMT_YUV420P || sf == AV_PIX_FMT_YUVJ420P || sf == AV_PIX_FMT_YUVA420P;
    const bool wantYuv = yuvDirect && !forceRgba;
    const bool sameSize = outW == src->width && outH == src->height;

    dst.colorSpace = colorSpaceOf(src);
    dst.range = colorRangeOf(src);

    if (wantYuv && sameSize) {
        const PixelFormat pf = sf == AV_PIX_FMT_YUVA420P ? PixelFormat::YUVA420P : PixelFormat::YUV420P;
        dst.allocate(src->width, src->height, pf);
        for (int p = 0; p < Frame::planeCount(pf); ++p) {
            const size_t ps = static_cast<size_t>(p);
            const int rowBytes = dst.planeWidth(p);
            const int rows = dst.planeHeight(p);
            for (int y = 0; y < rows; ++y) {
                std::memcpy(dst.planes[ps].data() + static_cast<size_t>(y) * static_cast<size_t>(dst.strides[ps]),
                            src->data[p] + static_cast<ptrdiff_t>(y) * src->linesize[p],
                            static_cast<size_t>(rowBytes));
            }
        }
        return true;
    }

    AVPixelFormat df = AV_PIX_FMT_RGBA;
    PixelFormat pf = PixelFormat::RGBA8;
    if (wantYuv) {
        df = sf == AV_PIX_FMT_YUVA420P ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
        pf = sf == AV_PIX_FMT_YUVA420P ? PixelFormat::YUVA420P : PixelFormat::YUV420P;
    }
    if (pf != PixelFormat::RGBA8 && (outW & 1 || outH & 1)) {
        outW &= ~1;
        outH &= ~1;
        outW = std::max(outW, 2);
        outH = std::max(outH, 2);
    }

    SwsContext* prev = sws_;
    sws_ = sws_getCachedContext(sws_, src->width, src->height, sf, outW, outH, df,
                                scaleFlags ? scaleFlags : SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_)
        return false;
    const std::uint64_t key = (static_cast<std::uint64_t>(colorSpaceOf(src)) << 1) |
                              static_cast<std::uint64_t>(colorRangeOf(src)) |
                              (static_cast<std::uint64_t>(sf) << 8) | (static_cast<std::uint64_t>(df) << 24) |
                              (static_cast<std::uint64_t>(outW) << 32);
    const bool detailsChanged = sws_ != prev || key != detailsKey_;
    detailsKey_ = key;

    // Informa matriz/range de origem; destino RGB e sempre full range.
    if (detailsChanged && (!av_pix_fmt_desc_get(sf) || !(av_pix_fmt_desc_get(sf)->flags & AV_PIX_FMT_FLAG_RGB))) {
        int cs = SWS_CS_DEFAULT;
        switch (colorSpaceOf(src)) {
        case ColorSpace::BT709: cs = SWS_CS_ITU709; break;
        case ColorSpace::BT601: cs = SWS_CS_ITU601; break;
        }
        const int srcRange = colorRangeOf(src) == ColorRange::Full ? 1 : 0;
        const int dstRange = (pf == PixelFormat::RGBA8) ? 1 : srcRange;
        sws_setColorspaceDetails(sws_, sws_getCoefficients(cs), srcRange, sws_getCoefficients(cs), dstRange,
                                 0, 1 << 16, 1 << 16);
    }

    dst.allocate(outW, outH, pf);
    uint8_t* dstData[4] = {nullptr, nullptr, nullptr, nullptr};
    int dstStride[4] = {0, 0, 0, 0};
    for (int p = 0; p < Frame::planeCount(pf); ++p) {
        dstData[p] = dst.planes[static_cast<size_t>(p)].data();
        dstStride[p] = dst.strides[static_cast<size_t>(p)];
    }
    const int h = sws_scale(sws_, src->data, src->linesize, 0, src->height, dstData, dstStride);
    return h > 0;
}

} // namespace bsl::media
