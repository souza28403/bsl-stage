#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

#include <utility>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarLadrilhos()
{
    secao("ladrilhos");

    const int T = 64;
    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    publicarSlices({criarSlice(1, 0.0f, 0.0f, 64.0f, 64.0f)}, T, T);

    seq::SequenceSet conjunto;
    publicarConjunto(conjunto);

    auto criar = [&](int w, int h, GLuint entrada) {
        Rodador r(makeLadrilhos(), w, h, entrada);
        r.definir("tamanho", media::Json(16.0));
        r.definir("modo", media::Json("textura"));
        r.definir("velocidade", media::Json(0.0));
        r.definir("raio", media::Json(0.0));
        r.definir("suavidade", media::Json(0.0));
        return r;
    };
    auto centro = [](int tx, int ty) { return std::make_pair(tx * 16 + 8, ty * 16 + 8); };

    secao("modo textura");
    {
        Rodador r = criar(T, T, cor);
        r.rodar();
        const auto a = centro(0, 0);
        const auto b = centro(3, 3);
        VERIFICAR(corIgual(r.px(a.first, a.second), 200, 100, 50, 255, 4));
        VERIFICAR(corIgual(r.px(b.first, b.second), 50, 25, 13, 255, 5));
        VERIFICAR(r.ok());
    }

    secao("modos");
    {
        Rodador r = criar(T, T, cor);
        r.definir("modo", media::Json("minimo"));
        r.rodar();
        const auto a = centro(0, 0);
        VERIFICAR(corIgual(r.px(a.first, a.second), 255, 255, 255, 255, 3));
        r.definir("modo", media::Json("arcoiris"));
        r.rodar();
        const Pixel p = r.px(a.first, a.second);
        VERIFICAR(p.r > 240 && p.g < 30 && p.b < 30);
        r.definir("modo", media::Json("matriz"));
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        const std::vector<unsigned char> m = lerTudo(r.saida);
        int verdes = 0;
        for (size_t i = 0; i + 3 < m.size(); i += 4)
            if (m[i + 1] > 100 && m[i] < 60)
                ++verdes;
        VERIFICAR(verdes > 0);
    }

    secao("forma");
    {
        Rodador r = criar(T, T, cor);
        r.rodar();
        VERIFICAR(corIgual(r.px(2, 2), 200, 100, 50, 255, 4));
        r.definir("forma", media::Json("circulo"));
        r.definir("raio", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(0, 0), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(8, 8), 200, 100, 50, 255, 4));
    }

    secao("so borda");
    {
        Rodador r = criar(T, T, cor);
        r.definir("soBorda", media::Json(true));
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 8), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(1, 8), 200, 100, 50, 255, 4));
    }

    secao("grade");
    {
        Rodador r = criar(T, T, cor);
        r.definir("gradeEspessura", media::Json(4.0));
        r.definir("gradeCor", media::Json::array({1.0, 1.0, 1.0, 1.0}));
        r.rodar();
        VERIFICAR(corIgual(r.px(15, 8), 255, 255, 255, 255, 4));
        VERIFICAR(corIgual(r.px(8, 8), 200, 100, 50, 255, 4));
    }

    secao("mostrar original e fundo");
    {
        Rodador r = criar(T, T, cor);
        r.definir("modo", media::Json("minimo"));
        r.definir("comprimento", media::Json(0.01));
        const auto b = centro(1, 0);
        r.rodar();
        VERIFICAR(corIgual(r.px(b.first, b.second), 0, 0, 0, 255));
        r.definir("mostrarOriginal", media::Json(true));
        r.rodar();
        VERIFICAR(corIgual(r.px(b.first, b.second), 200, 100, 50, 255, 3));
        r.definir("mostrarOriginal", media::Json(false));
        r.definir("fundo", media::Json("transparente"));
        r.rodar();
        VERIFICAR(r.px(b.first, b.second).a == 0);
        const auto a = centro(0, 0);
        VERIFICAR(r.px(a.first, a.second).a == 255);
    }

    secao("semente e aleatorizar");
    {
        Rodador r = criar(T, T, cor);
        r.definir("aleatorizar", media::Json(true));
        r.definir("semente", media::Json(5));
        r.rodar();
        const std::vector<unsigned char> a = lerTudo(r.saida);
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) == 0);
        r.definir("semente", media::Json(6));
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) > 0);
    }

    secao("velocidade e leque");
    {
        Rodador r = criar(T, T, cor);
        r.definir("velocidade", media::Json(1.0));
        r.ctx.time = 0.0;
        r.rodar();
        const std::vector<unsigned char> a = lerTudo(r.saida);
        r.ctx.time = 0.4;
        r.rodar();
        VERIFICAR(diferentes(a, lerTudo(r.saida)) > 0);
        publicarSlices(colunas(4, T, T), T, T);
        r.definir("velocidade", media::Json(0.0));
        r.ctx.time = 0.0;
        r.rodar();
        const std::vector<unsigned char> sem = lerTudo(r.saida);
        r.definir("leque", media::Json(1.0));
        r.rodar();
        VERIFICAR(diferentes(sem, lerTudo(r.saida)) > 0);
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 64.0f, 64.0f)}, T, T);
    }

    secao("intensidade");
    {
        Rodador r = criar(T, T, cor);
        r.definir("intensidade", media::Json(50.0));
        r.rodar();
        const auto a = centro(0, 0);
        VERIFICAR(corIgual(r.px(a.first, a.second), 100, 50, 25, 255, 4));
        r.definir("intensidade", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(a.first, a.second), 0, 0, 0, 255));
    }

    secao("referencia inexistente");
    {
        Rodador r = criar(T, T, cor);
        r.definir("visao", media::Json(4242));
        r.rodar();
        VERIFICAR(r.aviso());
        VERIFICAR(corIgual(r.px(8, 8), 200, 100, 50, 255));
    }

    secao("topOrigin");
    {
        const GLuint vert = criarTextura(T, T, imagemVertical(T, T));
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 64.0f, 32.0f)}, T, T);
        Rodador r = criar(T, T, vert);
        r.rodar();
        VERIFICAR(corIgual(r.px(8, 8), 255, 0, 0, 255, 4));
        VERIFICAR(corIgual(r.px(8, 40), 0, 0, 0, 255));
        r.ctx.topOrigin = false;
        r.rodar();
        VERIFICAR(r.px(8, 40).b > 150 && r.px(8, 40).r < 5);
        VERIFICAR(corIgual(r.px(8, 8), 0, 0, 0, 255));
        glDeleteTextures(1, &vert);
    }

    secao("clip diferente do canvas");
    {
        publicarSlices({criarSlice(1, 0.0f, 0.0f, 128.0f, 128.0f)}, 128, 128);
        Rodador r = criar(T, T, cor);
        r.definir("tamanho", media::Json(32.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(16, 16), 200, 100, 50, 255, 4));
        VERIFICAR(glGetError() == GL_NO_ERROR);
    }

    secao("muitos slices");
    {
        publicarSlices(grade(50, 40, 400, 400, 2), 400, 400);
        Rodador r = criar(400, 400, cor);
        r.definir("modo", media::Json("arcoiris"));
        r.definir("velocidade", media::Json(1.0));
        r.ctx.time = 0.7;
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
    }

    secao("imagem de exemplo");
    {
        const GLuint grad = criarTextura(640, 360, imagemGradiente(640, 360));
        publicarSlices(grade(4, 2, 640, 360, 8), 640, 360);
        Rodador r(makeLadrilhos(), 640, 360, grad);
        r.definir("tamanho", media::Json(40.0));
        r.definir("modo", media::Json("arcoiris"));
        r.definir("forma", media::Json("circulo"));
        r.definir("leque", media::Json(0.6));
        r.definir("gradeEspessura", media::Json(2.0));
        r.definir("gradeCor", media::Json::array({0.0, 0.0, 0.0, 1.0}));
        r.ctx.time = 0.4;
        r.definir("velocidade", media::Json(0.5));
        r.rodar();
        salvarPng(r.saida, "ladrilhos");
        VERIFICAR(glGetError() == GL_NO_ERROR);
        glDeleteTextures(1, &grad);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
}

}
