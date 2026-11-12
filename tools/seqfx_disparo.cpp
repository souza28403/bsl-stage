#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarDisparo()
{
    secao("disparo");

    const int T = 64;
    Fbo saida(T, T);
    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    publicarSlices(colunas(4, T, T), T, T);

    seq::SequenceSet conjunto;
    const int idSeq = seq::criarSequencia(conjunto, "A", 4);
    seq::Sequencia* sequencia = seq::acharSequencia(conjunto, idSeq);
    for (int i = 0; i < 4; ++i)
        sequencia->passos[static_cast<size_t>(i)] = {i + 1};
    publicarConjunto(conjunto);

    std::unique_ptr<IEffect> efeito = makeDisparo();
    EffectContext ctx;
    ctx.topOrigin = true;
    media::ParamValues v;
    v.set("sequencia", media::Json(idSeq));

    auto rodar = [&](double tempo) {
        ctx.time = tempo;
        efeito->setParams(media::ParamSet(efeito->paramDescs(), v));
        efeito->process(ctx, cor, T, T, saida);
    };
    auto vermelho = [&](int indice) { return ler(saida, indice * 16 + 8, 32).r; };
    auto aceso = [&](int indice) { return vermelho(indice) >= 197; };
    auto apagado = [&](int indice) { return vermelho(indice) == 0; };

    secao("proximo");
    rodar(0.0);
    VERIFICAR(apagado(0) && apagado(1) && apagado(2) && apagado(3));
    v.set("proximo", media::Json(1));
    rodar(1.0);
    VERIFICAR(aceso(0) && apagado(1) && apagado(2) && apagado(3));
    v.set("proximo", media::Json(2));
    rodar(1.1);
    VERIFICAR(aceso(0) && aceso(1) && apagado(2));

    secao("anterior");
    v.set("anterior", media::Json(1));
    rodar(1.2);
    v.set("proximo", media::Json(3));
    rodar(1.3);
    VERIFICAR(aceso(1) && apagado(2));

    secao("apagar");
    rodar(3.0);
    VERIFICAR(apagado(0) && apagado(1) && apagado(2) && apagado(3));

    secao("reiniciar");
    v.set("reiniciar", media::Json(1));
    v.set("proximo", media::Json(4));
    rodar(3.1);
    VERIFICAR(aceso(0) && apagado(1));

    secao("rajada");
    v.set("rajada", media::Json(true));
    v.set("velocidadeRajada", media::Json(10.0));
    rodar(5.0);
    VERIFICAR(aceso(1) && apagado(0) && apagado(2) && apagado(3));
    rodar(5.25);
    VERIFICAR(aceso(3));
    VERIFICAR(vermelho(2) > 100 && vermelho(2) < 197);
    VERIFICAR(vermelho(1) > 50 && vermelho(1) < vermelho(2));
    VERIFICAR(apagado(0));
    v.set("rajada", media::Json(false));
    rodar(8.0);
    VERIFICAR(apagado(0) && apagado(1) && apagado(2) && apagado(3));

    secao("reiniciar sozinho");
    v.set("resetAutomatico", media::Json(true));
    v.set("atrasoReset", media::Json(1.0));
    v.set("proximo", media::Json(5));
    rodar(10.0);
    VERIFICAR(aceso(0));
    rodar(11.5);
    v.set("proximo", media::Json(6));
    rodar(11.6);
    VERIFICAR(aceso(0) && apagado(1));

    secao("curva");
    v.set("resetAutomatico", media::Json(false));
    v.set("velocidadeApagar", media::Json(1.0));
    v.set("sustentacao", media::Json(0.0));
    v.set("liberacao", media::Json(1.0));
    v.set("proximo", media::Json(7));
    rodar(20.0);
    VERIFICAR(aceso(1));
    rodar(20.5);
    VERIFICAR(perto(vermelho(1), 100, 4));
    VERIFICAR(apagado(0) && apagado(2) && apagado(3));

    secao("referencias");
    v.set("sequencia", media::Json(9999));
    rodar(21.0);
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Warning);
    v.set("sequencia", media::Json(idSeq));
    rodar(21.1);
    VERIFICAR(efeito->status().level == media::SourceStatus::Level::Ok);
    VERIFICAR(efeito->status().text.empty());

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
}

}
