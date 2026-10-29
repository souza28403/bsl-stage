#pragma once

// Analisador de audio nativo, compartilhado por todo o app: captura WASAPI em thread propria (loopback = som do PC,
// ou entrada = microfone/placa) e alimenta o AudioDsp. Os efeitos leem snapshot() (copiado para EffectContext::audio).
// Sem dispositivo, ou com o audio desligado, tudo fica em zero e nada acusa erro.

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

    // Liga a thread de captura (idempotente). O app chama uma vez; os testes nao precisam (snapshot fica zerado).
    void start();
    void stop();
    bool running() const;

    void setConfig(const AudioConfig& config);
    AudioConfig config() const;

    // Thread-safe, barato (copia de ~200 bytes)
    AudioSnapshot snapshot() const;

    // Texto de estado para a UI: nome do dispositivo capturando ou o motivo de nao estar capturando
    std::string statusText() const;

    // Testes: devolve este snapshot no lugar da captura (nullopt volta ao normal)
    void setOverride(const std::optional<AudioSnapshot>& snapshot);

    // Dispositivos para o menu (nova enumeracao a cada chamada; chame so quando o usuario pedir)
    static std::vector<AudioDeviceInfo> listDevices(AudioSource source);

    ~AudioAnalyzer();
    AudioAnalyzer(const AudioAnalyzer&) = delete;
    AudioAnalyzer& operator=(const AudioAnalyzer&) = delete;

private:
    AudioAnalyzer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bsl::audio
