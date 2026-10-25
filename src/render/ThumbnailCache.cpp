#include "render/ThumbnailCache.h"

#include "media/YuvConverter.h"

namespace bsl {

void ThumbnailCache::pump()
{
    // Poucos uploads por frame para nao engasgar a UI
    for (int i = 0; i < 4; ++i) {
        auto thumb = thumbnailer_.poll();
        if (!thumb)
            break;
        Entry& e = entries_[thumb->path];
        e.done = true;
        if (thumb->ok && !thumb->frame.empty()) {
            GLuint tex = 0;
            int w = 0, h = 0;
            media::uploadRgbaTexture(tex, w, h, thumb->frame);
            e.texture = Texture(tex, w, h);
        }
    }
}

GLuint ThumbnailCache::get(const std::filesystem::path& path)
{
    auto it = entries_.find(path);
    if (it == entries_.end()) {
        entries_.emplace(path, Entry{});
        thumbnailer_.request(path);
        return 0;
    }
    return it->second.texture.id;
}

} // namespace bsl
