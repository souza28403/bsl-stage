#pragma once

#include "core/Generators.h"
#include "ffgl/FfglInstance.h"
#include "media/ISource.h"
#include "render/Fbo.h"
#include "render/FboPool.h"

#include <chrono>
#include <memory>
#include <string>

namespace bsl::ffgl {

class FfglSource final : public media::ISource
{
public:
    FfglSource(std::string typeId, int width, int height);
    ~FfglSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return fbo_ != nullptr; }
    void update(double dt) override;

    GLuint texture() const override { return fbo_ ? fbo_->texture() : 0; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return true; }
    bool supportsReverse() const override { return false; }

    std::vector<media::ParamDesc> paramDescs() const override { return descs_; }
    media::SourceStatus status() const override;
    bool pollParamChanges(media::ParamValues& values) override;

    FfglInstance* instance() const { return instance_.get(); }

    static const GeneratorInfo* generatorInfo(const std::string& id);

protected:
    void onParamsChanged() override;
    void onTempo(double beat, float bpm) override
    {
        beat_ = beat;
        bpm_ = bpm;
    }

private:
    std::string typeId_;
    int width_;
    int height_;
    std::vector<media::ParamDesc> descs_;
    std::unique_ptr<FfglInstance> instance_;
    std::unique_ptr<Fbo> fbo_;
    FboPool pool_;
    std::string loadError_;
    double time_ = 0.0;
    double beat_ = 0.0;
    float bpm_ = 120.0f;
    std::uint64_t frame_ = 0;
    std::chrono::steady_clock::time_point lastEdit_{};
};

}
