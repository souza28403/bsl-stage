#pragma once

// Analise de audio em CPU (sem WASAPI, testavel isoladamente): FFT radix-2, bandas graves/medios/agudos com a mesma
// matematica dos AudioCapture.cpp dos plugins FFGL do usuario, espectro em bandas logaritmicas e deteccao de batida.
//
// Alimentado com amostras mono (float -1..1) em qualquer taxa; a cada 256 amostras roda a FFT de 512 pontos das bandas
// (~180 vezes por segundo a 48 kHz) e, a cada 1024, a FFT de 2048 pontos do espectro. Nao e thread-safe: quem usa
// (AudioAnalyzer) serializa as chamadas.

#include "audio/AudioTypes.h"

#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace bsl::audio {

// FFT radix-2 no lugar; n precisa ser potencia de 2
void fftRadix2(std::complex<float>* a, int n);

class AudioDsp
{
public:
    static constexpr int kBandFft = 512;
    static constexpr int kBandHop = 256;
    static constexpr int kSpecFft = 2048;
    static constexpr int kSpecHop = 1024;
    // Ganho de referencia dos plugins (Audio Gain padrao 0,5 -> 2,1x): a deteccao de batida tem o limiar calibrado nele
    static constexpr float kKickRefGain = 2.1f;

    AudioDsp();

    void setSampleRate(int sampleRate);
    void setConfig(const AudioConfig& config) { config_ = config; }
    void reset();

    // Amostras mono (-1..1)
    void feed(const float* mono, std::size_t count);
    // Tempo sem sinal do dispositivo: equivale a alimentar zeros por 'seconds' (as bandas caem em vez de congelar)
    void feedSilence(double seconds);

    AudioSnapshot snapshot(bool active) const;

private:
    void processBands();
    void processSpectrum();

    int sampleRate_ = 48000;
    AudioConfig config_;

    std::vector<float> ring_; // kSpecFft amostras, circular
    std::size_t writeIdx_ = 0;
    int sinceBands_ = 0;
    int sinceSpec_ = 0;

    std::vector<std::complex<float>> work_;
    std::vector<float> windowBands_;
    std::vector<float> windowSpec_;

    // Faixas de bins das bandas (por taxa)
    int bassLo_ = 1, bassHi_ = 4, midLo_ = 5, midHi_ = 25, highLo_ = 30, highHi_ = 120;
    // Bandas logaritmicas do espectro: bins [lo, hi] de cada uma
    std::vector<std::pair<int, int>> specBins_;

    // AGC (acompanha o pico em ~2 s; leva o sinal fraco do navegador a um nivel util)
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

} // namespace bsl::audio
