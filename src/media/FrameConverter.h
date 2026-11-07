#pragma once

#include "media/Frame.h"

#include <cstdint>

struct AVFrame;
struct SwsContext;

namespace bsl::media {

void initFfmpegLogging();

class FrameConverter {
public:
    FrameConverter() = default;
    ~FrameConverter();
    FrameConverter(const FrameConverter&) = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;

    bool convert(const AVFrame* src, int outW, int outH, bool forceRgba, Frame& dst, int scaleFlags = 0);

private:
    SwsContext* sws_ = nullptr;
    std::uint64_t detailsKey_ = 0;
};

ColorSpace colorSpaceOf(const AVFrame* f);
ColorRange colorRangeOf(const AVFrame* f);

}
