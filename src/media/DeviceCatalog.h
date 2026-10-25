#pragma once

// Dispositivos de captura de video (DirectShow via FFmpeg/avdevice) e os formatos que cada um oferece.
// A enumeracao e feita em threads de fundo (abrir o dshow leva de centenas de ms a segundos); a UI so
// consulta o que ja foi lido.

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bsl::media {

struct CaptureFormat
{
    int width = 0;
    int height = 0;
    double fpsMin = 0.0;
    double fpsMax = 0.0;
    std::string pixelFormat; // "yuyv422", "nv12"... (vazio se for codec)
    std::string codec;       // "mjpeg", "h264"... (vazio se for pixel_format)
};

// Formato escolhido: "WxH@F:formato" (formato = pixel_format ou codec); "auto" = o melhor
struct ChosenFormat
{
    bool automatic = true;
    int width = 0, height = 0;
    int fps = 0;
    std::string format;
    bool isCodec = false;
};

class DeviceCatalog
{
public:
    static DeviceCatalog& instance();
    ~DeviceCatalog() = default;

    // Dispositivos de video (nome amigavel). Inicia a primeira leitura sozinho.
    void ensureListed();
    void refresh();           // le de novo (em segundo plano)
    bool listing() const;
    bool listed() const;
    std::vector<std::string> deviceNames() const;

    // Formatos de um dispositivo (le em segundo plano na primeira vez)
    void ensureFormats(const std::string& device);
    bool formatsReady(const std::string& device) const;
    std::vector<CaptureFormat> formats(const std::string& device) const;
    // Bloqueia ate ler (usar so em thread de fundo). Dispositivo em uso/sem video: lista vazia.
    std::vector<CaptureFormat> formatsBlocking(const std::string& device);

    // Interpretacao do log do dshow (-list_options) e escolha de formato: testaveis sem dispositivo
    static std::vector<CaptureFormat> parseFormats(const std::string& log);
    static std::string formatValue(const CaptureFormat& f, double fps);
    static ChosenFormat parseValue(const std::string& value);
    // O melhor formato automatico: ate 1080p, >= 25 fps, maior area; empate prefere mjpeg
    static ChosenFormat pickBest(const std::vector<CaptureFormat>& formats);

private:
    DeviceCatalog() = default;
    void readDevices();
    void readFormats(const std::string& device);

    mutable std::mutex m_;
    std::mutex avMutex_; // um acesso ao dshow por vez
    bool started_ = false;
    bool listing_ = false;
    bool listed_ = false;
    std::vector<std::string> names_;
    struct Caps
    {
        bool ready = false;
        bool pending = false;
        std::vector<CaptureFormat> formats;
    };
    std::map<std::string, Caps> caps_;
};

} // namespace bsl::media
