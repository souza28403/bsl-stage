#include "render/Fbo.h"

#include <stdexcept>
#include <utility>

namespace bsl {

Fbo::Fbo(int width, int height, bool floatFormat) : width_(width), height_(height), float_(floatFormat)
{
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    if (floatFormat)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        release();
        throw std::runtime_error("FBO incompleto");
    }
}

Fbo::~Fbo()
{
    release();
}

void Fbo::release()
{
    if (fbo_)
        glDeleteFramebuffers(1, &fbo_);
    if (texture_)
        glDeleteTextures(1, &texture_);
    fbo_ = texture_ = 0;
}

Fbo::Fbo(Fbo&& other) noexcept
    : fbo_(std::exchange(other.fbo_, 0)),
      texture_(std::exchange(other.texture_, 0)),
      width_(other.width_),
      height_(other.height_),
      float_(other.float_)
{
}

Fbo& Fbo::operator=(Fbo&& other) noexcept
{
    if (this != &other) {
        release();
        fbo_ = std::exchange(other.fbo_, 0);
        texture_ = std::exchange(other.texture_, 0);
        width_ = other.width_;
        height_ = other.height_;
        float_ = other.float_;
    }
    return *this;
}

void Fbo::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void Fbo::unbind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace bsl
