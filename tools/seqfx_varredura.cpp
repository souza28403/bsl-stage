#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarVarredura()
{
    secao("varredura");

    const int T = 64;
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

    auto criar = [&](int w, int h, GLuint entrada) {
        Rodador r(makeVarredura(), w, h, entrada);
        r.definir("sequencia", media::Json(idSeq));
        r.definir("velocidade", media::Json(1.0));
        r.definir("escala", media::Json("mascara"));
        return r;
    };

    secao("faixa no slice ativo");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 1.5;
        r.rodar();
        VERIFICAR(perto(r.px(24, 32).r, 179, 12) && perto(r.px(24, 32).g, 90, 8));
        VERIFICAR(corIgual(r.px(17, 32), 0, 0, 0, 255, 4));
        VERIFICAR(corIgual(r.px(8, 32), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(40, 32), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(56, 32), 0, 0, 0, 255));
        VERIFICAR(r.ok());
    }

    secao("movimento");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 1.25;
        r.rodar();
        VERIFICAR(r.px(19, 32).r > 120);
        VERIFICAR(r.px(29, 32).r < 5);
        r.ctx.time = 1.75;
        r.rodar();
        VERIFICAR(r.px(19, 32).r < 5);
        VERIFICAR(r.px(28, 32).r > 120);
    }

    secao("inverter");
    {
        Rodador r = criar(T, T, cor);
        r.definir("inverter", media::Json(true));
        r.ctx.time = 1.25;
        r.rodar();
        VERIFICAR(r.px(19, 32).r < 5);
        VERIFICAR(r.px(29, 32).r > 150);
    }

    secao("alternar pares e impares");
    {
        Rodador r = criar(T, T, cor);
        r.definir("alternar", media::Json(true));
        r.ctx.time = 1.25;
        r.rodar();
        VERIFICAR(r.px(19, 32).r < 5);
        VERIFICAR(r.px(29, 32).r > 150);
        r.ctx.time = 0.25;
        r.rodar();
        VERIFICAR(r.px(3, 32).r > 120);
    }

    secao("trocar eixos");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 1.25;
        r.definir("trocarEixos", media::Json(true));
        r.rodar();
        VERIFICAR(r.px(24, 11).r > 150);
        VERIFICAR(r.px(24, 50).r < 5);
        VERIFICAR(r.px(24, 32).r < 5);
    }

    secao("voltas");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 1.25;
        r.rodar();
        VERIFICAR(r.px(24, 32).r < 5);
        r.definir("voltas", media::Json(2));
        r.rodar();
        VERIFICAR(r.px(24, 32).r > 150);
    }

    secao("sobreposicao");
    {
        Rodador r = criar(T, T, cor);
        r.definir("sobreposicao", media::Json(2.0));
        r.ctx.time = 1.5;
        r.rodar();
        VERIFICAR(r.px(8, 32).r > 150);
        VERIFICAR(r.px(17, 32).r > 100);
        VERIFICAR(corIgual(r.px(40, 32), 0, 0, 0, 255));
    }

    secao("escala");
    {
        Rodador r = criar(T, T, metades);
        r.definir("escala", media::Json("mascara"));
        r.ctx.time = 1.5;
        r.rodar();
        const Pixel m = r.px(25, 32);
        VERIFICAR(m.r > 40 && m.b < 5);
        r.definir("escala", media::Json("preencher"));
        r.rodar();
        const Pixel p = r.px(25, 32);
        VERIFICAR(p.b > 40 && p.r < 5);
        const Pixel e = r.px(23, 32);
        VERIFICAR(e.r > 150 && e.b < 5);
    }

    secao("fundo transparente e intensidade");
    {
        Rodador r = criar(T, T, cor);
        r.definir("fundo", media::Json("transparente"));
        r.ctx.time = 1.5;
        r.rodar();
        VERIFICAR(r.px(8, 32).a == 0);
        VERIFICAR(perto(r.px(24, 32).a, 228, 8));
        r.definir("intensidade", media::Json(50.0));
        r.rodar();
        VERIFICAR(perto(r.px(24, 32).a, 114, 6));
        r.definir("fundo", media::Json("preto"));
        r.rodar();
        VERIFICAR(corIgual(r.px(24, 32), 90, 45, 22, 255, 6));
    }

    secao("visao");
    {
        Rodador r = criar(T, T, cor);
        r.definir("visao", media::Json(idVisao));
        r.ctx.time = 1.5;
        r.rodar();
        VERIFICAR(perto(r.px(24, 32).r, 179, 12));
        r.ctx.time = 2.5;
        r.rodar();
        VERIFICAR(corIgual(r.px(40, 32), 0, 0, 0, 255));
        VERIFICAR(r.ok());
    }

    secao("referencia inexistente");
    {
        Rodador r = criar(T, T, cor);
        r.definir("sequencia", media::Json(4242));
        r.rodar();
        VERIFICAR(r.aviso());
        VERIFICAR(corIgual(r.px(8, 32), 200, 100, 50, 255));
    }

    secao("topOrigin");
    {
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 64.0f, 32.0f)}, T, T);
        Rodador r = criar(T, T, cor);
        r.definir("velocidade", media::Json(1.0));
        r.ctx.time = 0.5;
        r.rodar();
        VERIFICAR(r.px(32, 16).r > 120);
        VERIFICAR(corIgual(r.px(32, 48), 0, 0, 0, 255));
        r.ctx.topOrigin = false;
        r.rodar();
        VERIFICAR(r.px(32, 48).r > 120);
        VERIFICAR(corIgual(r.px(32, 16), 0, 0, 0, 255));
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("clip diferente do canvas");
    {
        publicarSlices(colunas(4, 128, 128), 128, 128);
        Rodador r = criar(T, T, cor);
        r.ctx.time = 1.5;
        r.rodar();
        VERIFICAR(perto(r.px(24, 32).r, 179, 12));
        VERIFICAR(corIgual(r.px(8, 32), 0, 0, 0, 255));
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("muitos slices");
    {
        const std::vector<FeedSlice> muitos = grade(50, 40, 400, 400, 2);
        seq::SequenceSet grande;
        const int idGrande = seq::criarSequencia(grande, "G", 40);
        seq::Sequencia* sg = seq::acharSequencia(grande, idGrande);
        for (size_t i = 0; i < muitos.size(); ++i)
            sg->passos[i / 50].push_back(muitos[i].uid);
        publicarConjunto(grande);
        publicarSlices(muitos, 400, 400);
        Rodador r(makeVarredura(), 400, 400, cor);
        r.definir("sequencia", media::Json(idGrande));
        r.definir("velocidade", media::Json(10.0));
        r.ctx.time = 1.3;
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
        publicarConjunto(conjunto);
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("imagem de exemplo");
    {
        const GLuint grad = criarTextura(640, 360, imagemGradiente(640, 360));
        const std::vector<FeedSlice> slices = grade(8, 4, 640, 360, 6);
        seq::SequenceSet exemplo;
        const int id = seq::criarSequencia(exemplo, "E", 8);
        seq::Sequencia* se = seq::acharSequencia(exemplo, id);
        for (size_t i = 0; i < slices.size(); ++i)
            se->passos[i % 8].push_back(slices[i].uid);
        publicarConjunto(exemplo);
        publicarSlices(slices, 640, 360);
        Rodador r(makeVarredura(), 640, 360, grad);
        r.definir("sequencia", media::Json(id));
        r.definir("velocidade", media::Json(2.0));
        r.definir("sobreposicao", media::Json(3.0));
        r.definir("comprimentoBorda", media::Json(0.5));
        r.ctx.time = 2.6;
        r.rodar();
        salvarPng(r.saida, "varredura");
        VERIFICAR(glGetError() == GL_NO_ERROR);
        glDeleteTextures(1, &grad);
        publicarConjunto(conjunto);
        publicarSlices(colunas(4, T, T), T, T);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
    glDeleteTextures(1, &metades);
}

}
