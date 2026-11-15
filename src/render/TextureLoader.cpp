#include "render/TextureLoader.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdio>

namespace bsl {

RgbaImage loadImageFile(const std::filesystem::path& path)
{
    RgbaImage out;
    FILE* file = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        return out;
#else
    file = std::fopen(path.c_str(), "rb");
    if (!file)
        return out;
#endif

    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_file(file, &w, &h, &n, 4);
    std::fclose(file);
    if (!data)
        return out;

    out.width = w;
    out.height = h;
    out.pixels.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    return out;
}

RgbaImage resizeImage(const RgbaImage& src, int newWidth, int newHeight)
{
    RgbaImage out;
    if (src.empty() || newWidth <= 0 || newHeight <= 0)
        return out;
    out.width = newWidth;
    out.height = newHeight;
    out.pixels.assign(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight) * 4, 0);

    for (int y = 0; y < newHeight; ++y) {
        const int y0 = y * src.height / newHeight;
        const int y1 = std::max(y0 + 1, (y + 1) * src.height / newHeight);
        for (int x = 0; x < newWidth; ++x) {
            const int x0 = x * src.width / newWidth;
            const int x1 = std::max(x0 + 1, (x + 1) * src.width / newWidth);
            double r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            for (int sy = y0; sy < y1 && sy < src.height; ++sy) {
                for (int sx = x0; sx < x1 && sx < src.width; ++sx) {
                    const std::uint8_t* p =
                        &src.pixels[(static_cast<size_t>(sy) * static_cast<size_t>(src.width) +
                                     static_cast<size_t>(sx)) * 4];
                    const double pa = p[3];
                    r += p[0] * pa;
                    g += p[1] * pa;
                    b += p[2] * pa;
                    a += pa;
                    ++count;
                }
            }
            std::uint8_t* d = &out.pixels[(static_cast<size_t>(y) * static_cast<size_t>(newWidth) +
                                           static_cast<size_t>(x)) * 4];
            if (a > 0.0 && count > 0) {
                d[0] = static_cast<std::uint8_t>(r / a + 0.5);
                d[1] = static_cast<std::uint8_t>(g / a + 0.5);
                d[2] = static_cast<std::uint8_t>(b / a + 0.5);
                d[3] = static_cast<std::uint8_t>(a / count + 0.5);
            }
        }
    }
    return out;
}

Texture loadTexture(const std::filesystem::path& path)
{
    const RgbaImage img = loadImageFile(path);
    if (img.empty())
        return {};

    GLuint tex = 0;
    glGenTextures(1, &tex);
    GLint prevTex = 0, prevAlign = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 img.pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    return Texture(tex, img.width, img.height);
}

}
