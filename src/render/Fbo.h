#pragma once

#include <glad/gl.h>

namespace bsl {

// Framebuffer com uma textura RGBA8 (ou RGBA32F com 'floatFormat', para os buffers FLOAT do ISF).
// Lanca std::runtime_error se incompleto.
class Fbo
{
public:
    Fbo(int width, int height, bool floatFormat = false);
    ~Fbo();
    Fbo(const Fbo&) = delete;
    Fbo& operator=(const Fbo&) = delete;
    Fbo(Fbo&& other) noexcept;
    Fbo& operator=(Fbo&& other) noexcept;

    void bind() const;
    static void unbind();

    GLuint texture() const { return texture_; }
    GLuint id() const { return fbo_; }
    bool isFloat() const { return float_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();

    GLuint fbo_ = 0;
    GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool float_ = false;
};

} // namespace bsl
