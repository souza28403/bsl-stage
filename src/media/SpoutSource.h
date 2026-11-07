#pragma once

#include "media/ISource.h"
#include "media/SpoutInput.h"

#include <chrono>
#include <memory>

namespace bsl::media {

class SpoutSource final : public ISource
{
public:
    SpoutSource();
    ~SpoutSource() override;

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

    static std::vector<std::string> activeSenders();

    bool connected() const { return input_.connected(); }
    unsigned long long frameCount() const { return input_.frames(); }

protected:
    void onParamsChanged() override { paramsDirty_ = true; }

private:
    void showBlack();

    bool open_ = false;
    bool paramsDirty_ = false;
    SpoutInput input_;
    std::string wanted_;
    GLuint blackTex_ = 0;
    int blackW_ = 0, blackH_ = 0;
    int width_ = 1280;
    int height_ = 720;
};

}
