#pragma once

#include "ffgl/FfglCatalog.h"
#include "ffgl/FfglParams.h"
#include "fx/FxGl.h"
#include "fx/IEffect.h"
#include "render/Shader.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bsl::ffgl {

class Plugin;

class FfglInstance final : public fx::IEffect
{
public:
    explicit FfglInstance(EntryPtr entry);
    ~FfglInstance() override;

    std::string id() const override { return entry_->typeId; }
    std::string name() const override { return pluginDisplayName(entry_->info); }
    std::string category() const override { return entry_->origin; }
    std::vector<media::ParamDesc> paramDescs() const override { return entry_->map.descs; }
    void setParams(const media::ParamSet& params) override;
    void process(const fx::EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override;
    bool needsInput() const override { return entry_->info.type != kTypeSource; }
    media::SourceStatus status() const override;
    bool pollParamChanges(media::ParamValues& values) override;

    bool faulted() const { return faulted_; }
    const std::string& faultReason() const { return faultReason_; }
    std::uint64_t framesProcessed() const { return frames_; }
    float lastBpm() const { return lastBpm_; }
    float lastBarPhase() const { return lastBarPhase_; }

private:
    bool ensureReady(int w, int h);
    void fault(const std::string& what, unsigned long code);
    void fail(const std::string& message);
    void pass(GLuint inputTex, int w, int h, Fbo& output);
    void flushParams();
    void convert(GLuint src, int w, int h, const Fbo& dst, bool unpremultiply, bool flip);
    void teardown(bool callPlugin);

    EntryPtr entry_;
    std::shared_ptr<Plugin> plugin_;
    bool pluginHeld_ = false;
    void* instance_ = nullptr;
    int width_ = 0, height_ = 0;

    bool faulted_ = false;
    std::string faultReason_;
    std::string failMessage_;
    std::uint64_t seenEpoch_ = 0;

    std::vector<PluginValue> desired_;
    std::vector<PluginValue> sent_;
    std::vector<char> sentKnown_;
    std::vector<float> eventSeen_;
    std::vector<int> pulseOff_;
    bool haveParams_ = false;
    std::chrono::steady_clock::time_point lastPoll_{};

    std::unique_ptr<Shader> convert_;
    std::unique_ptr<FboPool> ownPool_;
    fx::ScreenVao vao_;
    fx::TexBlitter blit_;
    std::uint64_t frames_ = 0;
    float lastBpm_ = 0.0f, lastBarPhase_ = 0.0f;
};

}
