#include "audio/AudioDsp.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bsl::audio {

void fftRadix2(std::complex<float>* a, int n)
{
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

namespace {

constexpr float kBassTauMs = 30.59f;
constexpr float kMidTauMs = 23.90f;
constexpr float kHighTauMs = 19.43f;
constexpr float kSpecTauMs = 90.0f;

float follow(float current, float target, float dtMs, float attackMs, float tauMs)
{
    if (target > current) {
        if (attackMs <= 0.0f)
            return target;
        return current + (target - current) * (1.0f - std::exp(-dtMs / attackMs));
    }
    return current + (target - current) * (1.0f - std::exp(-dtMs / std::max(tauMs, 0.1f)));
}

}

AudioDsp::AudioDsp() : ring_(kSpecFft, 0.0f), work_(kSpecFft), windowBands_(kBandFft), windowSpec_(kSpecFft)
{
    for (int i = 0; i < kBandFft; ++i)
        windowBands_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kBandFft - 1)));
    for (int i = 0; i < kSpecFft; ++i)
        windowSpec_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kSpecFft - 1)));
    setSampleRate(48000);
}

void AudioDsp::setSampleRate(int sampleRate)
{
    sampleRate_ = std::clamp(sampleRate, 8000, 384000);
    const float sr = static_cast<float>(sampleRate_);

    const float binHz = sr / static_cast<float>(kBandFft);
    auto bin = [&](float hz) { return std::clamp(static_cast<int>(std::lround(hz / binHz)), 1, kBandFft / 2 - 1); };
    bassLo_ = bin(93.75f);
    bassHi_ = std::max(bassLo_, bin(375.0f));
    midLo_ = bin(468.75f);
    midHi_ = std::max(midLo_, bin(2343.75f));
    highLo_ = bin(2812.5f);
    highHi_ = std::max(highLo_, bin(11250.0f));

    specBins_.clear();
    const float fLo = 40.0f;
    const float fHi = std::min(16000.0f, sr * 0.45f);
    const float specBinHz = sr / static_cast<float>(kSpecFft);
    for (int i = 0; i < kSpectrumBands; ++i) {
        const float f0 = fLo * std::pow(fHi / fLo, static_cast<float>(i) / static_cast<float>(kSpectrumBands));
        const float f1 = fLo * std::pow(fHi / fLo, static_cast<float>(i + 1) / static_cast<float>(kSpectrumBands));
        int lo = std::clamp(static_cast<int>(std::floor(f0 / specBinHz)), 1, kSpecFft / 2 - 1);
        int hi = std::clamp(static_cast<int>(std::floor(f1 / specBinHz)), lo, kSpecFft / 2 - 1);
        specBins_.emplace_back(lo, hi);
    }
}

void AudioDsp::reset()
{
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    writeIdx_ = 0;
    sinceBands_ = sinceSpec_ = 0;
    rollingPeak_ = 0.05f;
    agcScale_ = 1.0f;
    prevBass_ = smBass_ = smMid_ = smHigh_ = kickEnv_ = 0.0f;
    beatCount_ = 0;
    sinceBeat_ = 1.0e9;
    rms_ = peak_ = 0.0f;
    spectrum_.fill(0.0f);
}

void AudioDsp::feed(const float* mono, std::size_t count)
{
    if (count == 0)
        return;
    float batchPeak = 0.0f;
    double sumSq = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const float s = std::isfinite(mono[i]) ? mono[i] : 0.0f;
        batchPeak = std::max(batchPeak, std::abs(s));
        sumSq += static_cast<double>(s) * static_cast<double>(s);
        ring_[writeIdx_] = s;
        writeIdx_ = (writeIdx_ + 1) % static_cast<std::size_t>(kSpecFft);
        if (++sinceBands_ >= kBandHop) {
            sinceBands_ = 0;
            processBands();
        }
        if (++sinceSpec_ >= kSpecHop) {
            sinceSpec_ = 0;
            processSpectrum();
        }
    }

    const float per10ms = static_cast<float>(count) / (0.01f * static_cast<float>(sampleRate_));
    if (batchPeak > rollingPeak_)
        rollingPeak_ = batchPeak;
    else
        rollingPeak_ += (batchPeak - rollingPeak_) * (1.0f - std::pow(1.0f - 0.015f, std::max(per10ms, 0.0f)));
    agcScale_ = 0.50f / std::max(0.05f, std::min(rollingPeak_, 1.0f));

    const float seconds = static_cast<float>(count) / static_cast<float>(sampleRate_);
    const float batchRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(count)));
    rms_ = batchRms > rms_ ? batchRms : rms_ + (batchRms - rms_) * (1.0f - std::exp(-seconds / 0.15f));
    peak_ = batchPeak > peak_ ? batchPeak : peak_ * std::exp(-seconds / 0.35f);
}

void AudioDsp::feedSilence(double seconds)
{
    seconds = std::clamp(seconds, 0.0, 0.5);
    std::size_t left = static_cast<std::size_t>(seconds * sampleRate_);
    static const std::vector<float> zeros(4096, 0.0f);
    while (left > 0) {
        const std::size_t n = std::min<std::size_t>(left, zeros.size());
        feed(zeros.data(), n);
        left -= n;
    }
}

