#pragma once

#include "audio/AudioTypes.h"

#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace bsl::audio {

void fftRadix2(std::complex<float>* a, int n);

class AudioDsp
{
public:
    static constexpr int kBandFft = 512;
    static constexpr int kBandHop = 256;
    static constexpr int kSpecFft = 2048;
    static constexpr int kSpecHop = 1024;
    static constexpr float kKickRefGain = 2.1f;

    AudioDsp();

    void setSampleRate(int sampleRate);
    void setConfig(const AudioConfig& config) { config_ = config; }
    void reset();

    void feed(const float* mono, std::size_t count);
    void feedSilence(double seconds);

    AudioSnapshot snapshot(bool active) const;

private:
    void processBands();
    void processSpectrum();

    int sampleRate_ = 48000;
    AudioConfig config_;

    std::vector<float> ring_;
    std::size_t writeIdx_ = 0;
    int sinceBands_ = 0;
    int sinceSpec_ = 0;

    std::vector<std::complex<float>> work_;
    std::vector<float> windowBands_;
    std::vector<float> windowSpec_;

    int bassLo_ = 1, bassHi_ = 4, midLo_ = 5, midHi_ = 25, highLo_ = 30, highHi_ = 120;
    std::vector<std::pair<int, int>> specBins_;

    float rollingPeak_ = 0.05f;
    float agcScale_ = 1.0f;

    float prevBass_ = 0.0f;
    float smBass_ = 0.0f, smMid_ = 0.0f, smHigh_ = 0.0f;
    float kickEnv_ = 0.0f;
    std::uint32_t beatCount_ = 0;
    double sinceBeat_ = 1.0e9;

    float rms_ = 0.0f;
    float peak_ = 0.0f;
    std::array<float, kSpectrumBands> spectrum_{};
};

}
