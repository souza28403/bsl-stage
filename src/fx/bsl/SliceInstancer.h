#pragma once

#include "fx/FxGl.h"
#include "fx/IEffect.h"
#include "fx/bsl/SequenceFeed.h"
#include "fx/bsl/SliceFeed.h"
#include "render/Shader.h"
#include "seq/SeqTiming.h"

#include <glad/gl.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

enum class EscalaSeq { Preencher = 0, Mascara, Esticar };
enum class FundoSeq { Preto = 0, Transparente };

struct InstanciaSlice
{
    float x = 0.0f;
    float y = 0.0f;
    float largura = 0.0f;
    float altura = 0.0f;
    float intensidade = 0.0f;
    float indice = 0.0f;
    float extra[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct ConfigDesenho
{
    EscalaSeq escala = EscalaSeq::Mascara;
    FundoSeq fundo = FundoSeq::Preto;
    bool topOrigin = true;
    float caixa[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    GLuint arranjo = 0;
};

inline constexpr int kMaxInstancias = 8192;

class SliceInstancer
{
public:
    SliceInstancer();
    ~SliceInstancer();
    SliceInstancer(const SliceInstancer&) = delete;
    SliceInstancer& operator=(const SliceInstancer&) = delete;

    bool pronto();
    const std::string& erro() const { return erro_; }

    void desenhar(GLuint entrada, int w, int h, Fbo& saida, const ConfigDesenho& config,
                  const std::vector<InstanciaSlice>& instancias);
    void passagemDireta(GLuint entrada, int w, int h, Fbo& saida);

    int ultimasInstancias() const { return ultimas_; }

private:
    void criar();
    void liberar();

    GLuint vao_ = 0;
    GLuint cantos_ = 0;
    GLuint dados_ = 0;
    std::vector<float> buffer_;
    std::unique_ptr<Shader> shader_;
    TexBlitter blit_;
    std::string erro_;
    bool tentou_ = false;
    int ultimas_ = 0;
};

InstanciaSlice instanciaDe(const FeedSlice& slice, int canvasW, int canvasH, float intensidade, float indice);

void montarInstancias(const std::vector<FeedSlice>& slices, int canvasW, int canvasH,
                      const std::vector<seq::ItemIntensidade>& intensidades, const std::vector<int>* permitidos,
                      float intensidadeGeral, std::vector<InstanciaSlice>& saida);

std::vector<InstanciaSlice> montarInstancias(const std::vector<FeedSlice>& slices, int canvasW, int canvasH,
                                             const std::vector<seq::ItemIntensidade>& intensidades,
                                             const std::vector<int>* permitidos, float intensidadeGeral);

void calcularCaixa(const std::vector<InstanciaSlice>& instancias, float caixa[4]);

class EfeitoSeq : public IEffect
{
public:
    EfeitoSeq(const char* id, const char* nome, std::vector<media::ParamDesc> descs);

    std::string id() const override { return id_; }
    std::string name() const override { return nome_; }
    std::string category() const override { return "Sequências"; }
    std::vector<media::ParamDesc> paramDescs() const override { return descs_; }
    void setParams(const media::ParamSet& params) override { params_ = params; }
    void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) final;
    media::SourceStatus status() const override;

protected:
    virtual bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output);

    const media::ParamSet& params() const;
    SliceInstancer& instanciador() { return instanciador_; }
    const std::vector<FeedSlice>& slicesAtuais(int& canvasW, int& canvasH);
    std::uint64_t versaoDosSlices() const { return versaoSlices_; }
    seq::Relogio relogio(const EffectContext& ctx) const;
    void desenharIntensidades(const EffectContext& ctx, GLuint entrada, int w, int h, Fbo& saida,
                              const std::vector<seq::ItemIntensidade>& intensidades, const std::vector<int>* permitidos);
    std::shared_ptr<const SequenceSnapshot> conjunto() const { return SequenceFeed::instance().snapshot(); }
    void avisar(const std::string& texto) { aviso_ = texto; }

private:
    bool validarReferencias();

    std::string id_;
    std::string nome_;
    std::vector<media::ParamDesc> descs_;
    std::optional<media::ParamSet> params_;
    SliceInstancer instanciador_;
    std::string aviso_;
    std::vector<FeedSlice> slices_;
    std::vector<InstanciaSlice> instancias_;
    std::uint64_t versaoSlices_ = 0;
    bool temSlices_ = false;
    int canvasW_ = 1920;
    int canvasH_ = 1080;
};

media::ParamDesc paramSequencia();
media::ParamDesc paramVisao();
media::ParamDesc paramEnvelope(const char* grupo = "Curva");
media::ParamDesc paramEscala(bool comEsticar = true);
media::ParamDesc paramFundo();
media::ParamDesc paramIntensidade();
std::vector<media::ParamDesc> paramsAvanco(const char* grupo = "Tempo");
std::vector<media::ParamDesc> paramsCurva();

int lerInteiro(const media::ParamSet& params, const std::string& id);
seq::ModoAvanco lerAvanco(const media::ParamSet& params);
seq::ModoCurva lerModoCurva(const media::ParamSet& params);
seq::ParametrosTempo lerParametrosTempo(const media::ParamSet& params);
const seq::Envelope* envelopeDe(const SequenceSnapshot& snap, const media::ParamSet& params);

EscalaSeq lerEscala(const media::ParamSet& params);
FundoSeq lerFundo(const media::ParamSet& params);

}
