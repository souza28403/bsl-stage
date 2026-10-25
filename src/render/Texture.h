#pragma once

#include <glad/gl.h>

#include <utility>

namespace bsl {

// Textura GL com dono unico (RAII). Precisa do contexto GL vivo ao ser destruida.
struct Texture
{
    GLuint id = 0;
    int width = 0;
    int height = 0;

    Texture() = default;
    Texture(GLuint i, int w, int h) : id(i), width(w), height(h) {}
    ~Texture() { reset(); }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept
        : id(std::exchange(o.id, 0)), width(o.width), height(o.height)
    {
    }
    Texture& operator=(Texture&& o) noexcept
    {
        if (this != &o) {
            reset();
            id = std::exchange(o.id, 0);
            width = o.width;
            height = o.height;
        }
        return *this;
    }

    void reset()
    {
        if (id)
            glDeleteTextures(1, &id);
        id = 0;
    }
    explicit operator bool() const { return id != 0; }
};

} // namespace bsl
