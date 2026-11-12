#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarSequencia()
{
    secao("sequencia");

    const int T = 64;
    Fbo saida(T, T);
    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    const GLuint metades = criarTextura(T, T, imagemMetades(T, T));
    publicarSlices(colunas(4, T, T), T, T);

    seq::SequenceSet conjunto;
    const int idSeq = seq::criarSequencia(conjunto, "A", 4);
    seq::Sequencia* sequencia = seq::acharSequencia(conjunto, idSeq);
    for (int i = 0; i < 4; ++i)
        sequencia->passos[static_cast<size_t>(i)] = {i + 1};
    const int idVisao = seq::criarVisao(conjunto, "V");
    seq::acharVisao(conjunto, idVisao)->ligados = {1, 2};
    publicarConjunto(conjunto);

    std::unique_ptr<IEffect> efeito = makeSequencia();
    EffectContext ctx;
    ctx.topOrigin = true;
    media::ParamValues v;
    v.set("sequencia", media::Json(idSeq));

    auto rodar = [&](GLuint entrada) {
        efeito->setParams(media::ParamSet(efeito->paramDescs(), v));
        efeito->process(ctx, entrada, T, T, saida);
    };
    auto aceso = [&](int indice, int tol = 3, int r = 200, int g = 100, int b = 50) {
        return corIgual(ler(saida, indice * 16 + 8, 32), r, g, b, 255, tol);
    };
    auto apagado = [&](int indice) { return corIgual(ler(saida, indice * 16 + 8, 32), 0, 0, 0, 255); };

    secao("passo fixo");
    v.set("avanco", media::Json("fixo"));
    v.set("passoFixo", media::Json(2));
    rodar(cor);
    VERIFICAR(apagado(0) && aceso(1) && apagado(2) && apagado(3));
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Ok);

    secao("velocidade");
    v.set("avanco", media::Json("velocidade"));
    v.set("velocidade", media::Json(1.0));
    ctx.time = 2.1;
    rodar(cor);
    VERIFICAR(apagado(0) && apagado(1) && aceso(2) && apagado(3));
    ctx.time = 3.1;
    rodar(cor);
    VERIFICAR(apagado(0) && apagado(1) && apagado(2) && aceso(3));
    ctx.time = 4.1;
    rodar(cor);
    VERIFICAR(aceso(0) && apagado(3));

    secao("batidas");
    v.set("avanco", media::Json("bpm"));
    v.set("batidas", media::Json(1.0));
    ctx.time = 0.0;
    ctx.beat = 1.1;
    rodar(cor);
    VERIFICAR(apagado(0) && aceso(1) && apagado(2));
    ctx.beat = 0.0;

    secao("direcao");
    v.set("avanco", media::Json("fixo"));
    v.set("passoFixo", media::Json(1));
    v.set("direcao", media::Json("tras"));
    rodar(cor);
    VERIFICAR(apagado(0) && apagado(1) && apagado(2) && aceso(3));
    v.set("direcao", media::Json("frente"));

    secao("ecos");
    v.set("passoFixo", media::Json(3));
    v.set("ecos", media::Json(2));
    rodar(cor);
    VERIFICAR(aceso(2) && apagado(3));
    VERIFICAR(aceso(1, 4, 167, 83, 42));
    VERIFICAR(aceso(0, 4, 83, 42, 21));
    v.set("ecos", media::Json(0));

    secao("visao");
    v.set("visao", media::Json(idVisao));
    v.set("passoFixo", media::Json(3));
    rodar(cor);
    VERIFICAR(apagado(0) && apagado(1) && apagado(2) && apagado(3));
    v.set("passoFixo", media::Json(2));
    rodar(cor);
    VERIFICAR(aceso(1) && apagado(2));
    v.set("visao", media::Json(0));

    secao("intensidade");
    v.set("intensidade", media::Json(50.0));
    rodar(cor);
    VERIFICAR(aceso(1, 3, 100, 50, 25));
    v.set("intensidade", media::Json(100.0));

    secao("fundo transparente");
    v.set("fundo", media::Json("transparente"));
    rodar(cor);
    VERIFICAR(corIgual(ler(saida, 40, 32), 0, 0, 0, 0));
    VERIFICAR(corIgual(ler(saida, 24, 32), 200, 100, 50, 255));
    v.set("fundo", media::Json("preto"));

    secao("escala esticar");
    v.set("escala", media::Json("esticar"));
    rodar(metades);
    VERIFICAR(corIgual(ler(saida, 18, 32), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 30, 32), 0, 0, 255, 255));
    v.set("escala", media::Json("mascara"));

    secao("referencias");
    v.set("sequencia", media::Json(9999));
    rodar(cor);
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Warning);
    VERIFICAR(corIgual(ler(saida, 40, 32), 200, 100, 50, 255));
    v.set("sequencia", media::Json(idSeq));
    rodar(cor);
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Ok);
    VERIFICAR(efeito->status().text.empty());

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
    glDeleteTextures(1, &metades);
}

}
