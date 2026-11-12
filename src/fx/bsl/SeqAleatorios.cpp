#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

class EfeitoAleatorios final : public EfeitoSeq
{
public:
    EfeitoAleatorios() : EfeitoSeq(kSlicesAleatoriosId, "Slices aleatórios", describeSlicesAleatorios()) {}

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        const media::ParamSet& p = params();
        const auto snap = conjunto();
        const int idVisao = lerInteiro(p, "visao");
        const std::vector<int>* permitidos = snap->uidsDaVisao(idVisao);
        atualizarCandidatos(*snap, idVisao, permitidos);

        seq::ParametrosAleatorios pa;
        pa.maximo = p.i("maximo");
        pa.passosPorSegundo = p.f("velocidade");
        pa.avanco = lerAvanco(p);
        pa.batidasPorPasso = p.f("batidas");
        pa.ecos = p.i("ecos");
        pa.sustentacao = p.f("sustentacao");
        pa.liberacao = p.f("liberacao");
        pa.curva = lerModoCurva(p);
        pa.semente = p.i("semente");
        pa.semRepetir = p.b("semRepetir");

        sorteio_.atualizar(pa, candidatos_, relogio(ctx));
        sorteio_.calcular(pa, envelopeDe(*snap, p), intensidades_);
        desenharIntensidades(ctx, inputTex, w, h, output, intensidades_, permitidos);
        return true;
    }

private:
    void atualizarCandidatos(const SequenceSnapshot& snap, int idVisao, const std::vector<int>* permitidos)
    {
        int cw = 0, ch = 0;
        const std::vector<FeedSlice>& slices = slicesAtuais(cw, ch);
        if (temChave_ && versaoDosSlices() == chaveSlices_ && snap.versao == chaveConjunto_ && idVisao == chaveVisao_)
            return;
        temChave_ = true;
        chaveSlices_ = versaoDosSlices();
        chaveConjunto_ = snap.versao;
        chaveVisao_ = idVisao;
        candidatos_.clear();
        for (const FeedSlice& s : slices) {
            if (s.uid <= 0 || s.w <= 0.0f || s.h <= 0.0f)
                continue;
            if (permitidos && !std::binary_search(permitidos->begin(), permitidos->end(), s.uid))
                continue;
            candidatos_.push_back(s.uid);
        }
        std::sort(candidatos_.begin(), candidatos_.end());
        candidatos_.erase(std::unique(candidatos_.begin(), candidatos_.end()), candidatos_.end());
    }

    seq::SorteioSlices sorteio_;
    std::vector<int> candidatos_;
    std::vector<seq::ItemIntensidade> intensidades_;
    bool temChave_ = false;
    std::uint64_t chaveSlices_ = 0;
    std::uint64_t chaveConjunto_ = 0;
    int chaveVisao_ = -1;
};

}

std::vector<media::ParamDesc> describeSlicesAleatorios()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramVisao());
    d.push_back(makeInt("maximo", "Máximo de slices", 4, 1, seq::kMaxAleatorios, "%d", "Sorteio"));
    d.back().tooltip = "Limitado à metade dos slices disponíveis";
    d.push_back(makeInt("semente", "Semente", 1, 1, 9999, "%d", "Sorteio"));
    d.push_back(makeBool("semRepetir", "Sem repetir", true, "Sorteio"));
    for (media::ParamDesc& p : paramsAvanco())
        d.push_back(std::move(p));
    d.push_back(makeInt("ecos", "Ecos", 0, 0, seq::kMaxEcos, "%d", "Curva"));
    for (media::ParamDesc& p : paramsCurva())
        d.push_back(std::move(p));
    d.push_back(paramEscala());
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeSlicesAleatorios()
{
    return std::make_unique<EfeitoAleatorios>();
}

}
