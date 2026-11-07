#pragma once

#include "audio/AudioTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsl::audio {

class AudioAnalyzer
{
public:
    static AudioAnalyzer& instance();

    void start();
    void stop();
    bool running() const;

    void setConfig(const AudioConfig& config);
    AudioConfig config() const;

    AudioSnapshot snapshot() const;

    std::string statusText() const;

    void setOverride(const std::optional<AudioSnapshot>& snapshot);

    static std::vector<AudioDeviceInfo> listDevices(AudioSource source);

    ~AudioAnalyzer();
    AudioAnalyzer(const AudioAnalyzer&) = delete;
    AudioAnalyzer& operator=(const AudioAnalyzer&) = delete;

private:
    AudioAnalyzer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
