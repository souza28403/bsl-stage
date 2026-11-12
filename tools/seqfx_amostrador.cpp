#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarAmostrador()
{
    secao("amostrador");

    const int T = 64;
    const GLuint metades = criarTextura(T, T, imagemMetades(T, T));
    const GLuint azul = criarTextura(T, T, imagemUniforme(T, T, 0, 0, 255));
    publicarSlices(colunas(4, T, T), T, T);

    seq::SequenceSet conjunto;
    const int idVisao = seq::criarVisao(conjunto, "V");
    seq::acharVisao(conjunto, idVisao)->ligados = {1, 3};
    publicarConjunto(conjunto);

    auto criar = [&](int w, int h, GLuint entrada) {
        Rodador r(makeAmostrador(), w, h, entrada);
        return r;
    };

    secao("amostragens");
    {
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("centro"));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 3));
        VERIFICAR(corIgual(r.px(24, 32), 255, 0, 0, 255, 3));
        VERIFICAR(corIgual(r.px(40, 32), 0, 0, 255, 255, 3));
        VERIFICAR(corIgual(r.px(56, 32), 0, 0, 255, 255, 3));
        VERIFICAR(r.ok());
        r.definir("amostragem", media::Json("media"));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 6));
        VERIFICAR(corIgual(r.px(56, 32), 0, 0, 255, 255, 6));
        r.definir("amostragem", media::Json("mistura"));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 6));
    }

    secao("media de slice largo");
    {
        publicarSlices({criarSlice(1, 8.0f, 0.0f, 48.0f, 64.0f)}, T, T);
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("media"));
        r.rodar();
        const Pixel p = r.px(32, 32);
        VERIFICAR(corIgual(p, 128, 0, 128, 255, 24));
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("luminancia e ganho");
    {
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("centro"));
        r.definir("cor", media::Json("luminancia"));
        r.definir("tinta", media::Json::array({0.0, 1.0, 0.0, 1.0}));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 0, 54, 0, 255, 4));
        VERIFICAR(corIgual(r.px(56, 32), 0, 18, 0, 255, 4));
        r.definir("ganho", media::Json(200.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 0, 108, 0, 255, 5));
        r.definir("ganho", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 0, 0, 0, 255));
    }

    secao("intensidade");
    {
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("centro"));
        r.definir("intensidade", media::Json(50.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 128, 0, 0, 255, 3));
        r.definir("intensidade", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 0, 0, 0, 255));
    }

    secao("suavizacao no tempo");
    {
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("centro"));
        r.definir("suavizacao", media::Json(0.9));
        r.ctx.dt = 1.0 / 60.0;
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 3));
        r.entrada = azul;
        r.rodar();
        const Pixel p = r.px(8, 32);
        VERIFICAR(p.r > 200 && p.b > 5 && p.b < 60);
        for (int i = 0; i < 120; ++i)
            r.rodar();
        const Pixel q = r.px(8, 32);
        VERIFICAR(q.b > 200 && q.r < 40);
        r.definir("suavizacao", media::Json(0.0));
        r.entrada = metades;
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 3));
    }

    secao("visao e fundo");
    {
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("centro"));
        r.definir("visao", media::Json(idVisao));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 3));
        VERIFICAR(corIgual(r.px(24, 32), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(40, 32), 0, 0, 255, 255, 3));
        VERIFICAR(corIgual(r.px(56, 32), 0, 0, 0, 255));
        r.definir("fundo", media::Json("transparente"));
        r.rodar();
        VERIFICAR(r.px(24, 32).a == 0);
        VERIFICAR(r.px(8, 32).a == 255);
        VERIFICAR(r.ok());
    }

    secao("referencia inexistente");
    {
        Rodador r = criar(T, T, metades);
        r.definir("visao", media::Json(4242));
        r.rodar();
        VERIFICAR(r.aviso());
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255));
    }

    secao("topOrigin");
    {
        const GLuint vert = criarTextura(T, T, imagemVertical(T, T));
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 64.0f, 16.0f)}, T, T);
        Rodador r = criar(T, T, vert);
        r.definir("amostragem", media::Json("centro"));
        r.rodar();
        VERIFICAR(corIgual(r.px(32, 8), 255, 0, 0, 255, 3));
        VERIFICAR(corIgual(r.px(32, 56), 0, 0, 0, 255));
        r.ctx.topOrigin = false;
        r.rodar();
        VERIFICAR(corIgual(r.px(32, 56), 0, 0, 255, 255, 3));
        VERIFICAR(corIgual(r.px(32, 8), 0, 0, 0, 255));
        glDeleteTextures(1, &vert);
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("clip diferente do canvas");
    {
        publicarSlices(colunas(4, 128, 128), 128, 128);
        Rodador r = criar(T, T, metades);
        r.definir("amostragem", media::Json("centro"));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 32), 255, 0, 0, 255, 3));
        VERIFICAR(corIgual(r.px(56, 32), 0, 0, 255, 255, 3));
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("entrada grande e muitos slices");
    {
        const int G = 800;
        const GLuint grande = criarTextura(G, G, imagemMetades(G, G));
        publicarSlices(grade(50, 40, 400, 400, 2), 400, 400);
        Rodador r = criar(G, G, grande);
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
        publicarSlices(grade(20, 10, 400, 400, 2), 400, 400);
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
        publicarSlices(colunas(4, T, T), T, T);
        glDeleteTextures(1, &grande);
    }

    secao("imagem de exemplo");
    {
        const GLuint grad = criarTextura(640, 360, imagemGradiente(640, 360));
        publicarSlices(grade(16, 9, 640, 360, 4), 640, 360);
        Rodador r = criar(640, 360, grad);
        r.definir("amostragem", media::Json("media"));
        r.rodar();
        salvarPng(r.saida, "amostrador");
        VERIFICAR(glGetError() == GL_NO_ERROR);
        glDeleteTextures(1, &grad);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &metades);
    glDeleteTextures(1, &azul);
}

}
