#include "render/SolidColorSource.h"

#include <stdexcept>

namespace bsl {

SolidColorSource::SolidColorSource(int width, int height) : width_(width), height_(height) {}

SolidColorSource::~SolidColorSource()
{
    close();
}

std::vector<media::ParamDesc> SolidColorSource::describe(const media::ParamValues&)
{
    using namespace media::param;
    std::vector<media::ParamDesc> d;
    d.push_back(makeColor("color", "Cor", {1.0f, 1.0f, 1.0f, 1.0f}));
    return d;
}

bool SolidColorSource::open(const std::filesystem::path& /*path*/, std::string* error)
{
    close();
    try {
        fbo_ = std::make_unique<Fbo>(width_, height_);
    } catch (const std::exception& e) {
        fbo_.reset();
        if (error)
            *error = e.what();
        return false;
    }
    clock_ = media::ClipClock{};
    dirty_ = true;
    render();
    return true;
}

void SolidColorSource::close()
{
    fbo_.reset();
}

void SolidColorSource::update(double /*dt*/)
{
    if (fbo_ && dirty_)
        render();
}

void SolidColorSource::render()
{
    dirty_ = false;
    const media::Color c = currentParams().c("color");
    alpha_ = c.a;

    GLint prevFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLfloat prevClear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);

    fbo_->bind();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);

    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

} // namespace bsl
