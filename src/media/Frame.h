#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace bsl::media {

enum class PixelFormat {
    RGBA8,
    YUV420P,
    YUVA420P
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
        return f == PixelFormat::RGBA8 ? 1 : (f == PixelFormat::YUV420P ? 3 : 4);
    }
    bool isYuv() const { return format != PixelFormat::RGBA8; }
    bool hasAlphaPlane() const { return format == PixelFormat::YUVA420P; }
    bool empty() const { return width <= 0 || height <= 0 || planes[0].empty(); }

    int planeWidth(int i) const { return (isYuv() && (i == 1 || i == 2)) ? (width + 1) / 2 : width; }
    int planeHeight(int i) const { return (isYuv() && (i == 1 || i == 2)) ? (height + 1) / 2 : height; }

    void allocate(int w, int h, PixelFormat f)
    {
        width = w;
        height = h;
        format = f;
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
