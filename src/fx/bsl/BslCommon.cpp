#include "fx/bsl/BslCommon.h"

#include "app/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace bsl::fx::bsllight {

const char* const kBslPrelude = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uSize;
uniform int uTop;
uniform float uTime;
uniform float uBeat;
uniform float uBeatPhase;
vec2 imgPos() { return uTop == 1 ? vUv : vec2(vUv.x, 1.0 - vUv.y); }
vec2 toTex(vec2 p) { return uTop == 1 ? p : vec2(p.x, 1.0 - p.y); }
vec2 glPos() { vec2 p = imgPos(); return vec2(p.x, 1.0 - p.y); }
vec4 samp(vec2 p) { return texture(uTex, toTex(p)); }
vec4 sampP(vec2 p) { vec4 c = samp(p); c.rgb *= c.a; return c; }
vec4 sampGl(vec2 q) { return sampP(vec2(q.x, 1.0 - q.y)); }
vec4 toStraight(vec3 rgb, float a)
{
    float outA = max(a, min(1.0, max(rgb.r, max(rgb.g, rgb.b))));
    return vec4(rgb / max(outA, 1.0e-4), outA);
}
)";

bool FeedbackBuffer::ensure(int w, int h)
{
    if (fbo_ && fbo_->width() == w && fbo_->height() == h)
        return false;
    GLint prevFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    fbo_ = std::make_unique<Fbo>(w, h);
    fbo_->bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    return true;
}

void FeedbackBuffer::capture(const Fbo& output, int w, int h)
{
    if (!fbo_)
        return;
    blit_.copy(output.texture(), w, h, *fbo_);
}

float effectBand(float snapshotBand, float gain, float gate)
{
    float v = snapshotBand * gain;
    if (v < gate)
        v = 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

int enumIndex(const media::ParamSet& params, const std::string& id)
{
    const media::ParamDesc* d = params.desc(id);
    if (!d)
        return 0;
    const std::string cur = params.s(id);
    for (size_t i = 0; i < d->options.size(); ++i)
        if (d->options[i].value == cur)
            return static_cast<int>(i);
    return 0;
}

float riseFallFollow(float current, float target, double dt, float releaseRate)
{
    if (target > current)
        return target;
    return current + (target - current) * (1.0f - std::exp(-static_cast<float>(dt) * releaseRate));
}

void BslEffect::setParams(const media::ParamSet& params)
{
    params_ = params;
}

const media::ParamSet& BslEffect::params() const
{
    static thread_local std::optional<media::ParamSet> fallback;
    if (params_)
        return *params_;
    fallback.emplace(descs_, media::ParamValues());
    return *fallback;
}

float BslEffect::bandBass(const EffectContext& ctx) const
{
    return effectBand(ctx.audio.bass, params().f("gain"), params().f("gate"));
}

float BslEffect::bandHigh(const EffectContext& ctx) const
{
    return effectBand(ctx.audio.high, params().f("gain"), params().f("gate"));
}

void BslEffect::build()
{
    try {
        const std::string frag = fragmentSource();
        shader_ = std::make_unique<Shader>(kFxVertexSource, frag.c_str());
    } catch (const std::exception& e) {
        failed_ = true;
        error_ = std::string("Falha no shader do efeito: ") + e.what();
        log::warn(std::string(meta_.name) + ": " + error_);
    }
}

media::SourceStatus BslEffect::status() const
{
    if (failed_)
        return {media::SourceStatus::Level::Warning, error_};
    return {};
}

void BslEffect::process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output)
{
    if (inputTex == 0)
        return;
    if (!shader_ && !failed_)
        build();
    if (failed_) {
        blit_.copy(inputTex, w, h, output);
        return;
    }

    inputTex_ = inputTex;
    skip_ = false;
    prepare(ctx, w, h);
    if (skip_) {
        blit_.copy(inputTex, w, h, output);
        return;
    }
    bindOutput(output, w, h);
    shader_->use();
    shader_->setInt("uTex", 0);
    shader_->setVec2("uSize", static_cast<float>(w), static_cast<float>(h));
    shader_->setInt("uTop", ctx.topOrigin ? 1 : 0);
    shader_->setFloat("uTime", static_cast<float>(std::fmod(ctx.time, 86400.0)));
    shader_->setFloat("uBeat", static_cast<float>(std::fmod(ctx.beat, 65536.0)));
    shader_->setFloat("uBeatPhase", ctx.beatPhase);
    bindTexture(0, inputTex);
    render(ctx, w, h);
    vao_.draw();

    for (int unit = 3; unit >= 0; --unit)
        bindTexture(unit, 0);
    glUseProgram(0);
    afterDraw(ctx, w, h, output);
}

void AiEffect::prepare(const EffectContext& ctx, int w, int h)
{
    const std::uint64_t key = ctx.sourceId != 0 ? ctx.sourceId : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    frame_ = ai::AiEngine::instance().acquire(key, ctx.frameIndex, inputTexture(), w, h, ctx.topOrigin, aiNeed(), ctx.eco);
    if (frame_.state != ai::AiState::Ready) {
        passThroughThisFrame();
        return;
    }
    prepareAi(ctx, w, h, frame_);
}

media::SourceStatus AiEffect::status() const
{
    const media::SourceStatus base = BslEffect::status();
    if (base.level != media::SourceStatus::Level::Ok)
        return base;
    if (frame_.state == ai::AiState::Ready)
        return {};
    const media::SourceStatus::Level level =
        frame_.state == ai::AiState::Unavailable ? media::SourceStatus::Level::Warning : media::SourceStatus::Level::Info;
    return {level, ai::aiStatusText(frame_)};
}

}
