#pragma once

#include "media/Thumbnailer.h"
#include "render/Texture.h"

#include <filesystem>
#include <map>

namespace bsl {

class ThumbnailCache
{
public:
    ThumbnailCache() = default;

    void pump();

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

}
