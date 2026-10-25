#pragma once

#include "media/ISource.h"
#include "render/Fbo.h"
#include "render/Shader.h"

#include <memory>

namespace bsl {

// Gerador "Padrao de teste": barras de cor, rampa, grade e um circulo em movimento,
// desenhado numa textura propria. Segue a convencao do ISource (v=0 = topo da imagem).
class TestPatternSource final : public media::ISource
{
public:
    TestPatternSource(int width, int height);
    ~TestPatternSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return fbo_ != nullptr; }
    void update(double dt) override;

    GLuint texture() const override { return fbo_ ? fbo_->texture() : 0; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return false; }

private:
    void render();

    int width_;
    int height_;
    double time_ = 0.0;
    std::unique_ptr<Fbo> fbo_;
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
};

} // namespace bsl
