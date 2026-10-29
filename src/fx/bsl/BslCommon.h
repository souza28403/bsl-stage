#pragma once

// Pecas comuns dos plugins BSL Light nativos (src/fx/bsl): base dos efeitos de um passe com entrada de imagem, buffer
// de feedback (o glCopyTexSubImage2D dos plugins FFGL vira um FBO proprio) e helpers dos niveis de audio.
//
// Convencoes dos shaders portados:
//  - GLSL 3.30 core; o prelude da imagem entrega imgPos() (0..1, y para baixo) e glPos() (y para cima, como o
//    OpenGL dos plugins), seja qual for a orientacao da textura (uTop);
//  - os plugins FFGL trabalhavam em alpha premultiplicado: sampP() entrega premultiplicado e toStraight() devolve ao
//    alpha reto do BSL Stage, entao a matematica dos shaders originais fica como era.

#include "ai/AiEngine.h"
#include "core/Generators.h"
#include "fx/FxGl.h"
#include "fx/IEffect.h"
#include "render/Shader.h"

#include <glad/gl.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

extern const char* const kBslPrelude;

// Ids do provedor "bsl:" (gravados no projeto)
inline constexpr const char* kAlphaReactorId = "bsl:alpha-reactor";
inline constexpr const char* kDatamoshId = "bsl:datamosh-pixelsort";
inline constexpr const char* kTypoId = "bsl:audio-typo";
inline constexpr const char* kSliceRefinerId = "bsl:slice-refiner-2";
inline constexpr const char* kGenBackgroundsId = "bsl:gen-backgrounds";
inline constexpr const char* kLedMapperId = "bsl:led-mapper";
inline constexpr const char* kPixelMapPatternId = "bsl:pixelmap-pattern";
inline constexpr const char* kSliceRefiner3Id = "bsl:slice-refiner";
inline constexpr const char* kWebBackgroundsId = kBslWebBackgroundsId;
inline constexpr const char* kWebMotionId = kBslWebMotionId;
inline constexpr const char* kPptManagerId = kBslPptManagerId;
inline constexpr const char* kVhsId = "bsl:vhs";
inline constexpr const char* kRelightId = "bsl:relight-25d";
inline constexpr const char* kBodyGlitchId = "bsl:body-glitch";
inline constexpr const char* kAuraId = "bsl:performer-aura";
inline constexpr const char* kZSliceId = "bsl:z-slice";

// Textura com o quadro anterior da saida do efeito (rastros, datamosh, ghost blur)
class FeedbackBuffer
{
public:
    // Garante w x h. Recriar zera o conteudo (transparente). Devolve true se recriou.
    bool ensure(int w, int h);
    GLuint texture() const { return fbo_ ? fbo_->texture() : 0; }
    // Guarda uma copia de 'output' (w x h) para o proximo quadro
    void capture(const Fbo& output, int w, int h);
    void reset() { fbo_.reset(); }

private:
    std::unique_ptr<Fbo> fbo_;
    TexBlitter blit_;
};

// Nivel de uma banda para o efeito: ganho proprio e noise gate sobre a banda em ganho unitario do analisador
// (o AGC e o ganho geral de Ver > Audio ja estao nela), limitado a 0..1 como o GetBassLevel() dos plugins.
float effectBand(float snapshotBand, float gain, float gate);

// Indice (0, 1, ...) da opcao escolhida de um parametro Enum
int enumIndex(const media::ParamSet& params, const std::string& id);

// Segue um alvo: sobe na hora, desce com o tempo de queda 1/rate (s). Como os plugins: 1 - exp(-dt * rate).
float riseFallFollow(float current, float target, double dt, float releaseRate);

// Efeito de um passe com a imagem de entrada. As subclasses dao o corpo do fragment shader (depois do prelude) e
// alimentam os uniforms em render(); o resto (shader, orientacao, FBO de saida, falha) fica aqui.
class BslEffect : public IEffect
{
public:
    struct Meta
    {
        const char* id;
        const char* name;
        const char* category;
    };

    BslEffect(Meta meta, std::vector<media::ParamDesc> descs) : meta_(meta), descs_(std::move(descs)) {}

    std::string id() const override { return meta_.id; }
    std::string name() const override { return meta_.name; }
    std::string category() const override { return meta_.category; }
    std::vector<media::ParamDesc> paramDescs() const override { return descs_; }
    void setParams(const media::ParamSet& params) override;
    void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) final;
    media::SourceStatus status() const override;

protected:
    // Fragment shader completo: kBslPrelude + declaracoes + main
    virtual std::string fragmentSource() const = 0;
    // Antes de ligar a saida e as texturas: tudo que cria ou envia texturas/FBOs (feedback, bitmap de texto) e os envelopes
    virtual void prepare(const EffectContext& /*ctx*/, int /*w*/, int /*h*/) {}
    // Chamado com o shader ligado e a textura de entrada na unidade 0. Liga texturas extras (unidade 1+) e define uniforms.
    virtual void render(const EffectContext& ctx, int w, int h) = 0;
    // Depois do desenho, com o resultado em 'output' (feedback)
    virtual void afterDraw(const EffectContext& /*ctx*/, int /*w*/, int /*h*/, const Fbo& /*output*/) {}

    const media::ParamSet& params() const;
    Shader& shader() { return *shader_; }
    // Textura de entrada do quadro (valida de prepare() em diante)
    GLuint inputTexture() const { return inputTex_; }
    // Chamado de prepare(): o quadro sai igual a entrada (copia), sem rodar o shader
    void passThroughThisFrame() { skip_ = true; }
    // Ganho e gate do efeito (os mesmos parametros "gain" e "gate" dos plugins) aplicados as bandas do quadro
    float bandBass(const EffectContext& ctx) const;
    float bandHigh(const EffectContext& ctx) const;

    Meta meta_;
    std::vector<media::ParamDesc> descs_;

private:
    void build();

    std::optional<media::ParamSet> params_;
    std::unique_ptr<Shader> shader_;
    GLuint inputTex_ = 0;
    bool skip_ = false;
    bool failed_ = false;
    std::string error_;
    ScreenVao vao_;
    TexBlitter blit_;
};

// Efeito que usa a IA local (profundidade e/ou pessoa, ai::AiEngine): pede o resultado do quadro em prepare(); sem IA
// pronta (perfil Eco, sem modelo, carregando) a imagem passa direto e o status mostra o motivo.
class AiEffect : public BslEffect
{
public:
    using BslEffect::BslEffect;
    media::SourceStatus status() const override;

protected:
    // Quais saidas da IA o efeito precisa agora (ai::kNeedDepth | ai::kNeedPerson)
    virtual unsigned aiNeed() const = 0;
    // Chamado so com a IA pronta (frame.state == Ready), no lugar de prepare()
    virtual void prepareAi(const EffectContext& ctx, int w, int h, const ai::AiFrame& frame) = 0;

    const ai::AiFrame& aiFrame() const { return frame_; }

private:
    void prepare(const EffectContext& ctx, int w, int h) final;

    ai::AiFrame frame_ = [] {
        ai::AiFrame f;
        f.state = ai::AiState::Ready; // antes do primeiro quadro: sem aviso
        return f;
    }();
};

} // namespace bsl::fx::bsllight
