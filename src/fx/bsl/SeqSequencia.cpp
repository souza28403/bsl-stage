#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SliceInstancer.h"

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

class EfeitoSequencia final : public EfeitoSeq
{
public:
    EfeitoSequencia() : EfeitoSeq(kSequenciaId, "Sequência de slices", describeSequencia()) {}

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        const media::ParamSet& p = params();
        const auto snap = conjunto();
        const seq::Sequencia* sequencia = snap->sequencia(lerInteiro(p, "sequencia"));
        if (!sequencia)
            return false;
        const std::vector<int>* permitidos = snap->uidsDaVisao(lerInteiro(p, "visao"));
        seq::calcularIntensidades(*sequencia, lerParametrosTempo(p), envelopeDe(*snap, p), relogio(ctx), permitidos,
                                  intensidades_);
        desenharIntensidades(ctx, inputTex, w, h, output, intensidades_, permitidos);
        return true;
    }

private:
    std::vector<seq::ItemIntensidade> intensidades_;
};

}

std::vector<media::ParamDesc> describeSequencia()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramSequencia());
    d.push_back(paramVisao());
    for (media::ParamDesc& p : paramsAvanco())
        d.push_back(std::move(p));
    d.push_back(makeEnum("direcao", "Direção",
                         {{"frente", "Para frente"}, {"tras", "Para trás"}, {"vaievolta", "Vai e volta"},
                          {"aleatorio", "Aleatório"}},
                         "frente", "Tempo"));
    d.push_back(makeInt("deslocamento", "Deslocamento", 0, -1024, 1024, "%d", "Tempo"));
    d.push_back(makeInt("semente", "Semente", 1, 1, 9999, "%d", "Tempo"));
    d.back().enabledIf = "direcao=aleatorio";
    d.push_back(makeInt("ecos", "Ecos", 0, 0, seq::kMaxEcos, "%d", "Curva"));
    for (media::ParamDesc& p : paramsCurva())
        d.push_back(std::move(p));
    d.push_back(paramEscala());
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeSequencia()
{
    return std::make_unique<EfeitoSequencia>();
}

}
