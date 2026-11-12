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

void testarAtraso()
{
    secao("atraso");

    const int T = 64;
    const int quadros = 12;
    Fbo saida(T, T);
    std::vector<GLuint> texturas;
    for (int i = 0; i < quadros; ++i)
        texturas.push_back(criarTextura(T, T, imagemUniforme(T, T, 0, 20 + 15 * i, 0)));

    publicarSlices({criarSlice(1, 0.0f, 0.0f, 20.0f, 64.0f), criarSlice(2, 20.0f, 0.0f, 20.0f, 64.0f),
                    criarSlice(3, 40.0f, 0.0f, 24.0f, 64.0f)},
                   T, T);
    seq::SequenceSet conjunto;
    const int idSeq = seq::criarSequencia(conjunto, "A", 2);
    seq::Sequencia* sequencia = seq::acharSequencia(conjunto, idSeq);
    sequencia->passos[0] = {1};
    sequencia->passos[1] = {2};
    publicarConjunto(conjunto);

    std::unique_ptr<IEffect> efeito = makeAtrasoSlices();
    EffectContext ctx;
    ctx.topOrigin = true;
    media::ParamValues v;
    v.set("sequencia", media::Json(idSeq));
    v.set("atraso", media::Json(0.19));
    v.set("unidade", media::Json("segundos"));
    v.set("qualidade", media::Json(50.0));

    auto rodar = [&](int quadro) {
        ctx.time = static_cast<double>(quadro) / 30.0;
        efeito->setParams(media::ParamSet(efeito->paramDescs(), v));
        efeito->process(ctx, texturas[static_cast<size_t>(quadro)], T, T, saida);
    };
    auto verde = [&](int x) { return ler(saida, x, 32).g; };

    secao("historico");
    for (int i = 0; i < quadros; ++i)
        rodar(i);
    VERIFICAR(perto(verde(10), 20 + 15 * 11));
    VERIFICAR(perto(verde(30), 20 + 15 * 5));
    VERIFICAR(perto(verde(52), 20 + 15 * 11));
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Ok);
    VERIFICAR(efeito->status().text.empty());

    secao("inverter");
    v.set("inverter", media::Json(true));
    rodar(quadros - 1);
    VERIFICAR(perto(verde(10), 20 + 15 * 5));
    VERIFICAR(perto(verde(30), 20 + 15 * 11));
    VERIFICAR(perto(verde(52), 20 + 15 * 11));
    v.set("inverter", media::Json(false));

    secao("sem atraso");
    v.set("atraso", media::Json(0.0));
    rodar(quadros - 1);
    VERIFICAR(perto(verde(10), 20 + 15 * 11));
    VERIFICAR(perto(verde(30), 20 + 15 * 11));
    v.set("atraso", media::Json(0.19));

    secao("unidades");
    v.set("unidade", media::Json("quadros"));
    v.set("atraso", media::Json(5.7));
    rodar(quadros - 1);
    VERIFICAR(perto(verde(30), 20 + 15 * 5));
    v.set("unidade", media::Json("batidas"));
    v.set("atraso", media::Json(0.38));
    ctx.bpm = 120.0f;
    rodar(quadros - 1);
    VERIFICAR(perto(verde(30), 20 + 15 * 5));

    secao("fundo transparente");
    v.set("unidade", media::Json("segundos"));
    v.set("atraso", media::Json(0.19));
    v.set("fundo", media::Json("transparente"));
    rodar(quadros - 1);
    VERIFICAR(perto(ler(saida, 52, 32).a, 255));
    v.set("fundo", media::Json("preto"));

    secao("intensidade");
    v.set("intensidade", media::Json(0.0));
    rodar(quadros - 1);
    VERIFICAR(perto(verde(10), 0) && perto(verde(30), 0));
    v.set("intensidade", media::Json(100.0));

    secao("ocioso");
    v.set("atraso", media::Json(0.0));
    ctx.time = 50.0;
    efeito->setParams(media::ParamSet(efeito->paramDescs(), v));
    efeito->process(ctx, texturas[0], T, T, saida);
    ctx.time = 53.0;
    efeito->process(ctx, texturas[1], T, T, saida);
    VERIFICAR(perto(verde(10), 35) && perto(verde(30), 35));
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Ok);

    secao("pausa longa");
    v.set("atraso", media::Json(0.19));
    v.set("resetPausa", media::Json(1));
    ctx.time = 100.0;
    efeito->setParams(media::ParamSet(efeito->paramDescs(), v));
    efeito->process(ctx, texturas[2], T, T, saida);
    ctx.time = 100.0 + 120.0;
    efeito->process(ctx, texturas[3], T, T, saida);
    VERIFICAR(perto(verde(30), 20 + 15 * 3));

    secao("referencias");
    v.set("sequencia", media::Json(9999));
    efeito->setParams(media::ParamSet(efeito->paramDescs(), v));
    efeito->process(ctx, texturas[0], T, T, saida);
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Warning);
    VERIFICAR(perto(verde(30), 20));

    secao("memoria limitada");
    {
        const int W = 1920;
        const int H = 1080;
        Fbo grande(W, H);
        const GLuint entrada = criarTextura(W, H, imagemUniforme(W, H, 10, 20, 30));
        std::unique_ptr<IEffect> limitado = makeAtrasoSlices();
        media::ParamValues lv;
        lv.set("sequencia", media::Json(idSeq));
        lv.set("atraso", media::Json(1.0));
        lv.set("unidade", media::Json("segundos"));
        lv.set("qualidade", media::Json(50.0));
        lv.set("memoria", media::Json(16));
        limitado->setParams(media::ParamSet(limitado->paramDescs(), lv));
        EffectContext c;
        c.topOrigin = true;
        c.time = 0.0;
        limitado->process(c, entrada, W, H, grande);
        c.time = 0.05;
        limitado->process(c, entrada, W, H, grande);
        const media::SourceStatus st = limitado->status();
        VERIFICAR(st.level == media::SourceStatus::Level::Info);
        VERIFICAR(st.text.find("MB") != std::string::npos);

        lv.set("memoria", media::Json(256));
        lv.set("atraso", media::Json(0.2));
        limitado->setParams(media::ParamSet(limitado->paramDescs(), lv));
        c.time = 0.1;
        limitado->process(c, entrada, W, H, grande);
        c.time = 0.2;
        limitado->process(c, entrada, W, H, grande);
        VERIFICAR(limitado->status().level == media::SourceStatus::Level::Ok);

        c.preview = true;
        lv.set("atraso", media::Json(1.0));
        limitado->setParams(media::ParamSet(limitado->paramDescs(), lv));
        c.time = 0.3;
        limitado->process(c, entrada, W, H, grande);
        VERIFICAR(limitado->status().level == media::SourceStatus::Level::Info);
        glDeleteTextures(1, &entrada);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    for (GLuint t : texturas)
        glDeleteTextures(1, &t);
}

}