void AudioDsp::processBands()
{
    const float totalGain = std::clamp(config_.gain, 0.01f, 16.0f) * agcScale_;
    const std::size_t start = (writeIdx_ + static_cast<std::size_t>(kSpecFft - kBandFft)) % static_cast<std::size_t>(kSpecFft);
    for (int i = 0; i < kBandFft; ++i) {
        const float s = ring_[(start + static_cast<std::size_t>(i)) % static_cast<std::size_t>(kSpecFft)] * totalGain;
        work_[static_cast<size_t>(i)] = std::complex<float>(s * windowBands_[static_cast<size_t>(i)], 0.0f);
    }
    fftRadix2(work_.data(), kBandFft);

    auto avg = [&](int lo, int hi) {
        float sum = 0.0f;
        for (int k = lo; k <= hi; ++k)
            sum += std::abs(work_[static_cast<size_t>(k)]);
        return sum / static_cast<float>(hi - lo + 1);
    };
    float rawBass = avg(bassLo_, bassHi_) * 0.07f;
    float rawMid = avg(midLo_, midHi_) * 0.10f;
    float rawHigh = avg(highLo_, highHi_) * 0.16f;

    const float gate = std::max(config_.gate, 0.0f);
    if (rawBass < gate)
        rawBass = 0.0f;
    if (rawMid < gate)
        rawMid = 0.0f;
    if (rawHigh < gate)
        rawHigh = 0.0f;

    const float dtMs = 1000.0f * static_cast<float>(kBandHop) / static_cast<float>(sampleRate_);

    const float rbRef = rawBass / std::clamp(config_.gain, 0.01f, 16.0f) * kKickRefGain;
    float kickPulse = 0.0f;
    if (rbRef > prevBass_ * 1.25f && rbRef > 0.08f)
        kickPulse = std::min(1.5f, (rbRef - prevBass_) * 3.5f);
    prevBass_ = rbRef;
    kickEnv_ = std::max(kickPulse, kickEnv_ * std::exp(-dtMs / 100.0f));
    sinceBeat_ += static_cast<double>(dtMs) * 0.001;
    if (kickPulse > 0.0f && sinceBeat_ > 0.12) {
        ++beatCount_;
        sinceBeat_ = 0.0;
    }

    const float rel = std::clamp(config_.releaseScale, 0.25f, 4.0f);
    const float atk = std::clamp(config_.attackMs, 0.0f, 200.0f);
    smBass_ = follow(smBass_, rawBass, dtMs, atk, kBassTauMs * rel);
    smMid_ = follow(smMid_, rawMid, dtMs, atk, kMidTauMs * rel);
    smHigh_ = follow(smHigh_, rawHigh, dtMs, atk, kHighTauMs * rel);
}

void AudioDsp::processSpectrum()
{
    const float totalGain = std::clamp(config_.gain, 0.01f, 16.0f) * agcScale_;
    const std::size_t start = writeIdx_ % static_cast<std::size_t>(kSpecFft);
    for (int i = 0; i < kSpecFft; ++i) {
        const float s = ring_[(start + static_cast<std::size_t>(i)) % static_cast<std::size_t>(kSpecFft)] * totalGain;
        work_[static_cast<size_t>(i)] = std::complex<float>(s * windowSpec_[static_cast<size_t>(i)], 0.0f);
    }
    fftRadix2(work_.data(), kSpecFft);

    const float dtMs = 1000.0f * static_cast<float>(kSpecHop) / static_cast<float>(sampleRate_);
    const float rel = std::clamp(config_.releaseScale, 0.25f, 4.0f);
    for (int b = 0; b < kSpectrumBands; ++b) {
        float mx = 0.0f;
        for (int k = specBins_[static_cast<size_t>(b)].first; k <= specBins_[static_cast<size_t>(b)].second; ++k)
            mx = std::max(mx, std::abs(work_[static_cast<size_t>(k)]));
        const float amp = mx * 4.0f / static_cast<float>(kSpecFft);
        const float db = 20.0f * std::log10(amp + 1.0e-6f);
        const float target = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        float& v = spectrum_[static_cast<size_t>(b)];
        v = follow(v, target, dtMs, 0.0f, kSpecTauMs * rel);
    }
}

AudioSnapshot AudioDsp::snapshot(bool active) const
{
    AudioSnapshot s;
    s.active = active;
    s.sampleRate = active ? sampleRate_ : 0;
    s.rms = std::clamp(rms_, 0.0f, 1.0f);
    s.peak = std::clamp(peak_, 0.0f, 1.0f);
    s.bass = std::min(smBass_, 8.0f);
    s.mid = std::min(smMid_, 8.0f);
    s.high = std::min(smHigh_, 8.0f);
    s.kick = kickEnv_;
    s.beatCount = beatCount_;
    s.spectrum = spectrum_;
    return s;
}

}
