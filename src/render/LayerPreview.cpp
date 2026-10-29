#include "render/LayerPreview.h"

#include "render/CompositeShaders.h"

#include <algorithm>
#include <cmath>

namespace bsl {

namespace {

constexpr int kPreviewWidth = 640; // largura da textura da previa de camada

// Tamanho da fonte na tela, em fracao do canvas (mesma conta do Compositor)
void fitSize(FitMode mode, int cw, int ch, int sw, int sh, float& outX, float& outY)
{
    const float fcw = static_cast<float>(cw), fch = static_cast<float>(ch);
    const float fsw = static_cast<float>(std::max(sw, 1)), fsh = static_cast<float>(std::max(sh, 1));
    float kx = 1.0f, ky = 1.0f;
    switch (mode) {
    case FitMode::Fit: kx = ky = std::min(fcw / fsw, fch / fsh); break;
    case FitMode::Fill: kx = ky = std::max(fcw / fsw, fch / fsh); break;
    case FitMode::Stretch:
        kx = fcw / fsw;
        ky = fch / fsh;
        break;
    case FitMode::Original: break;
    }
    outX = fsw * kx / fcw;
    outY = fsh * ky / fch;
}

} // namespace

LayerPreview::LayerPreview() = default;

LayerPreview::~LayerPreview() = default; // shutdown() ja liberou o que era de GL

void LayerPreview::shutdown()
{
    release();
    shader_.reset();
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void LayerPreview::release()
{
    base_.reset();
    out_.reset();
    pool_.clear();
}

GLuint LayerPreview::compose(GLuint tex, int srcW, int srcH, FitMode fit, BlendMode blend, float opacity, int canvasW,
                             int canvasH)
{
    base_.reset();
    out_.reset();
    if (tex == 0 || srcW <= 0 || srcH <= 0 || canvasW <= 0 || canvasH <= 0)
        return 0;
    if (!shader_) {
        shader_ = std::make_unique<Shader>(shaders::kVertexSource, shaders::fragment(shaders::kBlendBody, true).c_str());
        glGenVertexArrays(1, &vao_);
    }

    const int w = kPreviewWidth;
    const int h = std::clamp(static_cast<int>(std::lround(static_cast<double>(w) * canvasH / canvasW)), 16, 2048);
    base_ = pool_.acquire(w, h);
    out_ = pool_.acquire(w, h);

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Fundo preto opaco
    base_->bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float sx = 1.0f, sy = 1.0f;
    fitSize(fit, canvasW, canvasH, srcW, srcH, sx, sy);
    shader_->use();
    shader_->setInt("uBase", 0);
    shader_->setInt("uSrc", 1);
    shader_->setVec2("uRegion", static_cast<float>(w), static_cast<float>(h));
    shader_->setVec2("uSize", sx, sy);
    shader_->setInt("uSrcTop", 1);
    shader_->setFloat("uOpacity", std::clamp(opacity, 0.0f, 1.0f));
    shader_->setInt("uBlend", static_cast<int>(blend));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, base_->texture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    out_->bind();
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    Fbo::unbind();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    pool_.endFrame();
    return out_->texture();
}

} // namespace bsl
