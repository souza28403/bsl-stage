#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

class EfeitoDisparo final : public EfeitoSeq
{
public:
    EfeitoDisparo() : EfeitoSeq(kDisparoId, "Disparo manual", describeDisparo()) {}

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        const media::ParamSet& p = params();
        const auto snap = conjunto();
        const int idSequencia = lerInteiro(p, "sequencia");
        const seq::Sequencia* sequencia = snap->sequencia(idSequencia);
        if (!sequencia)
            return false;
        const int total = static_cast<int>(sequencia->passos.size());
        const std::vector<int>* permitidos = snap->uidsDaVisao(lerInteiro(p, "visao"));

        if (idSequencia != ultimaSequencia_) {
            maquina_.reiniciar();
            ultimaSequencia_ = idSequencia;
        }

        const int proximo = lerInteiro(p, "proximo");
        const int anterior = lerInteiro(p, "anterior");
        const int reiniciar = lerInteiro(p, "reiniciar");
        if (!iniciado_) {
            iniciado_ = true;
        } else {
            if (reiniciar != ultimoReiniciar_)
                maquina_.reiniciar();
            for (int i = 0, n = repeticoes(proximo, ultimoProximo_); i < n; ++i)
                maquina_.proximo(ctx.time, total);
            for (int i = 0, n = repeticoes(anterior, ultimoAnterior_); i < n; ++i)
                maquina_.anterior(ctx.time, total);
        }
        ultimoProximo_ = proximo;
        ultimoAnterior_ = anterior;
        ultimoReiniciar_ = reiniciar;

        seq::ParametrosDisparo pd;
        pd.velocidadeRajada = p.f("velocidadeRajada");
        pd.resetAutomatico = p.b("resetAutomatico");
        pd.atrasoReset = p.f("atrasoReset");
        pd.velocidadeApagar = p.f("velocidadeApagar");
        pd.sustentacao = p.f("sustentacao");
        pd.liberacao = p.f("liberacao");
        pd.curva = lerModoCurva(p);

        maquina_.rajada(p.b("rajada"), ctx.time);
        maquina_.atualizar(ctx.time, pd, total);
        maquina_.calcular(*sequencia, pd, envelopeDe(*snap, p), ctx.time, permitidos, intensidades_);
        desenharIntensidades(ctx, inputTex, w, h, output, intensidades_, permitidos);
        return true;
    }

private:
    static int repeticoes(int atual, int ultimo)
    {
        if (atual == ultimo)
            return 0;
        return atual > ultimo ? std::min(atual - ultimo, 8) : 1;
    }

    seq::MaquinaDisparo maquina_;
    std::vector<seq::ItemIntensidade> intensidades_;
    bool iniciado_ = false;
    int ultimaSequencia_ = -1;
    int ultimoProximo_ = 0;
    int ultimoAnterior_ = 0;
    int ultimoReiniciar_ = 0;
};

}

std::vector<media::ParamDesc> describeDisparo()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramSequencia());
    d.push_back(paramVisao());
    d.push_back(makeCustom("proximo", "Próximo", "event", media::Json(0), "Disparo"));
    d.push_back(makeCustom("anterior", "Anterior", "event", media::Json(0), "Disparo"));
    d.push_back(makeBool("rajada", "Rajada (segurar)", false, "Disparo"));
    d.push_back(makeFloat("velocidadeRajada", "Velocidade da rajada", 8.0f, 0.5f, 60.0f, "%.1f passos/s", "Disparo"));
    d.back().enabledIf = "rajada";
    d.push_back(makeBool("resetAutomatico", "Reiniciar sozinho", false, "Disparo"));
    d.push_back(makeFloat("atrasoReset", "Atraso para reiniciar", 2.0f, 0.1f, 60.0f, "%.1f s", "Disparo"));
    d.back().enabledIf = "resetAutomatico";
    d.push_back(makeCustom("reiniciar", "Reiniciar", "event", media::Json(0), "Disparo"));
    d.push_back(makeFloat("velocidadeApagar", "Velocidade do apagar", 2.0f, 0.1f, 20.0f, "%.1f /s", "Curva"));
    for (media::ParamDesc& p : paramsCurva())
        d.push_back(std::move(p));
    d.push_back(paramEscala());
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeDisparo()
{
    return std::make_unique<EfeitoDisparo>();
}

}
