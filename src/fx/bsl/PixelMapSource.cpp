// Padrao de calibracao BSL: gera ao vivo, no canvas, o padrao HD de cada painel do Pixel Map dentro do seu retangulo
// escalado (a mesma conta da tabela e do "Criar mapeamento"). Com o mapeamento criado, o operador poe a fonte na grade e
// o padrao certo aparece no painel fisico real. Opcionalmente anima uma barra que corre e um contador de quadros para
// ver frame drop e travamento.
//
// O desenho e feito em CPU (output/PatternRaster) e enviado como textura; o padrao de cada painel fica em cache e so e
// refeito quando o painel, a escala ou a marca mudam.

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslSources.h"
#include "fx/bsl/PixelMapFeed.h"

#include "app/Log.h"
#include "output/PatternRaster.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace bsl::fx::bsllight {

PixelMapFeed& PixelMapFeed::instance()
{
    static PixelMapFeed feed;
    return feed;
}

void PixelMapFeed::set(const pixelmap::Config& config)
{
    std::lock_guard lk(mutex_);
    if (config == config_)
        return;
    config_ = config;
    ++version_;
}

pixelmap::Config PixelMapFeed::config() const
{
    std::lock_guard lk(mutex_);
    return config_;
}

std::uint64_t PixelMapFeed::version() const
{
    std::lock_guard lk(mutex_);
    return version_;
}

namespace {

using namespace media::param;
namespace pm = pixelmap;

struct Tile
{
    std::string key;
    pm::Image image; // padrao ja reduzido/ampliado para o retangulo do painel no canvas
    bool used = false;
};

class PixelMapPatternSource final : public media::ISource
{
public:
    PixelMapPatternSource(int w, int h) : width_(std::max(w, 1)), height_(std::max(h, 1)) {}
    ~PixelMapPatternSource() override { close(); }

    bool open(const std::filesystem::path&, std::string*) override
    {
        close();
        glGenTextures(1, &tex_);
        allocate();
        clock_ = media::ClipClock{};
        time_ = 0.0;
        frames_ = 0;
        dirty_ = true;
        render();
        return tex_ != 0;
    }

    void close() override
    {
        if (tex_) {
            glDeleteTextures(1, &tex_);
            tex_ = 0;
        }
        tiles_.clear();
    }

    bool isOpen() const override { return tex_ != 0; }

    void update(double dt) override
    {
        if (!tex_)
            return;
        if (clock_.playing)
            time_ += dt * clock_.speed;
        ++frames_;
        render();
    }

    GLuint texture() const override { return tex_; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return false; }
    bool supportsReverse() const override { return false; }
    std::vector<media::ParamDesc> paramDescs() const override { return describePixelMapPattern(); }

    media::SourceStatus status() const override
    {
        if (!pm::fontsAvailable())
            return {media::SourceStatus::Level::Warning, "Fontes não encontradas em assets/fonts: o padrão sai sem textos."};
        if (panelCount_ == 0)
            return {media::SourceStatus::Level::Info, "Sem painéis: adicione no modo Pixel Map."};
        return {};
    }

protected:
    void onParamsChanged() override { dirty_ = true; }

    void onCanvasResized(int w, int h) override
    {
        width_ = std::max(w, 1);
        height_ = std::max(h, 1);
        if (tex_)
            allocate();
        dirty_ = true;
    }

private:
    void allocate()
    {
        GLint prev = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
    }

    // Cria a imagem base (todos os padroes nos retangulos escalados) quando a configuracao, o canvas ou a marca mudam
    void rebuildBase(const media::ParamSet& p)
    {
        pm::Config cfg = PixelMapFeed::instance().config();
        cfg.inW = width_; // o desenho vale para o canvas real (igual ao "Criar mapeamento")
        cfg.inH = height_;
        smooth_ = p.b("smooth");

        const std::string wmKey = std::to_string(static_cast<int>(cfg.watermark)) + "|" + cfg.watermarkPath;
        if (wmKey != wmKey_) {
            wmKey_ = wmKey;
            logo_ = pm::Image();
            hasLogo_ = pm::loadWatermark(cfg, logo_);
            ++logoGen_;
            tiles_.clear();
        }

        for (Tile& t : tiles_)
            t.used = false;
        base_ = pm::Image(width_, height_, 0x000000);
        panelCount_ = static_cast<int>(cfg.panels.size());
        rects_.clear();
        for (const pm::Panel& panel : cfg.panels) {
            const RectI r = pm::scaledRectRounded(cfg, panel);
            rects_.push_back(r);
            if (r.w < 1 || r.h < 1 || r.x >= width_ || r.y >= height_ || r.x + r.w <= 0 || r.y + r.h <= 0)
                continue;
            const std::string key = panel.name + "|" + std::to_string(panel.x) + "|" + std::to_string(panel.y) + "|"
                                    + std::to_string(panel.w) + "|" + std::to_string(panel.h) + "|" + std::to_string(r.w) + "|"
                                    + std::to_string(r.h) + "|" + (smooth_ ? "s" : "n") + "|" + std::to_string(logoGen_);
            Tile* tile = nullptr;
            for (Tile& t : tiles_)
                if (t.key == key)
                    tile = &t;
            if (!tile) {
                const pm::Image pattern = pm::renderPattern(panel, hasLogo_ ? &logo_ : nullptr);
                Tile t;
                t.key = key;
                t.image = pm::Image(r.w, r.h, 0x000000);
                if (!pattern.empty()) {
                    pm::Surface s(t.image);
                    s.drawImage(pattern, 0.0, 0.0, r.w, r.h, 1.0f, smooth_);
                }
                tiles_.push_back(std::move(t));
                tile = &tiles_.back();
            }
            tile->used = true;
            blit(tile->image, r.x, r.y);
        }
        std::erase_if(tiles_, [](const Tile& t) { return !t.used; });
    }

