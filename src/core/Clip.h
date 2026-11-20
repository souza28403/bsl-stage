#pragma once

#include "core/Generators.h"
#include "core/LayerTransform.h"
#include "fx/EffectChain.h"
#include "media/ISource.h"
#include "media/MediaFormats.h"
#include "render/Texture.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl {

enum class FitMode { Fit, Fill, Stretch, Original };
inline constexpr int kFitModeCount = 4;

inline const char* fitModeName(FitMode m)
{
    static const char* const names[kFitModeCount] = {"Ajustar", "Preencher", "Esticar", "Original"};
    return names[static_cast<int>(m)];
}

inline const char* loopModeName(media::LoopMode m)
{
    switch (m) {
    case media::LoopMode::Loop: return "Loop";
    case media::LoopMode::Once: return "Uma vez";
    case media::LoopMode::PingPong: return "Ping-pong";
    }
    return "";
}

inline void fitFraction(FitMode mode, int cw, int ch, int sw, int sh, float& outX, float& outY)
{
    const float fcw = static_cast<float>(cw), fch = static_cast<float>(ch);
    const float fsw = static_cast<float>(sw > 1 ? sw : 1), fsh = static_cast<float>(sh > 1 ? sh : 1);
    float kx = 1.0f, ky = 1.0f;
    switch (mode) {
    case FitMode::Fit: kx = ky = (fcw / fsw < fch / fsh) ? fcw / fsw : fch / fsh; break;
    case FitMode::Fill: kx = ky = (fcw / fsw > fch / fsh) ? fcw / fsw : fch / fsh; break;
    case FitMode::Stretch:
        kx = fcw / fsw;
        ky = fch / fsh;
        break;
    case FitMode::Original: break;
    }
    outX = fsw * kx / fcw;
    outY = fsh * ky / fch;
}

enum class ReplayMode { Resume, Restart };

inline const char* replayModeName(ReplayMode m)
{
    return m == ReplayMode::Resume ? "Retomar de onde parou" : "Reiniciar do início";
}

struct Clip
{
    enum class Type { Empty, File, Generator };

    Type type = Type::Empty;
    std::string name;
    std::filesystem::path path;
    std::string generatorId;
    media::ParamValues params;
    media::MediaKind kind = media::MediaKind::Unknown;
    bool missing = false;
    bool missingPlugin = false;
    bool showCache = false;
    bool showReduce = false;
    std::filesystem::path playPath;
    mutable bool showCacheWarned = false;

    float speed = 1.0f;
    media::LoopMode loop = media::LoopMode::Loop;
    FitMode fit = FitMode::Fit;
    bool playing = true;
    bool interact = false;
    ReplayMode replay = ReplayMode::Restart;

    bool ownTransform = false;
    LayerTransform transform;

    fx::EffectChain effects;

    std::unique_ptr<media::ISource> source;
    std::unique_ptr<media::ISource> parked;
    std::uint64_t parkedStamp = 0;
    double parkedAccum = 0.0;
    double resumeSeconds = 0.0;

    Texture liveThumb;
    double liveThumbTime = -1.0e9;

    bool empty() const { return type == Type::Empty; }
    bool isOpen() const { return source && source->isOpen(); }
    bool isParked() const { return parked && parked->isOpen(); }
    media::ISource* liveSource() const { return source ? source.get() : parked.get(); }
    bool supportsPingPong() const { return kind != media::MediaKind::Video && !isLive() && !hasPageNav(); }

    bool isGenerator() const { return type == Type::Generator; }
    bool isTestPattern() const { return type == Type::Generator && generatorId == kTestPatternId; }
    const GeneratorInfo* generatorInfo() const { return type == Type::Generator ? findGenerator(generatorId) : nullptr; }
    bool isLive() const;
    bool isRemote() const;
    bool isWeb() const;
    bool isPaged() const;
    bool hasPageNav() const { return isPaged(); }
    bool isExclusive() const { return isLive() || hasPageNav(); }
    bool keepsAliveInBackground() const { return isLive() || isRemote(); }
    bool supportsReplayMode() const;
    ReplayMode defaultReplay() const;
    std::vector<media::ParamDesc> paramDescs() const;
    void setParam(const std::string& id, media::Json value);
    void refreshName();

    static Clip fromFile(const std::filesystem::path& file);
    static Clip testPattern();
    static Clip generator(const std::string& id);
    static Clip absentGenerator(const std::string& id);
    static Clip fromId(const std::string& idOrPath);

    void setSpeed(float s);
    void setLoop(media::LoopMode m);
    void setPlaying(bool p);
    void restart();
    void seek(double seconds);
};

}
