#pragma once

#include <glad/gl.h>

namespace bsl {

// Framebuffer com uma textura RGBA8. Lanca std::runtime_error se incompleto.
class Fbo
{
public:
    Fbo(int width, int height);
    ~Fbo();
    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;
    Fbo(Fbo&& other) noexcept;
    Fbo& operator=(Fbo&& other) noexcept;

    void bind() const;
    static void unbind();

    GLuint texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();

    GLuint fbo_ = 0;
    GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace bsl
