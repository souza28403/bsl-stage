#pragma once

#include "media/ISource.h"

#include <chrono>
#include <memory>

namespace bsl {
class Fbo;
class Shader;
namespace ndi {
class Runtime;
}
} // namespace bsl

namespace bsl::media {

// Fonte NDI® (recebe vídeo de outra máquina/programa pela rede). O receptor roda sem bloquear: a cada quadro do app
// a fonte pega o que já chegou (timeout 0), sobe para a GPU e, se for UYVY, converte para RGBA num shader.
// Reconecta sozinha se a fonte some e volta. Sem o runtime do NDI mostra preto + "NDI Runtime não encontrado".
class NdiSource final : public ISource
{
public:
    explicit NdiSource(ndi::Runtime* runtime = nullptr); // nulo = Runtime::instance()
    ~NdiSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return hasAlpha_; }
    bool supportsReverse() const override { return false; }

    std::vector<ParamDesc> paramDescs() const override { return describe(values_); }
    bool isLive() const override { return true; }
    SourceStatus status() const override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::string label(const ParamValues& values);

    // Diagnóstico / testes
    enum class State { NoRuntime, NoSource, Connecting, Live, NoSignal };
    State state() const { return state_; }
    unsigned long long frameCount() const { return frames_; }

protected:
    void onParamsChanged() override { paramsDirty_ = true; }

private:
    void connect();
    void disconnect();
    void showBlack();
    void presentUyvy(const unsigned char* data, int stride, int w, int h);
    void presentPacked(const unsigned char* data, int stride, int w, int h, unsigned int format, bool opaque);

    ndi::Runtime* runtime_ = nullptr;
    bool open_ = false;
    bool paramsDirty_ = false;
    State state_ = State::NoSource;

    struct Recv; // receptor NDI
    std::unique_ptr<Recv> recv_;
    std::string sourceName_;
    bool lowBandwidth_ = false;
    bool lowLatency_ = false;
    std::string info_; // "1920 x 1080 @ 60 fps | UYVY"
    bool sourceListed_ = true;

    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point connectedAt_{};
    std::chrono::steady_clock::time_point lastCheck_{};
    unsigned long long frames_ = 0;
    bool everLive_ = false;

    // GPU
    GLuint rgbaTex_ = 0;   // BGRA/RGBA direto (e o preto inicial)
    int rgbaW_ = 0, rgbaH_ = 0;
    bool opaqueSwizzle_ = false;
    GLuint packedTex_ = 0; // UYVY empacotado: (w/2) x h texels RGBA8
    int packedW_ = 0, packedH_ = 0;
    std::unique_ptr<Fbo> fbo_;
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    bool usingFbo_ = false;
    bool hasAlpha_ = false;
    int width_ = 1280;
    int height_ = 720;
};

} // namespace bsl::media
