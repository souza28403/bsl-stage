#include "render/Compositor.h"

#include "audio/AudioAnalyzer.h"
#include "fx/ChainRunner.h"
#include "render/CompositeShaders.h"
#include "render/ZoneMasks.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bsl {

namespace {

using shaders::fragment;
using shaders::kBlendBody;
using shaders::kVertexSource;

// Encaixe da fonte no canvas num FBO proprio (camada com efeitos): fora da fonte fica transparente
const char* const kFitBody = R"(
uniform sampler2D uSrc;
uniform vec2 uRegion;
uniform vec2 uSize;
uniform int uSrcTop;
void main()
{
    fragColor = sampleSource(uSrc, gl_FragCoord.xy / uRegion, uSize, uSrcTop);
}
)";

// Master: o resultado dos efeitos vai por cima do preto (alpha reto) e recebe o fade; sai opaco
const char* const kFinishBody = R"(
uniform sampler2D uSrc;
uniform float uFade;    // master: escurece o resultado (1 = sem efeito)
void main()
{
    vec4 c = texelFetch(uSrc, ivec2(gl_FragCoord.xy), 0);
    fragColor = vec4(c.rgb * c.a * uFade, 1.0);
}
)";

// Tamanho da fonte na tela, em fracao do canvas
void fitSize(FitMode mode, int cw, int ch, int sw, int sh, float& outX, float& outY)
{
    const float fcw = static_cast<float>(cw), fch = static_cast<float>(ch);
    const float fsw = static_cast<float>(std::max(sw, 1)), fsh = static_cast<float>(std::max(sh, 1));
    float kx = 1.0f, ky = 1.0f;
    switch (mode) {
    case FitMode::Fit:
        kx = ky = std::min(fcw / fsw, fch / fsh);
        break;
    case FitMode::Fill:
        kx = ky = std::max(fcw / fsw, fch / fsh);
        break;
    case FitMode::Stretch:
        kx = fcw / fsw;
        ky = fch / fsh;
        break;
    case FitMode::Original:
        break;
    }
    outX = fsw * kx / fcw;
    outY = fsh * ky / fch;
}

} // namespace

Compositor::Compositor(int width, int height)
    : fbo_{Fbo(width, height), Fbo(width, height)},
      shader_(kVertexSource, fragment(kBlendBody, true).c_str()),
      fit_(kVertexSource, fragment(kFitBody, true).c_str()),
      finish_(kVertexSource, fragment(kFinishBody, false).c_str())
{
    glGenVertexArrays(1, &vao_);
}

