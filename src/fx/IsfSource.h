#pragma once

// Gerador ISF: shader sem entrada de imagem, desenhado no tamanho do canvas. Uma ISource como as outras geradoras;
// o id do clip e "isf:<caminho relativo a pasta ISF>".

#include "core/Generators.h"
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

    // Registro de geradores (ver core/Generators): informacao do gerador "isf:..." (criada sob demanda, nunca liberada)
    static const GeneratorInfo* generatorInfo(const std::string& id);

protected:
    void onParamsChanged() override;
    void onTempo(double beat, float bpm) override
    {
        beat_ = beat;
        bpm_ = bpm;
    }

private:
    void refreshDoc(); // "Recarregar": o arquivo mudou no disco, o shader recompila sem fechar o clip

    std::string id_;
    int width_;
    int height_;
    std::vector<media::ParamDesc> descs_;
    std::unique_ptr<IsfRenderer> renderer_;
    std::unique_ptr<Fbo> fbo_;
    std::string loadError_;
    std::uint64_t seenVersion_ = 0;
    double beat_ = 0.0;
    float bpm_ = 120.0f;
    std::uint64_t frame_ = 0;
};

} // namespace bsl::fx
