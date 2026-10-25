#pragma once

#include "media/Frame.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::media {

// Resultado da decodificacao de uma imagem: 1 frame (estatica) ou varios (GIF/WEBP animado).
struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<Frame> frames;   // todos RGBA8 do mesmo tamanho (alpha reta)
    std::vector<double> delays;  // segundos de exibicao de cada frame (mesmo tamanho de frames)
    bool hasAlpha = false;       // algum pixel com alpha < 255
    std::string decoder;         // "stb" ou "ffmpeg"

    bool animated() const { return frames.size() > 1; }
    double totalDuration() const
    {
        double d = 0.0;
        for (double x : delays)
            d += x;
        return d;
    }
};

// Decodifica PNG/JPG/BMP/TGA/PSD/HDR/GIF via stb; WEBP/TIFF e o que o stb falhar via FFmpeg.
// Animacoes sao limitadas a ~768 MB de RGBA; excedente e truncado.
bool decodeImage(const std::filesystem::path& path, DecodedImage& out, std::string* error = nullptr);

} // namespace bsl::media
