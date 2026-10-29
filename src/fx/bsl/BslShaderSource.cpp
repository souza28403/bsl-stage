#include "fx/bsl/BslShaderSource.h"

#include "app/Log.h"

#include <cmath>
#include <stdexcept>

namespace bsl::fx::bsllight {

const char* const kBslSourcePrelude = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform vec2 uSize;
// Posicao 0..1 com y para CIMA (a linha 0 da textura e o topo da imagem; aqui o topo e y = 1)
vec2 glUv() { return vec2(vUv.x, 1.0 - vUv.y); }
)";

bool BslShaderSource::open(const std::filesystem::path& /*path*/, std::string* /*error*/)
{
    close();
    error_.clear();
    try {
        fbo_ = std::make_unique<Fbo>(width_, height_);
        const std::string frag = std::string(kBslSourcePrelude) + fragmentBody();
        shader_ = std::make_unique<Shader>(kFxVertexSource, frag.c_str());
        vao_ = std::make_unique<ScreenVao>();
    } catch (const std::exception& e) {
        // A fonte abre mesmo assim (preta) e o ClipPanel mostra o aviso
        error_ = std::string("Falha no shader: ") + e.what();
        log::warn(error_);
        shader_.reset();
        if (!fbo_) {
            return false;
        }
    }
    clock_ = media::ClipClock{};
    time_ = 0.0;
    onOpened();
    render();
    return true;
}

void BslShaderSource::close()
{
    vao_.reset();
    shader_.reset();
    fbo_.reset();
}

void BslShaderSource::update(double dt)
{
    if (!fbo_)
        return;
    if (clock_.playing)
        time_ += dt * clock_.speed;
    render();
}

void BslShaderSource::render()
{
    if (!fbo_)
        return;

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo = 0, prevProgram = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);

    bindOutput(*fbo_, width_, height_);
    if (shader_) {
        shader_->use();
        shader_->setVec2("uSize", static_cast<float>(width_), static_cast<float>(height_));
        setUniforms(*shader_, std::fmod(time_, 86400.0));
        vao_->draw();
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glUseProgram(static_cast<GLuint>(prevProgram));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (blend)
        glEnable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (depth)
        glEnable(GL_DEPTH_TEST);
}

media::SourceStatus BslShaderSource::status() const
{
    if (!error_.empty())
        return {media::SourceStatus::Level::Warning, error_};
    const std::string extra = extraStatus();
    if (!extra.empty())
        return {media::SourceStatus::Level::Info, extra};
    return {};
}

} // namespace bsl::fx::bsllight
