#pragma once

#include "media/Thumbnailer.h"
#include "render/Texture.h"

#include <filesystem>
#include <map>

namespace bsl {

// Miniaturas dos clips: a thread de fundo decodifica, o upload para GL acontece aqui (main thread).
class ThumbnailCache
{
public:
    ThumbnailCache() = default;

    // Chamar uma vez por frame: sobe as miniaturas prontas para a GPU.
    void pump();

    // Textura da miniatura (160x90 RGBA) ou 0 se ainda nao chegou/falhou. Pede a geracao na primeira vez.
    GLuint get(const std::filesystem::path& path);

private:
    struct Entry
    {
        Texture texture;
        bool done = false;
    };

    media::Thumbnailer thumbnailer_;
    std::map<std::filesystem::path, Entry> entries_;
};

} // namespace bsl
