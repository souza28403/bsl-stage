#pragma once

#include "media/SourceParams.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::media {

enum class LoopMode { Loop, Once, PingPong };

enum class ClockEvent { None, Wrapped, Ended };

struct ClipClock {
    double duration = 0.0;
    double position = 0.0;
    double speed = 1.0;
    bool playing = true;
    LoopMode loop = LoopMode::Loop;
    int dir = 1;

    ClockEvent advance(double dt)
    {
        if (!playing || duration <= 0.0)
            return ClockEvent::None;
        position += dt * speed * dir;
        ClockEvent ev = ClockEvent::None;
        switch (loop) {
        case LoopMode::Loop:
            if (position >= duration || position < 0.0) {
                position = std::fmod(position, duration);
                if (position < 0.0)
                    position += duration;
                ev = ClockEvent::Wrapped;
            }
            break;
        case LoopMode::Once:
            if (position >= duration) {
                position = duration;
                playing = false;
                ev = ClockEvent::Ended;
            } else if (position <= 0.0 && dt * speed * dir < 0.0) {
                position = 0.0;
                playing = false;
                ev = ClockEvent::Ended;
            }
            break;
        case LoopMode::PingPong:
            for (int guard = 0; guard < 8 && (position > duration || position < 0.0); ++guard) {
                position = position > duration ? 2.0 * duration - position : -position;
                dir = -dir;
                ev = ClockEvent::Wrapped;
            }
            position = std::clamp(position, 0.0, duration);
            break;
        }
        return ev;
    }
};

class ISource {
public:
    virtual ~ISource() = default;

    virtual bool open(const std::filesystem::path& path, std::string* error = nullptr) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual void update(double dt) = 0;

    virtual GLuint texture() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool hasAlpha() const = 0;

    double duration() const { return clock_.duration; }
    double position() const { return clock_.position; }
    double speed() const { return clock_.speed; }
    bool isPlaying() const { return clock_.playing; }
    LoopMode loopMode() const { return clock_.loop; }

    void seek(double seconds)
    {
        clock_.position = clock_.duration > 0.0 ? std::clamp(seconds, 0.0, clock_.duration) : 0.0;
        onSeek(clock_.position);
    }
    void setSpeed(double s) { clock_.speed = supportsReverse() ? s : std::max(0.0, s); }
    void play()
    {
        if (clock_.loop == LoopMode::Once && clock_.duration > 0.0 && clock_.position >= clock_.duration - 1e-6)
            seek(0.0);
        clock_.playing = true;
    }
    void pause() { clock_.playing = false; }
    void setLoopMode(LoopMode m)
    {
        clock_.loop = (m == LoopMode::PingPong && !supportsReverse()) ? LoopMode::Loop : m;
        clock_.dir = 1;
    }

    virtual bool supportsReverse() const { return true; }

    virtual std::vector<ParamDesc> paramDescs() const { return {}; }
    void setParams(const ParamValues& values)
    {
        values_ = values;
        onParamsChanged();
    }
    const ParamValues& paramValues() const { return values_; }

    void setCanvasSize(int w, int h)
    {
        const bool first = canvasW_ == 0 && canvasH_ == 0;
        if (w == canvasW_ && h == canvasH_)
            return;
        canvasW_ = w;
        canvasH_ = h;
        if (!first)
            onCanvasResized(w, h);
    }
    void setTempo(double beat, float bpm) { onTempo(beat, bpm); }

    virtual bool pollParamChanges(ParamValues& values)
    {
        (void)values;
        return false;
    }

    virtual bool isLive() const { return false; }
    virtual SourceStatus status() const { return {}; }

protected:
    virtual void onSeek(double ) {}
    virtual void onParamsChanged() {}
    virtual void onCanvasResized(int , int ) {}
    virtual void onTempo(double , float ) {}
    ParamSet currentParams() const { return ParamSet(paramDescs(), values_); }

    ClipClock clock_;
    ParamValues values_;
    int canvasW_ = 0, canvasH_ = 0;
};

}