Compositor::~Compositor()
{
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

void Compositor::resize(int width, int height)
{
    if (width == fbo_[0].width() && height == fbo_[0].height())
        return;
    Fbo a(width, height);
    Fbo b(width, height);
    fbo_[0] = std::move(a);
    fbo_[1] = std::move(b);
    current_ = 0;
}

void Compositor::render(Composition& comp, float previewScale, bool eco)
{
    renderedScale_ = std::clamp(previewScale, 0.1f, 1.0f);
    const int w = std::max(1, static_cast<int>(static_cast<float>(width()) * renderedScale_));
    const int h = std::max(1, static_cast<int>(static_cast<float>(height()) * renderedScale_));
    // Escala exata para os UVs baterem com a area renderizada
    renderedScale_ = static_cast<float>(w) / static_cast<float>(width());

    // Relogio dos efeitos
    const auto now = std::chrono::steady_clock::now();
    double dt = haveLast_ ? std::chrono::duration<double>(now - lastRender_).count() : 0.0;
    dt = std::clamp(dt, 0.0, 0.1);
    lastRender_ = now;
    haveLast_ = true;
    time_ += dt;

    fx::EffectContext ctx;
    ctx.time = time_;
    ctx.dt = dt;
    ctx.frameIndex = frame_++;
    ctx.bpm = comp.bpm;
    ctx.beat = comp.beat;
    ctx.beatPhase = static_cast<float>(comp.beat - std::floor(comp.beat));
    ctx.barPhase = static_cast<float>(std::fmod(std::max(comp.beat, 0.0), 4.0) / 4.0);
    ctx.canvasWidth = comp.width;
    ctx.canvasHeight = comp.height;
    ctx.eco = eco;
    ctx.pool = &pool_;
    ctx.audio = audio::AudioAnalyzer::instance().snapshot();

    // So vale a mascara de zonas publicada neste quadro (Z-Slice): ver render/ZoneMasks.h
    ZoneMasks::instance().beginFrame();

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Estado do passe de composicao (os efeitos mexem no GL; refeito depois de cada cadeia)
    auto blendState = [&] {
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    };
    blendState();

    // Fundo preto opaco
    current_ = 0;
    fbo_[0].bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool anySolo = false;
    for (const Layer& l : comp.layers)
        anySolo = anySolo || l.solo;

    for (int i = 0; i < comp.layerCount(); ++i) {
        Layer& layer = comp.layers[static_cast<size_t>(i)];
        if (comp.masterBypass || layer.bypass || (anySolo && !layer.solo))
            continue;
        Clip* clip = comp.activeClip(i);
        if (!clip || !clip->isOpen() || clip->source->texture() == 0)
            continue;
        const media::ISource& src = *clip->source;

        float sx = 1.0f, sy = 1.0f;
        fitSize(clip->fit, comp.width, comp.height, src.width(), src.height(), sx, sy);

        GLuint tex = src.texture();
        bool top = true; // texturas de fonte: linha 0 = topo da imagem

        // --- Cadeia do clip: no tamanho da fonte (no Eco, no maximo do tamanho da area renderizada) ---
        FboPool::Lease srcCopy;
        FboPool::Lease clipLease;
        if (clip->effects.hasActive() && src.width() > 0 && src.height() > 0) {
            int cw = src.width(), ch = src.height();
            if (eco) {
                const float k = std::min(1.0f, std::max(static_cast<float>(w) / static_cast<float>(cw),
                                                        static_cast<float>(h) / static_cast<float>(ch)));
                if (k < 0.999f) {
                    cw = std::max(1, static_cast<int>(std::lround(static_cast<float>(cw) * k)));
                    ch = std::max(1, static_cast<int>(std::lround(static_cast<float>(ch) * k)));
                    srcCopy = pool_.acquire(cw, ch);
                    blit_.copy(tex, src.width(), src.height(), *srcCopy, true); // reduz com filtro bilinear
                }
            }
            ctx.topOrigin = true;
            ctx.chain = fx::ChainKind::Clip;
            ctx.layerIndex = i;
            ctx.sourceId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(clip->source.get()));
            clipLease = fx::ChainRunner::run(clip->effects, ctx, srcCopy ? srcCopy->texture() : tex, cw, ch);
            if (clipLease)
                tex = clipLease->texture();
            blendState();
        }

        // --- Cadeia da camada: no tamanho do canvas (area renderizada), depois do encaixe ---
        FboPool::Lease fitLease;
        FboPool::Lease layerLease;
        float bsx = sx, bsy = sy;
        if (layer.effects.hasActive()) {
            fitLease = pool_.acquire(w, h);
            fitLease->bind();
            glViewport(0, 0, w, h);
            fit_.use();
            fit_.setInt("uSrc", 0);
            fit_.setVec2("uRegion", static_cast<float>(w), static_cast<float>(h));
            fit_.setVec2("uSize", sx, sy);
            fit_.setInt("uSrcTop", top ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);

            ctx.topOrigin = false; // o FBO do encaixe e do canvas: linha 0 = base
            ctx.chain = fx::ChainKind::Layer;
            ctx.layerIndex = i;
            ctx.sourceId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&layer));
            layerLease = fx::ChainRunner::run(layer.effects, ctx, fitLease->texture(), w, h);
            tex = layerLease ? layerLease->texture() : fitLease->texture();
            top = false;
            bsx = bsy = 1.0f; // ja encaixada no canvas
            blendState();
        }

        shader_.use();
        shader_.setInt("uBase", 0);
        shader_.setInt("uSrc", 1);
        shader_.setVec2("uRegion", static_cast<float>(w), static_cast<float>(h));
        shader_.setVec2("uSize", bsx, bsy);
        shader_.setInt("uSrcTop", top ? 1 : 0);
        shader_.setFloat("uOpacity", std::clamp(layer.opacity, 0.0f, 1.0f));
        shader_.setInt("uBlend", static_cast<int>(layer.blendMode));

        const int dst = 1 - current_;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_[current_].texture());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex);
        fbo_[dst].bind();
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        current_ = dst;
    }

    // --- Master: efeitos sobre o resultado de todas as camadas, depois o fade ---
    const float master = std::clamp(comp.masterOpacity, 0.0f, 1.0f);
    const bool masterFx = !comp.masterBypass && comp.masterEffects.hasActive();
    if (masterFx || (!comp.masterBypass && master < 0.999f)) {
        FboPool::Lease inLease;
        FboPool::Lease outLease;
        GLuint tex = fbo_[current_].texture();
        if (masterFx) {
            GLuint inTex = tex;
            if (w != width() || h != height()) {
                // O canvas so tem a regiao (0,0)-(w,h) renderizada: copia para uma textura w x h para os efeitos
                inLease = pool_.acquire(w, h);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_[current_].id());
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, inLease->id());
                glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                inTex = inLease->texture();
            }
            ctx.topOrigin = false; // canvas: linha 0 = base
            ctx.chain = fx::ChainKind::Master;
            ctx.layerIndex = -1;
            ctx.sourceId = 1;      // master
            outLease = fx::ChainRunner::run(comp.masterEffects, ctx, inTex, w, h);
            if (outLease)
                tex = outLease->texture();
            else if (inLease)
                tex = inLease->texture();
            blendState();
        }
        finish_.use();
        finish_.setInt("uSrc", 0);
        finish_.setFloat("uFade", master);
        const int dst = 1 - current_;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        fbo_[dst].bind();
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        current_ = dst;
    }

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    Fbo::unbind();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    pool_.endFrame();
}

} // namespace bsl
