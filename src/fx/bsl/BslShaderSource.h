#pragma once

// Base das fontes procedurais BSL (um passe, sem imagem de entrada): desenha um fragment shader no tamanho do canvas.
// O prelude entrega glUv() com y para CIMA, como o OpenGL dos plugins FFGL originais.

#include "fx/FxGl.h"
#include "media/ISource.h"
#include "render/Fbo.h"
#include "render/Shader.h"

#include <memory>
#include <string>

namespace bsl::fx::bsllight {

class BslShaderSource : public media::ISource
{
public:
    BslShaderSource(int width, int height) : width_(width), height_(height) {}
    ~BslShaderSource() override { close(); }

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return fbo_ != nullptr; }
    void update(double dt) override;

    GLuint texture() const override { return fbo_ ? fbo_->texture() : 0; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return false; }
    bool supportsReverse() const override { return false; }
    media::SourceStatus status() const override;

protected:
    // Corpo do fragment shader depois do prelude (declara os uniforms proprios e o main)
    virtual std::string fragmentBody() const = 0;
    // Com o shader ligado: define os uniforms proprios. 'time' = segundos de reproducao do clip.
    virtual void setUniforms(Shader& shader, double time) = 0;
    // Depois de abrir (valores iniciais ja entregues)
    virtual void onOpened() {}
    virtual std::string extraStatus() const { return {}; }

    int width_;
    int height_;

private:
    void render();

    std::unique_ptr<Fbo> fbo_;
    std::unique_ptr<Shader> shader_;
    std::unique_ptr<ScreenVao> vao_;
    std::string error_;
    double time_ = 0.0;
};

extern const char* const kBslSourcePrelude;

} // namespace bsl::fx::bsllight
