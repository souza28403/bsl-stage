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
    std::vector<std::uint8_t> pixels; // RGBA8, alpha reta, linhas de cima para baixo
    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

// Le um PNG/JPG (stb_image) como RGBA8. Imagem vazia se falhar.
RgbaImage loadImageFile(const std::filesystem::path& path);

// Reducao simples por media de area (ponderada pelo alpha). Se o destino for maior, usa vizinho mais proximo.
RgbaImage resizeImage(const RgbaImage& src, int newWidth, int newHeight);

// Carrega a imagem para uma textura GL (com mipmaps). Texture vazia se falhar.
Texture loadTexture(const std::filesystem::path& path);

} // namespace bsl
