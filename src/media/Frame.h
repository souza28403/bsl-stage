#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bsl::media {

enum class PixelFormat {
    RGBA8,
    YUV420P,
    YUVA420P,
    DXT1,
    DXT5,
    DXT5_YCoCg
};

enum class ColorSpace { BT601, BT709 };
enum class ColorRange { Limited, Full };

struct Frame {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    std::array<std::vector<std::uint8_t>, 4> planes;
    std::array<int, 4> strides = {0, 0, 0, 0};
    double pts = 0.0;
    ColorSpace colorSpace = ColorSpace::BT709;
    ColorRange range = ColorRange::Limited;

    static int planeCount(PixelFormat f)
    {
        switch (f) {
        case PixelFormat::YUV420P: return 3;
        case PixelFormat::YUVA420P: return 4;
        default: return 1;
        }
    }
    static bool isCompressedFormat(PixelFormat f)
    {
        return f == PixelFormat::DXT1 || f == PixelFormat::DXT5 || f == PixelFormat::DXT5_YCoCg;
    }
    bool isYuv() const { return format == PixelFormat::YUV420P || format == PixelFormat::YUVA420P; }
    bool isCompressed() const { return isCompressedFormat(format); }
    bool hasAlphaPlane() const { return format == PixelFormat::YUVA420P; }
    bool empty() const { return width <= 0 || height <= 0 || planes[0].empty(); }

    int planeWidth(int i) const { return (isYuv() && (i == 1 || i == 2)) ? (width + 1) / 2 : width; }
    int planeHeight(int i) const { return (isYuv() && (i == 1 || i == 2)) ? (height + 1) / 2 : height; }

    void allocate(int w, int h, PixelFormat f)
    {
        width = w;
        height = h;
        format = f;
        if (isCompressedFormat(f)) {
            const int blockBytes = f == PixelFormat::DXT1 ? 8 : 16;
            strides = {((w + 3) / 4) * blockBytes, 0, 0, 0};
            planes[0].resize(static_cast<size_t>(strides[0]) * static_cast<size_t>((h + 3) / 4));
            for (int i = 1; i < 4; ++i)
                planes[static_cast<size_t>(i)].clear();
            return;
        }
        for (int i = 0; i < 4; ++i) {
            if (i < planeCount(f)) {
                const int bpp = (f == PixelFormat::RGBA8) ? 4 : 1;
                strides[static_cast<size_t>(i)] = planeWidth(i) * bpp;
                planes[static_cast<size_t>(i)].resize(static_cast<size_t>(strides[static_cast<size_t>(i)]) *
                                                      static_cast<size_t>(planeHeight(i)));
            } else {
                strides[static_cast<size_t>(i)] = 0;
                planes[static_cast<size_t>(i)].clear();
            }
        }
    }
};

}
