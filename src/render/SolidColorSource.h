#pragma once

#include "media/ISource.h"
#include "render/Fbo.h"

#include <memory>

namespace bsl {

// Gerador "Cor solida": o canvas inteiro numa cor RGBA (alpha reta). So redesenha quando a cor muda.
class SolidColorSource final : public media::ISource
{
public:
    SolidColorSource(int width, int height);
    ~SolidColorSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return fbo_ != nullptr; }
    void update(double dt) override;

    GLuint texture() const override { return fbo_ ? fbo_->texture() : 0; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return alpha_ < 1.0f; }

    std::vector<media::ParamDesc> paramDescs() const override { return describe(values_); }
    static std::vector<media::ParamDesc> describe(const media::ParamValues& values);

protected:
    void onParamsChanged() override { dirty_ = true; }

private:
    void render();

    int width_;
    int height_;
    float alpha_ = 1.0f;
    bool dirty_ = true;
    std::unique_ptr<Fbo> fbo_;
};

} // namespace bsl
