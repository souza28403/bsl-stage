#pragma once

// Uso interno: ponte entre AVFrame (FFmpeg) e media::Frame.

#include "media/Frame.h"

#include <cstdint>

struct AVFrame;
struct SwsContext;

namespace bsl::media {

// Reduz o nivel de log do FFmpeg (uma vez por processo).
void initFfmpegLogging();

// Converte AVFrame -> Frame, com cache do contexto swscale. Nao e thread-safe;
// use uma instancia por thread.
class FrameConverter {
public:
    FrameConverter() = default;
    ~FrameConverter();
    FrameConverter(const FrameConverter&) = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;

    // outW/outH <= 0 mantem o tamanho. forceRgba converte sempre para RGBA8.
    // Formatos yuv420p/yuvj420p/yuva420p sem escala e sem forceRgba sao copiados direto.
    // scaleFlags: SWS_* (0 = bilinear).
    bool convert(const AVFrame* src, int outW, int outH, bool forceRgba, Frame& dst, int scaleFlags = 0);

private:
    SwsContext* sws_ = nullptr;
    std::uint64_t detailsKey_ = 0;
};

// Classificacao do espaco de cor/range do AVFrame (heuristica 601 < 720p <= 709 se nao informado).
ColorSpace colorSpaceOf(const AVFrame* f);
ColorRange colorRangeOf(const AVFrame* f);

} // namespace bsl::media
