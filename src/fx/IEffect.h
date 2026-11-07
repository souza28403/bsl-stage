#pragma once

#include "audio/AudioTypes.h"
#include "media/SourceParams.h"
#include "render/Fbo.h"
#include "render/FboPool.h"

#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::fx {

enum class ChainKind { Unknown, Clip, Layer, Master };

struct EffectContext
{
    double time = 0.0;
    double dt = 0.0;
    std::uint64_t frameIndex = 0;
    float bpm = 120.0f;
    double beat = 0.0;
    float beatPhase = 0.0f;
    float barPhase = 0.0f;
    int canvasWidth = 1920;
    int canvasHeight = 1080;
    bool topOrigin = true;
    bool eco = false;
    bool preview = false;
    FboPool* pool = nullptr;
    std::uint64_t sourceId = 0;
    ChainKind chain = ChainKind::Unknown;
    int layerIndex = -1;
    audio::AudioSnapshot audio;
};

class IEffect
{
public:
    virtual ~IEffect() = default;

    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual std::string category() const = 0;

    virtual std::vector<media::ParamDesc> paramDescs() const = 0;
    virtual void setParams(const media::ParamSet& params) = 0;

    virtual void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) = 0;

    virtual bool needsInput() const { return true; }
    virtual bool isLive() const { return false; }
    virtual media::SourceStatus status() const { return {}; }

    virtual bool pollParamChanges(media::ParamValues& values)
    {
        (void)values;
        return false;
    }
};

}
