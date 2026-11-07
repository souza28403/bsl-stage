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
}

namespace bsl::media {

class NdiSource final : public ISource
{
public:
    explicit NdiSource(ndi::Runtime* runtime = nullptr);
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

    struct Recv;
    std::unique_ptr<Recv> recv_;
    std::string sourceName_;
    bool lowBandwidth_ = false;
    bool lowLatency_ = false;
    std::string info_;
    bool sourceListed_ = true;

    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point connectedAt_{};
    std::chrono::steady_clock::time_point lastCheck_{};
    unsigned long long frames_ = 0;
    bool everLive_ = false;

    GLuint rgbaTex_ = 0;
    int rgbaW_ = 0, rgbaH_ = 0;
    bool opaqueSwizzle_ = false;
    GLuint packedTex_ = 0;
    int packedW_ = 0, packedH_ = 0;
    std::unique_ptr<Fbo> fbo_;
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    bool usingFbo_ = false;
    bool hasAlpha_ = false;
    int width_ = 1280;
    int height_ = 720;
};

}
