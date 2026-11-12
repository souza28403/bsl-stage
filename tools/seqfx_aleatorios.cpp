#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

#include <vector>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarAleatorios()
{
    secao("aleatorios");

    const int T = 64;
    const int N = 8;
    Fbo saida(T, T);
    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    publicarSlices(colunas(N, T, T), T, T);

    seq::SequenceSet conjunto;
    const int idVisao = seq::criarVisao(conjunto, "V");
    seq::acharVisao(conjunto, idVisao)->ligados = {1, 2, 3, 4};
    publicarConjunto(conjunto);

    EffectContext ctx;
    ctx.topOrigin = true;

    auto criar = [&](int maximo, int semente, int visao) {
        std::unique_ptr<IEffect> e = makeSlicesAleatorios();
        media::ParamValues v;
        v.set("maximo", media::Json(maximo));
        v.set("semente", media::Json(semente));
        v.set("velocidade", media::Json(1.0));
        v.set("visao", media::Json(visao));
        e->setParams(media::ParamSet(e->paramDescs(), v));
        return e;
    };
    auto rodar = [&](IEffect& e, double tempo) {
        ctx.time = tempo;
        e.process(ctx, cor, T, T, saida);
        std::vector<bool> acesos;
        for (int i = 0; i < N; ++i)
            acesos.push_back(ler(saida, i * (T / N) + (T / N) / 2, 32).r > 100);
        return acesos;
    };
    auto contar = [](const std::vector<bool>& v) {
        int n = 0;
        for (bool b : v)
            n += b ? 1 : 0;
        return n;
    };

    secao("quantidade");
    {
        auto e = criar(2, 1, 0);
        VERIFICAR(contar(rodar(*e, 0.1)) == 2);
        VERIFICAR(e->status().level == media::SourceStatus::Level::Ok);
        VERIFICAR(e->status().text.empty());
    }
    {
        auto e = criar(64, 1, 0);
        VERIFICAR(contar(rodar(*e, 0.1)) == 4);
    }

    secao("sem repetir");
    {
        auto e = criar(2, 3, 0);
        const std::vector<bool> a = rodar(*e, 0.1);
        const std::vector<bool> b = rodar(*e, 1.1);
        VERIFICAR(contar(a) == 2 && contar(b) == 2);
        bool repetiu = false;
        for (int i = 0; i < N; ++i)
            repetiu = repetiu || (a[static_cast<size_t>(i)] && b[static_cast<size_t>(i)]);
        VERIFICAR(!repetiu);
        const std::vector<bool> c = rodar(*e, 2.1);
        repetiu = false;
        for (int i = 0; i < N; ++i)
            repetiu = repetiu || (b[static_cast<size_t>(i)] && c[static_cast<size_t>(i)]);
        VERIFICAR(!repetiu);
    }

    secao("semente");
    {
        auto e1 = criar(3, 7, 0);
        auto e2 = criar(3, 7, 0);
        VERIFICAR(rodar(*e1, 0.1) == rodar(*e2, 0.1));
        VERIFICAR(rodar(*e1, 1.1) == rodar(*e2, 1.1));
    }

    secao("visao");
    {
        auto e = criar(64, 1, idVisao);
        const std::vector<bool> a = rodar(*e, 0.1);
        VERIFICAR(contar(a) == 2);
        bool fora = false;
        for (int i = 4; i < N; ++i)
            fora = fora || a[static_cast<size_t>(i)];
        VERIFICAR(!fora);
        const std::vector<bool> b = rodar(*e, 1.1);
        fora = false;
        for (int i = 4; i < N; ++i)
            fora = fora || b[static_cast<size_t>(i)];
        VERIFICAR(!fora && contar(b) == 2);
    }

    secao("ecos");
    {
        auto e = criar(2, 1, 0);
        media::ParamValues v;
        v.set("maximo", media::Json(2));
        v.set("velocidade", media::Json(1.0));
        v.set("ecos", media::Json(1));
        e->setParams(media::ParamSet(e->paramDescs(), v));
        rodar(*e, 0.1);
        const std::vector<bool> b = rodar(*e, 1.1);
        VERIFICAR(contar(b) == 4);
    }

    secao("referencias");
    {
        auto e = criar(2, 1, 4242);
        rodar(*e, 0.1);
        VERIFICAR(e->status().level == media::SourceStatus::Level::Warning);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
}

}
