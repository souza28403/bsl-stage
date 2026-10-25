#pragma once

#include "media/ISource.h"
#include "media/TextRaster.h"

#include <memory>

namespace bsl {
class Fbo;
class Shader;
} // namespace bsl

namespace bsl::media {

// Gerador "Texto" (cartelas, letreiros, letras de musica). O texto e rasterizado em CPU (TextRaster) so
// quando um parametro muda e vai para uma textura "faixa"; a cada quadro a faixa e desenhada na textura da
// fonte (tamanho do canvas) com um deslocamento de UV. No letreiro a faixa tem exatamente um periodo da
// rolagem e repete (GL_REPEAT): loop continuo sem emenda e sem re-rasterizar.
class TextSource final : public ISource
{
public:
    TextSource(int width, int height);
    ~TextSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return true; }

    std::vector<ParamDesc> paramDescs() const override { return describe(values_); }
    SourceStatus status() const override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::string label(const ParamValues& values);

    // Estado para testes
    bool scrolling() const { return period_ > 0; }
    double scrollOffset() const { return scroll_; }
    int stripWidth() const { return stripW_; }
    int stripHeight() const { return stripH_; }
    int rasterCount() const { return rasterCount_; } // quantas vezes o texto foi rasterizado

protected:
    void onSeek(double seconds) override;
    void onParamsChanged() override { dirty_ = true; }

private:
    void rebuild();
    void render();
    void resetScroll();

    int width_;
    int height_;
    bool open_ = false;
    bool dirty_ = true;

    std::unique_ptr<Fbo> fbo_;
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    GLuint strip_ = 0;
    int stripW_ = 0;
    int stripH_ = 0;
    int period_ = 0; // 0 = sem letreiro
    TextMarquee marquee_ = TextMarquee::None;
    float speed_ = 0.0f; // px/s do letreiro (sinal = sentido)
    double scroll_ = 0.0;
    bool needsDraw_ = true;
    int rasterCount_ = 0;

    bool fontFallback_ = false;
    bool fontMissing_ = false;
    bool truncated_ = false;
    std::string fontName_;
};

} // namespace bsl::media
