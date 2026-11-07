#pragma once

#include "media/ISource.h"

#include <memory>

namespace bsl::media {

enum class CaptureBackend { Auto, WindowsGraphicsCapture, DesktopDuplication };

class ScreenCaptureSource final : public ISource
{
public:
    ScreenCaptureSource();
    ~ScreenCaptureSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override;
    int height() const override;
    bool hasAlpha() const override { return false; }
    bool supportsReverse() const override { return false; }

    std::vector<ParamDesc> paramDescs() const override { return describe(values_); }
    bool isLive() const override { return true; }
    SourceStatus status() const override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::string label(const ParamValues& values);

    std::string backendName() const;
    std::string pathName() const;
    bool usesInterop() const;
    unsigned long long frameCount() const;
    static void forceBackend(CaptureBackend backend);
    static void forceCpuPath(bool cpu);

protected:
    void onParamsChanged() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool open_ = false;
};

}