    void blit(const pm::Image& src, int dx, int dy)
    {
        const int x0 = std::max(0, dx), x1 = std::min(width_, dx + src.w);
        const int y0 = std::max(0, dy), y1 = std::min(height_, dy + src.h);
        if (x1 <= x0)
            return;
        for (int y = y0; y < y1; ++y)
            std::memcpy(base_.px(x0, y), src.px(x0 - dx, y - dy), static_cast<size_t>(x1 - x0) * 4);
    }

    void animate(const media::ParamSet& p)
    {
        const bool bar = p.b("bar");
        const bool counter = p.b("counter");
        if (!bar && !counter)
            return;
        const pm::Config cfg = PixelMapFeed::instance().config();
        pm::Surface s(frame_);
        const double phase = std::fmod(time_ * static_cast<double>(p.f("speed")), 1.0);
        for (size_t i = 0; i < rects_.size() && i < cfg.panels.size(); ++i) {
            const RectI r = rects_[i];
            if (r.w < 1 || r.h < 1)
                continue;
            if (bar) {
                const double bw = std::max(3.0, r.w * 0.02);
                const double x = r.x + phase * (r.w - bw);
                s.fillRect(x, r.y, bw, r.h, pm::rgb(0xFFFFFF));
            }
            if (counter && r.w >= 70 && r.h >= 24) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "F %06d  %.2fs", static_cast<int>(frames_ % 1000000), time_);
                const double size = std::clamp(r.h * 0.14, 10.0, 26.0);
                const double tw = pm::textWidth(buf, size, pm::FontId::Mono);
                const double pad = size * 0.4;
                const double bx = r.x + 6.0, by = r.y + 6.0;
                if (tw + pad * 2.0 > r.w - 12.0)
                    continue;
                s.fillRect(bx, by, tw + pad * 2.0, size + pad * 2.0, pm::rgb(0x000000, 0.8f));
                s.strokeRect(bx, by, tw + pad * 2.0, size + pad * 2.0, 1.0, pm::rgb(0xFF0A33));
                s.text(buf, bx + pad + tw * 0.5, by + pad + size * 0.5, size, pm::rgb(0xFFFFFF), pm::FontId::Mono);
            }
        }
    }

    void render()
    {
        const media::ParamSet p = currentParams();
        const std::uint64_t version = PixelMapFeed::instance().version();
        if (dirty_ || version != feedVersion_ || base_.w != width_ || base_.h != height_) {
            feedVersion_ = version;
            dirty_ = false;
            rebuildBase(p);
            frame_ = base_;
            haveFrame_ = false;
        }
        const bool animated = p.b("bar") || p.b("counter");
        if (animated) {
            frame_.rgba = base_.rgba;
            animate(p);
        } else if (haveFrame_) {
            return; // imagem estatica: nada a enviar
        }
        haveFrame_ = !animated;

        GLint prev = 0, prevAlign = 4;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, frame_.rgba.data());
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    }

    int width_;
    int height_;
    GLuint tex_ = 0;
    double time_ = 0.0;
    long long frames_ = 0;

    bool dirty_ = true;
    bool haveFrame_ = false;
    bool smooth_ = true;
    std::uint64_t feedVersion_ = 0;
    int panelCount_ = 0;
    std::string wmKey_ = "?";
    pm::Image logo_;
    bool hasLogo_ = false;
    int logoGen_ = 0;
    pm::Image base_;
    pm::Image frame_;
    std::vector<RectI> rects_;
    std::vector<Tile> tiles_;
};

} // namespace

std::vector<media::ParamDesc> describePixelMapPattern()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeBool("smooth", "Filtro suave (bilinear)", true, "Imagem"));
    d.back().tooltip = "Como o padrão HD é reduzido/ampliado para o retângulo do painel no canvas. Desligado, copia o pixel "
                       "mais próximo (mostra o serrilhado de uma escala não inteira).";
    d.push_back(makeBool("bar", "Barra que corre", false, "Animação (frame drop e travamento)"));
    d.back().tooltip = "Uma barra branca varre cada painel: qualquer engasgo na saída aparece como salto da barra.";
    d.push_back(makeFloat("speed", "Velocidade da barra", 0.5f, 0.05f, 4.0f, "%.2f painel/s", "Animação (frame drop e travamento)"));
    d.back().enabledIf = "bar";
    d.push_back(makeBool("counter", "Contador de quadros", false, "Animação (frame drop e travamento)"));
    d.back().tooltip = "Número do quadro e tempo em cada painel: dá para ver quadros repetidos ou pulados.";
    return d;
}

std::unique_ptr<media::ISource> makePixelMapPattern(int canvasW, int canvasH)
{
    return std::make_unique<PixelMapPatternSource>(canvasW, canvasH);
}

} // namespace bsl::fx::bsllight
