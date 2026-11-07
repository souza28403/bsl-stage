#include "render/Compositor.h"

#include "audio/AudioAnalyzer.h"
#include "fx/ChainRunner.h"
#include "render/CompositeShaders.h"
#include "render/ZoneMasks.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace bsl {

namespace {

using shaders::fragment;
using shaders::kBlendBody;
using shaders::kVertexSource;

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

const char* const kFinishBody = R"(
uniform sampler2D uSrc;
uniform float uFade;
void main()
{
    vec4 c = texelFetch(uSrc, ivec2(gl_FragCoord.xy), 0);
    fragColor = vec4(c.rgb * c.a * uFade, 1.0);
}
)";

constexpr std::uint64_t kViewMasterSourceBase = 0x40000000ull;

}

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
    views_.clear();
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

void Compositor::resize(int width, int height)
{
    if (width == fbo_[0].width() && height == fbo_[0].height())
        return;
    views_.clear();
    Fbo a(width, height);
    Fbo b(width, height);
    fbo_[0] = std::move(a);
    fbo_[1] = std::move(b);
    current_ = 0;
}

bool Compositor::layerView(int layerId, bool includeMaster, GLuint& texture, bool applyTransform) const
{
    for (const View& v : views_) {
        if (v.layerId == layerId && v.includeMaster == includeMaster && v.applyTransform == applyTransform) {
            texture = v.lease ? v.lease->texture() : 0;
            return true;
        }
    }
    texture = 0;
    return false;
}

