#pragma once

#include "media/ISource.h"
#include "media/VideoDecoder.h"
#include "media/YuvConverter.h"

#include <chrono>
#include <memory>

namespace bsl::media {

// Placa de captura / webcam (DirectShow via FFmpeg "dshow"), reaproveitando o VideoDecoder em modo ao vivo.
// Abrir o dispositivo acontece numa thread de fundo: a UI nunca trava. Se o dispositivo some (ou nao
// existe), a fonte mostra preto + "Sem sinal" e tenta reabrir a cada 2 s.
class CaptureDeviceSource final : public ISource
{
public:
    CaptureDeviceSource();
    ~CaptureDeviceSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return false; }
    bool supportsReverse() const override { return false; }

    std::vector<ParamDesc> paramDescs() const override { return describe(values_); }
    bool isLive() const override { return true; }
    SourceStatus status() const override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::string label(const ParamValues& values);

    // Diagnostico / testes
    enum class State { Connecting, Live, NoSignal };
    State state() const { return state_; }
    unsigned long long frameCount() const { return frames_; }
    const std::string& lastError() const { return lastError_; }

protected:
    void onParamsChanged() override { paramsDirty_ = true; }

private:
    struct Pending; // resultado de uma abertura em andamento (thread de fundo)

    void startConnect();
    void applyDevice();
    void present(const Frame& f);
    void showBlack();
    void dropDecoder();
    std::string deviceName() const;

    bool open_ = false;
    bool paramsDirty_ = true;
    State state_ = State::NoSignal;
    std::string lastError_;
    std::string device_;      // dispositivo em uso
    std::string formatValue_; // formato pedido ("auto" ou "WxH@F:fmt")
    std::string openedInfo_;  // descricao do que foi aberto

    std::unique_ptr<VideoDecoder> decoder_;
    std::shared_ptr<Pending> pending_;
    std::chrono::steady_clock::time_point stateSince_{};
    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point nextRetry_{};
    unsigned long long frames_ = 0;
    unsigned long long framesThisConnection_ = 0;

    std::unique_ptr<YuvConverter> yuv_;
    GLuint rgbaTex_ = 0;
    int rgbaW_ = 0, rgbaH_ = 0;
    bool usingYuv_ = false;
    int width_ = 1280;
    int height_ = 720;
};

} // namespace bsl::media
