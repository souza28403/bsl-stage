#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarContorno()
{
    secao("contorno");

    const int T = 64;
    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    publicarSlices(colunas(4, T, T), T, T);

    seq::SequenceSet conjunto;
    const int idVisao = seq::criarVisao(conjunto, "V");
    seq::acharVisao(conjunto, idVisao)->ligados = {1, 2};
    publicarConjunto(conjunto);

    auto criar = [&](int w, int h, GLuint entrada) {
        Rodador r(makeContorno(), w, h, entrada);
        r.definir("animar", media::Json(false));
        r.definir("mostrarOriginal", media::Json(false));
        r.definir("espessura", media::Json(4.0));
        return r;
    };

    secao("borda fixa");
    {
        Rodador r = criar(T, T, cor);
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 32), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(16 + 14, 32), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(8, 32), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(8, 1), 51, 204, 255, 255, 4));
        VERIFICAR(r.ok());
    }

    secao("mostrar original");
    {
        Rodador r = criar(T, T, cor);
        r.definir("mostrarOriginal", media::Json(true));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 200, 100, 50, 255));
        VERIFICAR(corIgual(r.px(1, 32), 51, 204, 255, 255, 4));
    }

    secao("fundo transparente");
    {
        Rodador r = criar(T, T, cor);
        r.definir("fundo", media::Json("transparente"));
        r.rodar();
        VERIFICAR(r.px(8, 32).a == 0);
        VERIFICAR(corIgual(r.px(1, 32), 51, 204, 255, 255, 4));
    }

    secao("intensidade");
    {
        Rodador r = criar(T, T, cor);
        r.definir("intensidade", media::Json(50.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 32), 26, 102, 128, 255, 4));
        r.definir("intensidade", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 32), 0, 0, 0, 255));
    }

    secao("suavizar para dentro");
    {
        Rodador r = criar(T, T, cor);
        r.definir("espessura", media::Json(8.0));
        r.definir("suavizar", media::Json(true));
        r.rodar();
        const int borda = r.px(0, 32).g;
        const int meio = r.px(4, 32).g;
        VERIFICAR(borda > meio && meio > 5);
    }

    secao("animacao");
    {
        Rodador r = criar(T, T, cor);
        r.definir("animar", media::Json(true));
        r.definir("comprimento", media::Json(0.2));
        r.definir("velocidade", media::Json(1.0));
        r.ctx.time = 0.05;
        r.rodar();
        const std::vector<unsigned char> a = lerTudo(r.saida);
        int acesos = 0, total = 0;
        for (int x = 0; x < 16; ++x) {
            ++total;
            if (r.px(x, 0).g > 20)
                ++acesos;
        }
        VERIFICAR(acesos > 0 && acesos < total);
        r.ctx.time = 0.5;
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) > 0);
        r.ctx.time = 0.05;
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) == 0);
    }

    secao("seguir bpm");
    {
        Rodador r = criar(T, T, cor);
        r.definir("animar", media::Json(true));
        r.definir("comprimento", media::Json(0.2));
        r.definir("seguirBpm", media::Json(true));
        r.ctx.time = 0.0;
        r.ctx.beat = 0.0;
        r.rodar();
        const std::vector<unsigned char> a = lerTudo(r.saida);
        r.ctx.time = 3.0;
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) == 0);
        r.ctx.beat = 0.3;
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) > 0);
    }

    secao("quantidade e inverter");
    {
        Rodador r = criar(T, T, cor);
        r.definir("animar", media::Json(true));
        r.definir("comprimento", media::Json(0.3));
        r.definir("velocidade", media::Json(0.0));
        r.definir("fase", media::Json(0.25));
        r.rodar();
        const std::vector<unsigned char> a = lerTudo(r.saida);
        r.definir("inverterDirecao", media::Json(true));
        r.rodar();
        const std::vector<unsigned char> b = lerTudo(r.saida);
        VERIFICAR(diferentes(a, b) > 0);
        r.definir("inverterDirecao", media::Json(false));
        r.definir("quantidade", media::Json(3));
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) > 0);
    }

    secao("visao");
    {
        Rodador r = criar(T, T, cor);
        r.definir("visao", media::Json(idVisao));
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 32), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(16 + 1, 32), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(32 + 1, 32), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(48 + 1, 32), 0, 0, 0, 255));
        VERIFICAR(r.ok());
    }

    secao("referencia inexistente");
    {
        Rodador r = criar(T, T, cor);
        r.definir("visao", media::Json(4242));
        r.rodar();
        VERIFICAR(r.aviso());
        VERIFICAR(corIgual(r.px(8, 32), 200, 100, 50, 255));
    }

    secao("topOrigin");
    {
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 32.0f, 32.0f)}, T, T);
        Rodador r = criar(T, T, cor);
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 16), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(1, 48), 0, 0, 0, 255));
        r.ctx.topOrigin = false;
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 16), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(1, 48), 51, 204, 255, 255, 4));
    }

    secao("clip diferente do canvas");
    {
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 64.0f, 64.0f)}, 128, 128);
        Rodador r = criar(T, T, cor);
        r.rodar();
        VERIFICAR(corIgual(r.px(1, 16), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(31, 16), 51, 204, 255, 255, 4) || corIgual(r.px(30, 16), 51, 204, 255, 255, 4));
        VERIFICAR(corIgual(r.px(48, 48), 0, 0, 0, 255));
    }

    secao("muitos slices");
    {
        publicarSlices(grade(50, 40, 400, 400, 2), 400, 400);
        Rodador r = criar(400, 400, cor);
        r.definir("animar", media::Json(true));
        r.ctx.time = 0.7;
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
    }

    secao("imagem de exemplo");
    {
        const GLuint grad = criarTextura(640, 360, imagemGradiente(640, 360));
        publicarSlices(grade(8, 4, 640, 360, 6), 640, 360);
        Rodador r(makeContorno(), 640, 360, grad);
        r.definir("espessura", media::Json(5.0));
        r.definir("comprimento", media::Json(0.45));
        r.definir("quantidade", media::Json(2));
        r.definir("leque", media::Json(0.8));
        r.ctx.time = 0.35;
        r.rodar();
        salvarPng(r.saida, "contorno");
        VERIFICAR(glGetError() == GL_NO_ERROR);
        glDeleteTextures(1, &grad);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
}

}
