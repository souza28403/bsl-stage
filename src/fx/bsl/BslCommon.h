#pragma once

#include "ai/AiEngine.h"
#include "core/Generators.h"
#include "fx/FxGl.h"
#include "fx/IEffect.h"
#include "render/Shader.h"

#include <glad/gl.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

extern const char* const kBslPrelude;

inline constexpr const char* kSliceRefinerId = "bsl:slice-refiner-2";
inline constexpr const char* kPixelMapPatternId = "bsl:pixelmap-pattern";
inline constexpr const char* kSliceRefiner3Id = "bsl:slice-refiner";
inline constexpr const char* kWebBackgroundsId = kBslWebBackgroundsId;
inline constexpr const char* kVhsId = "bsl:vhs";
inline constexpr const char* kRelightId = "bsl:relight-25d";
inline constexpr const char* kBodyGlitchId = "bsl:body-glitch";
inline constexpr const char* kAuraId = "bsl:performer-aura";
inline constexpr const char* kZSliceId = "bsl:z-slice";

class FeedbackBuffer
{
public:
    bool ensure(int w, int h);
    GLuint texture() const { return fbo_ ? fbo_->texture() : 0; }
    void capture(const Fbo& output, int w, int h);
    void reset() { fbo_.reset(); }

private:
    std::unique_ptr<Fbo> fbo_;
    TexBlitter blit_;
};

float effectBand(float snapshotBand, float gain, float gate);

int enumIndex(const media::ParamSet& params, const std::string& id);

float riseFallFollow(float current, float target, double dt, float releaseRate);

class BslEffect : public IEffect
{
public:
    struct Meta
    {
        const char* id;
        const char* name;
        const char* category;
    };

    BslEffect(Meta meta, std::vector<media::ParamDesc> descs) : meta_(meta), descs_(std::move(descs)) {}

    std::string id() const override { return meta_.id; }
    std::string name() const override { return meta_.name; }
    std::string category() const override { return meta_.category; }
    std::vector<media::ParamDesc> paramDescs() const override { return descs_; }
    void setParams(const media::ParamSet& params) override;
    void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) final;
    media::SourceStatus status() const override;

protected:
    virtual std::string fragmentSource() const = 0;
    virtual void prepare(const EffectContext& , int , int ) {}
    virtual void render(const EffectContext& ctx, int w, int h) = 0;
    virtual void afterDraw(const EffectContext& , int , int , const Fbo& ) {}

    const media::ParamSet& params() const;
    Shader& shader() { return *shader_; }
    GLuint inputTexture() const { return inputTex_; }
    void passThroughThisFrame() { skip_ = true; }
    float bandBass(const EffectContext& ctx) const;
    float bandHigh(const EffectContext& ctx) const;

    Meta meta_;
    std::vector<media::ParamDesc> descs_;

private:
    void build();

    std::optional<media::ParamSet> params_;
    std::unique_ptr<Shader> shader_;
    GLuint inputTex_ = 0;
    bool skip_ = false;
    bool failed_ = false;
    std::string error_;
    ScreenVao vao_;
    TexBlitter blit_;
};

class AiEffect : public BslEffect
{
public:
    using BslEffect::BslEffect;
    media::SourceStatus status() const override;

protected:
    virtual unsigned aiNeed() const = 0;
    virtual void prepareAi(const EffectContext& ctx, int w, int h, const ai::AiFrame& frame) = 0;

    const ai::AiFrame& aiFrame() const { return frame_; }

private:
    void prepare(const EffectContext& ctx, int w, int h) final;

    ai::AiFrame frame_ = [] {
        ai::AiFrame f;
        f.state = ai::AiState::Ready;
        return f;
    }();
};

}
