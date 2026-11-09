#pragma once

#include "media/ISource.h"
#include "media/NetStream.h"
#include "media/YuvConverter.h"

#include <chrono>
#include <memory>

namespace bsl::media {

class NetStreamSource final : public ISource
{
public:
    NetStreamSource();
    ~NetStreamSource() override;

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
    static net::Config configFrom(const ParamValues& values);

    net::Status streamStatus() const { return worker_.status(); }
    unsigned long long frameCount() const { return frames_; }

protected:
    void onParamsChanged() override;

private:
    void startWorker();
    void present(const Frame& f);
    void showBlack();

    bool open_ = false;
    bool paramsDirty_ = false;
    std::chrono::steady_clock::time_point dirtyAt_{};
    net::StreamWorker worker_;
    net::Config appliedConfig_;
    bool holdLast_ = true;
    bool showingBlack_ = true;
    unsigned long long frames_ = 0;

    Frame incoming_;
    std::unique_ptr<YuvConverter> yuv_;
    GLuint rgbaTex_ = 0;
    int rgbaW_ = 0, rgbaH_ = 0;
    bool usingYuv_ = false;
    int width_ = 1280;
    int height_ = 720;
};

}
