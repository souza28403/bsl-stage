#pragma once

// Tipos do modulo de audio nativo do BSL Stage (sem dependencias de plataforma, para poder ir no EffectContext).

#include <array>
#include <cstdint>
#include <string>

namespace bsl::audio {

inline constexpr int kSpectrumBands = 32; // bandas logaritmicas do espectro (40 Hz a 16 kHz)

// Uma "foto" da analise. Todos os valores sao zero quando nao ha dispositivo (nunca ha erro).
// As bandas graves/medios/agudos estao em ganho unitario (depois do AGC), com a suavizacao de ataque/release da
// configuracao global; cada efeito aplica o proprio ganho e noise gate por cima (como faziam os plugins FFGL).
struct AudioSnapshot
{
    bool active = false;           // capturando de verdade
    int sampleRate = 0;
    float rms = 0.0f;              // 0..1: nivel RMS do sinal bruto (sem AGC), suavizado
    float peak = 0.0f;             // 0..1: pico do sinal bruto, com queda lenta
    float bass = 0.0f;             // ~93-375 Hz   (0..~4; em ganho 1x e AGC)
    float mid = 0.0f;              // ~470-2340 Hz
    float high = 0.0f;             // ~2800-11250 Hz
    float kick = 0.0f;             // pulso de onset dos graves (0..1,5), decai em ~100 ms
    std::uint32_t beatCount = 0;   // soma 1 a cada batida detectada
    std::array<float, kSpectrumBands> spectrum{}; // 0..1 (-60 dB a 0 dB), bandas logaritmicas
};

enum class AudioSource { Loopback, Input };

// Configuracao global (Ver > Audio). Gravada em settings.json.
struct AudioConfig
{
    bool enabled = true;
    AudioSource source = AudioSource::Loopback; // som do PC (padrao) ou entrada (microfone/placa)
    std::string deviceId;                       // "" = automatico (loopback: a saida que esta tocando)
    float gain = 1.0f;                          // 0,1 a 4 (multiplicador geral, antes dos ganhos de cada efeito)
    float gate = 0.0f;                          // 0 a 0,15 (noise gate geral das bandas)
    float attackMs = 0.0f;                      // 0 a 200: tempo de subida das bandas (0 = instantaneo)
    float releaseScale = 1.0f;                  // 0,25 a 4: multiplica o tempo de queda das bandas

    bool operator==(const AudioConfig& o) const
    {
        return enabled == o.enabled && source == o.source && deviceId == o.deviceId && gain == o.gain && gate == o.gate &&
               attackMs == o.attackMs && releaseScale == o.releaseScale;
    }
    bool operator!=(const AudioConfig& o) const { return !(*this == o); }
};

struct AudioDeviceInfo
{
    std::string id;   // id do endpoint WASAPI (UTF-8)
    std::string name; // nome amigavel (UTF-8)
};

} // namespace bsl::audio
