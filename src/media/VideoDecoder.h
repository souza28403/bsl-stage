#pragma once

#include "media/Frame.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bsl::media {

struct VideoInfo {
    std::string codec;        // ex.: "h264", "vp9"
    std::string decoderName;  // ex.: "h264", "libvpx-vp9"
    std::string pixelFormat;  // formato nativo do stream, ex.: "yuv420p"
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration = 0.0;    // segundos (0 = desconhecida)
    bool hasAlpha = false;
};

struct VideoDecoderOptions {
    // Perfil Eco: desliga o loop filter e reduz o video ao tamanho alvo (se maior).
    bool lowPower = false;
    int targetWidth = 0;
    int targetHeight = 0;
    int queueSize = 4;        // frames prontos a frente do relogio

    // Modo ao vivo (webcam/placa de captura): sem duracao, seek nem loop; latencia baixa (fflags nobuffer,
    // decoder sem frame threading) e a fila descarta os frames mais antigos em vez de bloquear o dispositivo.
    // Use tryGetLatest() para consumir.
    bool live = false;
    std::string inputFormat;  // demuxer/dispositivo do avdevice, ex.: "dshow" (vazio = detectar pelo arquivo)
    std::vector<std::pair<std::string, std::string>> inputOptions; // AVOptions do demuxer (video_size, framerate, ...)
};

// Decodifica um video em thread propria e entrega frames CPU.
// Formatos yuv420p/yuva420p saem em planos (conversao fica no shader); os demais viram RGBA8.
// Metodos publicos sao thread-safe em relacao a thread interna (um unico consumidor externo).
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const std::filesystem::path& path, const VideoDecoderOptions& options = {});
    void close();
    bool isOpen() const;

    const VideoInfo& info() const;
    const std::string& error() const; // motivo da ultima falha de open()

    // Salta para 'seconds' (exato: frames anteriores ao alvo sao descartados na decodificacao).
    void seek(double seconds);

    // Quando ativo, ao fim do arquivo a thread volta ao inicio sem pausa (loop sem engasgo).
    // O consumidor percebe a volta quando clipTime "pula para tras" (mais de metade da duracao).
    void setLoop(bool loop);

    // Retorna o frame que deve estar na tela em 'clipTime' (o mais novo com pts <= clipTime),
    // descartando os atrasados. nullptr = manter o frame anterior. Apos open()/seek() o primeiro
    // frame e entregue mesmo que ainda nao esteja "vencido" (para mostrar algo em pausa).
    std::unique_ptr<Frame> tryGetFrame(double clipTime);

    // Modo ao vivo: o frame mais novo da fila (descarta os mais antigos). nullptr = nada novo.
    std::unique_ptr<Frame> tryGetLatest();

    // Modo vazao (thumbnails/testes): bloqueia ate o proximo frame da fila, ignorando o relogio.
    // nullptr em timeout ou fim de arquivo.
    std::unique_ptr<Frame> nextFrame(int timeoutMs);

    // true se chegou ao fim (sem loop) e nao ha mais frames na fila.
    bool atEnd() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bsl::media
