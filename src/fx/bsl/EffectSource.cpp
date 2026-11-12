#include "fx/bsl/EffectSource.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/BslSources.h"

#include <algorithm>
#include <cmath>

namespace bsl::fx::bsllight {

namespace {

class EffectSource final : public media::ISource
{
public:
    EffectSource(std::unique_ptr<IEffect> efeito, std::vector<media::ParamDesc> descs, int w, int h)
        : efeito_(std::move(efeito)), descs_(std::move(descs)), width_(std::max(w, 1)), height_(std::max(h, 1))
    {
    }

    ~EffectSource() override { close(); }

    bool open(const std::filesystem::path&, std::string* error) override
    {
        close();
        try {
            entrada_ = std::make_unique<Fbo>(width_, height_);
            saida_ = std::make_unique<Fbo>(width_, height_);
        } catch (const std::exception& e) {
            entrada_.reset();
            saida_.reset();
            if (error)
                *error = e.what();
            return false;
        }
        entrada_->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        Fbo::unbind();
        clock_ = media::ClipClock{};
        tempo_ = 0.0;
        quadro_ = 0;
        efeito_->setParams(currentParams());
        update(0.0);
        return true;
    }

    void close() override
    {
        entrada_.reset();
        saida_.reset();
    }

    bool isOpen() const override { return saida_ != nullptr; }

    void update(double dt) override
    {
        if (!saida_)
            return;
        const double passo = clock_.playing ? dt * clock_.speed : 0.0;
        tempo_ += passo;
        EffectContext ctx;
        ctx.time = tempo_;
        ctx.dt = passo;
        ctx.frameIndex = quadro_++;
        ctx.bpm = bpm_;
        ctx.beat = beat_;
        ctx.beatPhase = static_cast<float>(beat_ - std::floor(beat_));
        ctx.barPhase = static_cast<float>(std::fmod(beat_, 4.0) / 4.0);
        ctx.canvasWidth = width_;
        ctx.canvasHeight = height_;
        ctx.topOrigin = true;
        efeito_->process(ctx, entrada_->texture(), width_, height_, *saida_);
    }

    GLuint texture() const override { return saida_ ? saida_->texture() : 0; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return true; }
    bool supportsReverse() const override { return false; }
    std::vector<media::ParamDesc> paramDescs() const override { return descs_; }
    bool pollParamChanges(media::ParamValues& values) override { return efeito_->pollParamChanges(values); }
    bool isLive() const override { return efeito_->isLive(); }
    media::SourceStatus status() const override { return efeito_->status(); }

protected:
    void onParamsChanged() override { efeito_->setParams(currentParams()); }

    void onTempo(double beat, float bpm) override
    {
        beat_ = beat;
        bpm_ = bpm;
    }

private:
    std::unique_ptr<IEffect> efeito_;
    std::vector<media::ParamDesc> descs_;
    int width_;
    int height_;
    std::unique_ptr<Fbo> entrada_;
    std::unique_ptr<Fbo> saida_;
    double tempo_ = 0.0;
    std::uint64_t quadro_ = 0;
    double beat_ = 0.0;
    float bpm_ = 120.0f;
};

}

std::unique_ptr<media::ISource> makeEffectSource(std::unique_ptr<IEffect> efeito, std::vector<media::ParamDesc> descs,
                                                 int canvasW, int canvasH)
{
    return std::make_unique<EffectSource>(std::move(efeito), std::move(descs), canvasW, canvasH);
}

std::vector<media::ParamDesc> descreverGerador(std::vector<media::ParamDesc> descs)
{
    for (media::ParamDesc& d : descs)
        if (d.id == "fundo" && d.type == media::ParamType::Enum)
            d.def = "transparente";
    return descs;
}

std::vector<media::ParamDesc> describeGeradorContorno()
{
    return descreverGerador(describeContorno());
}

std::unique_ptr<media::ISource> makeGeradorContorno(int canvasW, int canvasH)
{
    return makeEffectSource(makeContorno(), describeGeradorContorno(), canvasW, canvasH);
}

std::vector<media::ParamDesc> describeGeradorLadrilhos()
{
    return descreverGerador(describeLadrilhos());
}

std::unique_ptr<media::ISource> makeGeradorLadrilhos(int canvasW, int canvasH)
{
    return makeEffectSource(makeLadrilhos(), describeGeradorLadrilhos(), canvasW, canvasH);
}

std::vector<media::ParamDesc> describeGeradorBordaNeon()
{
    return descreverGerador(describeBordaNeon());
}

std::unique_ptr<media::ISource> makeGeradorBordaNeon(int canvasW, int canvasH)
{
    return makeEffectSource(makeBordaNeon(), describeGeradorBordaNeon(), canvasW, canvasH);
}

}
