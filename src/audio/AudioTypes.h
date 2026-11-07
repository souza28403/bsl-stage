#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace bsl::audio {

inline constexpr int kSpectrumBands = 32;

struct AudioSnapshot
{
    bool active = false;
    int sampleRate = 0;
    float rms = 0.0f;
    float peak = 0.0f;
    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float kick = 0.0f;
    std::uint32_t beatCount = 0;
    std::array<float, kSpectrumBands> spectrum{};
};

enum class AudioSource { Loopback, Input };

struct AudioConfig
{
    bool enabled = true;
    AudioSource source = AudioSource::Loopback;
    std::string deviceId;
    float gain = 1.0f;
    float gate = 0.0f;
    float attackMs = 0.0f;
    float releaseScale = 1.0f;

    bool operator==(const AudioConfig& o) const
    {
        return enabled == o.enabled && source == o.source && deviceId == o.deviceId && gain == o.gain && gate == o.gate &&
               attackMs == o.attackMs && releaseScale == o.releaseScale;
    }
    bool operator!=(const AudioConfig& o) const { return !(*this == o); }
};

struct AudioDeviceInfo
{
    std::string id;
    std::string name;
};

}
