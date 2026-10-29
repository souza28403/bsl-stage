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

// Relogio de transporte de um clip (posicao, velocidade, play/pause, modo de loop).
struct ClipClock {
    double duration = 0.0; // 0 = sem duracao (imagem estatica): o relogio nao anda
    double position = 0.0;
    double speed = 1.0;
    bool playing = true;
    LoopMode loop = LoopMode::Loop;
    int dir = 1; // +1 / -1 (ping-pong)

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

// Fonte de midia no lado GPU. Convencao da textura devolvida por texture(): sempre RGBA8 de
// alpha reta (nao premultiplicada), com v=0 na primeira linha (topo) da imagem.
// Todos os metodos devem ser chamados na thread do contexto GL.
class ISource {
public:
    virtual ~ISource() = default;

    virtual bool open(const std::filesystem::path& path, std::string* error = nullptr) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Avanca o relogio (dt em segundos reais, ja considerando speed/play/pause/loop) e atualiza a textura.
    virtual void update(double dt) = 0;

    virtual GLuint texture() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool hasAlpha() const = 0;

    // --- Transporte ---
    double duration() const { return clock_.duration; }
    double position() const { return clock_.position; }
    double speed() const { return clock_.speed; }
    bool isPlaying() const { return clock_.playing; }
    LoopMode loopMode() const { return clock_.loop; } // modo efetivo (ver supportsReverse)

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

    // false: velocidade negativa e ping-pong nao sao suportados (ping-pong vira Loop).
    virtual bool supportsReverse() const { return true; }

    // --- Parametros genericos (ver SourceParams.h) ---
    // Lista de parametros que a fonte expoe; pode depender dos valores atuais (ex.: formatos do dispositivo).
    virtual std::vector<ParamDesc> paramDescs() const { return {}; }
    // Os valores moram no Clip; a fonte recebe uma copia ao abrir (antes do open()) e a cada mudanca.
    void setParams(const ParamValues& values)
    {
        values_ = values;
        onParamsChanged();
    }
    const ParamValues& paramValues() const { return values_; }

    // Tamanho do canvas da composicao e tempo musical; a Composition repassa a cada quadro (so mudancas chamam os ganchos).
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

    // Parametros que a propria fonte mudou (ex.: gerador FFGL cujo plugin tem interface propria). A Composition chama
    // a cada quadro; grava em 'values' so o que mudou e devolve true (a fonte ja sabe desses valores).
    virtual bool pollParamChanges(ParamValues& values)
    {
        (void)values;
        return false;
    }

    // Fonte ao vivo (dispositivo/captura): exclusiva, a Previa nao abre uma segunda instancia.
    virtual bool isLive() const { return false; }
    // Situacao para o ClipPanel (ex.: "Sem sinal"). Texto vazio = tudo certo.
    virtual SourceStatus status() const { return {}; }

protected:
    virtual void onSeek(double /*seconds*/) {}
    virtual void onParamsChanged() {}
    virtual void onCanvasResized(int /*w*/, int /*h*/) {}
    virtual void onTempo(double /*beat*/, float /*bpm*/) {}
    // Leitura tipada dos valores atuais, com o padrao dos descritores
    ParamSet currentParams() const { return ParamSet(paramDescs(), values_); }

    ClipClock clock_;
    ParamValues values_;
    int canvasW_ = 0, canvasH_ = 0;
};

} // namespace bsl::media
