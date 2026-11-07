#pragma once

#include "core/Generators.h"
#include "fx/IsfReload.h"
#include "fx/IsfRenderer.h"
#include "media/ISource.h"
#include "render/Fbo.h"

#include <memory>
#include <string>

namespace bsl::fx {

class IsfSource final : public media::ISource
{
public:
    IsfSource(std::string id, int width, int height);
    ~IsfSource() override;

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

    static const GeneratorInfo* generatorInfo(const std::string& id);

protected:
    void onParamsChanged() override;
    void onTempo(double beat, float bpm) override
    {
        beat_ = beat;
        bpm_ = bpm;
    }

private:
    void refreshDoc();

    std::string id_;
    int width_;
    int height_;
    std::vector<media::ParamDesc> descs_;
    std::unique_ptr<IsfRenderer> renderer_;
    std::unique_ptr<Fbo> fbo_;
    std::string loadError_;
    IsfReloadState reload_;
    std::uint64_t seenVersion_ = 0;
    double beat_ = 0.0;
    float bpm_ = 120.0f;
    std::uint64_t frame_ = 0;
};

}
