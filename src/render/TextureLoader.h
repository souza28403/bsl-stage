#pragma once

#include "render/Texture.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace bsl {

struct RgbaImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

RgbaImage loadImageFile(const std::filesystem::path& path);

RgbaImage resizeImage(const RgbaImage& src, int newWidth, int newHeight);

Texture loadTexture(const std::filesystem::path& path);

}