void Compositor::render(Composition& comp, float previewScale, bool eco, const std::vector<LayerViewRequest>& layerViews)
{
    renderedScale_ = std::clamp(previewScale, 0.1f, 1.0f);
    const int w = std::max(1, static_cast<int>(static_cast<float>(width()) * renderedScale_));
    const int h = std::max(1, static_cast<int>(static_cast<float>(height()) * renderedScale_));
    renderedScale_ = static_cast<float>(w) / static_cast<float>(width());

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

    ZoneMasks::instance().beginFrame();

    views_.clear();
    for (const LayerViewRequest& rq : layerViews) {
        const int li = comp.findLayerById(rq.layerId);
        const bool dup = std::any_of(views_.begin(), views_.end(), [&](const View& v) {
            return v.layerId == rq.layerId && v.includeMaster == rq.includeMaster && v.applyTransform == rq.applyTransform;
        });
        if (li < 0 || dup)
            continue;
        View v;
        v.layerId = rq.layerId;
        v.includeMaster = rq.includeMaster;
        v.applyTransform = rq.applyTransform;
        v.layerIndex = li;
        views_.push_back(std::move(v));
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    auto blendState = [&] {
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    };
    blendState();

    current_ = 0;
    fbo_[0].bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    FboPool::Lease black;
    auto ensureBlack = [&] {
        if (black)
            return;
        black = pool_.acquire(width(), height());
        black->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    };

    auto blendOnto = [&](GLuint baseTex, const Fbo& dstFbo, const Layer& layer, GLuint tex, bool top, float bsx, float bsy,
                         const TransformGpu& xf) {
        shader_.use();
        shader_.setInt("uBase", 0);
        shader_.setInt("uSrc", 1);
        shader_.setVec2("uRegion", static_cast<float>(w), static_cast<float>(h));
        shader_.setVec2("uSize", bsx, bsy);
        shader_.setInt("uSrcTop", top ? 1 : 0);
        shader_.setFloat("uOpacity", std::clamp(layer.opacity, 0.0f, 1.0f));
        shader_.setInt("uBlend", static_cast<int>(layer.blendMode));
        shader_.setInt("uXf", xf.on ? 1 : 0);
        if (xf.on) {
            shader_.setVec4("uXfBox", xf.box[0], xf.box[1], xf.box[2], xf.box[3]);
            shader_.setVec4("uXfPivot", xf.pivot[0], xf.pivot[1], xf.cs[0], xf.cs[1]);
            shader_.setVec4("uXfCrop", xf.crop[0], xf.crop[1], xf.crop[2], xf.crop[3]);
            shader_.setVec2("uCanvas", xf.canvas[0], xf.canvas[1]);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, baseTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex);
        dstFbo.bind();
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    };

    bool anySolo = false;
    for (const Layer& l : comp.layers)
        anySolo = anySolo || (l.solo && !l.sliceOnly);

    for (int i = 0; i < comp.layerCount(); ++i) {
        Layer& layer = comp.layers[static_cast<size_t>(i)];
        const bool inMain = !layer.sliceOnly && !comp.outputOff() && !layer.bypass && !(anySolo && !layer.solo);
        std::vector<View*> mine;
        if (!layer.bypass)
            for (View& v : views_)
                if (v.layerIndex == i)
                    mine.push_back(&v);
        if (!inMain && mine.empty())
            continue;
        Clip* clip = comp.activeClip(i);
        if (!clip || !clip->isOpen() || clip->source->texture() == 0)
            continue;
        const media::ISource& src = *clip->source;

        float sx = 1.0f, sy = 1.0f;
        fitFraction(clip->fit, comp.width, comp.height, src.width(), src.height(), sx, sy);

        GLuint tex = src.texture();
        bool top = true;

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
                    blit_.copy(tex, src.width(), src.height(), *srcCopy, true);
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

            ctx.topOrigin = false;
            ctx.chain = fx::ChainKind::Layer;
            ctx.layerIndex = i;
            ctx.sourceId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&layer));
            layerLease = fx::ChainRunner::run(layer.effects, ctx, fitLease->texture(), w, h);
            tex = layerLease ? layerLease->texture() : fitLease->texture();
            top = false;
            bsx = bsy = 1.0f;
            blendState();
        }

        const LayerTransform& transform = clip->ownTransform ? clip->transform : layer.transform;
        const TransformGpu xf = transform.gpu(comp.width, comp.height);
        const TransformGpu noXf;

        if (inMain) {
            const int dst = 1 - current_;
            blendOnto(fbo_[current_].texture(), fbo_[dst], layer, tex, top, bsx, bsy, xf);
            current_ = dst;
        }
        for (View* v : mine) {
            ensureBlack();
            v->lease = pool_.acquire(width(), height());
            blendOnto(black->texture(), *v->lease, layer, tex, top, bsx, bsy, v->applyTransform ? xf : noXf);
        }
    }

    const float master = std::clamp(comp.masterOpacity, 0.0f, 1.0f);
    const bool masterFx = !comp.outputOff() && comp.masterEffects.hasActive();
    const bool masterFade = !comp.outputOff() && master < 0.999f;

    auto masterPass = [&](fx::EffectChain* chain, std::uint64_t sourceId, bool isCopy, const Fbo& acc, const Fbo& out) {
        FboPool::Lease inLease;
        FboPool::Lease outLease;
        GLuint tex = acc.texture();
        if (chain) {
            GLuint inTex = tex;
            if (w != width() || h != height()) {
                inLease = pool_.acquire(w, h);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, acc.id());
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, inLease->id());
                glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                inTex = inLease->texture();
            }
            ctx.topOrigin = false;
            ctx.chain = fx::ChainKind::Master;
            ctx.layerIndex = -1;
            ctx.sourceId = sourceId;
            ctx.preview = isCopy;
            outLease = fx::ChainRunner::run(*chain, ctx, inTex, w, h);
            ctx.preview = false;
            if (outLease)
                tex = outLease->texture();
            else if (inLease)
                tex = inLease->texture();
            blendState();
        }
        finish_.use();
        finish_.setInt("uSrc", 0);
        finish_.setFloat("uFade", master);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        out.bind();
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    };

    if (masterFx || masterFade) {
        const int dst = 1 - current_;
        masterPass(masterFx ? &comp.masterEffects : nullptr, 1, false, fbo_[current_], fbo_[dst]);
        current_ = dst;
    }

    std::set<int> copiesInUse;
    for (View& v : views_) {
        if (!v.includeMaster)
            continue;
        if (comp.outputOff()) {
            v.lease.reset();
            continue;
        }
        if (!masterFx && !masterFade)
            continue;
        if (!v.lease && !masterFx)
            continue;
        if (!v.lease)
            ensureBlack();
        fx::EffectChain* chain = nullptr;
        if (masterFx) {
            chain = &masterCopies_[v.layerId];
            chain->syncFrom(comp.masterEffects);
            copiesInUse.insert(v.layerId);
        }
        FboPool::Lease out = pool_.acquire(width(), height());
        masterPass(chain, kViewMasterSourceBase + static_cast<std::uint64_t>(v.layerId), true,
                   v.lease ? *v.lease : *black, *out);
        v.lease = std::move(out);
    }
    std::erase_if(masterCopies_, [&](const auto& kv) { return copiesInUse.count(kv.first) == 0; });

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    Fbo::unbind();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    black.reset();
    pool_.endFrame();
}

}
