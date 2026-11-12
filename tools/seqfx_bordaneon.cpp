#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"

#include <glad/gl.h>

#include <algorithm>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

void testarBordaNeon()
{
    secao("borda neon");

    const int T = 128;
    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    publicarSlices(colunas(4, T, T), T, T);

    seq::SequenceSet conjunto;
    const int idVisao = seq::criarVisao(conjunto, "V");
    seq::acharVisao(conjunto, idVisao)->ligados = {1, 2};
    publicarConjunto(conjunto);

    auto criar = [&](int w, int h, GLuint entrada) {
        Rodador r(makeBordaNeon(), w, h, entrada);
        r.definir("brilho", media::Json(0.0));
        r.definir("raioCanto", media::Json(0.0));
        r.definir("retX", media::Json(10.0));
        r.definir("retY", media::Json(10.0));
        r.definir("retL", media::Json(80.0));
        r.definir("retA", media::Json(80.0));
        r.definir("velocidade", media::Json(1.0));
        r.definir("comprimentoAceso", media::Json(0.01));
        r.definir("flashRuido", media::Json(0.0));
        return r;
    };
    auto todos = [](Rodador& r) {
        r.definir("padrao", media::Json("personalizado"));
        r.definir("personalizado", media::Json("1,2,3,4,5,6,7,8"));
        r.ctx.time = 0.5;
    };
    const int ar = 197, ag = 233, ab = 255;

    secao("segmento aceso");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.0625;
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), ar, ag, ab, 255, 5));
        VERIFICAR(corIgual(r.px(64, 64), 0, 0, 0, 255));
        VERIFICAR(r.px(90, 13).r < 30);
        VERIFICAR(r.px(115, 30).r < 30);
        VERIFICAR(r.px(115, 90).r < 30);
        VERIFICAR(r.px(100, 114).r < 30);
        VERIFICAR(r.px(30, 114).r < 30);
        VERIFICAR(r.px(13, 100).r < 30);
        VERIFICAR(r.px(13, 30).r < 30);
        VERIFICAR(r.ok());
    }

    secao("sequencia dos segmentos");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.1875;
        r.rodar();
        VERIFICAR(r.px(90, 13).r > 185);
        VERIFICAR(r.px(30, 13).r > 5);
        r.ctx.time = 0.3125;
        r.rodar();
        VERIFICAR(r.px(115, 30).r > 185);
        VERIFICAR(r.px(30, 13).r < 30);
        r.ctx.time = 0.4375;
        r.rodar();
        VERIFICAR(r.px(115, 90).r > 185);
        r.ctx.time = 0.5625;
        r.rodar();
        VERIFICAR(r.px(100, 114).r > 185);
        r.ctx.time = 0.6875;
        r.rodar();
        VERIFICAR(r.px(30, 114).r > 185);
        r.ctx.time = 0.8125;
        r.rodar();
        VERIFICAR(r.px(13, 100).r > 185);
        r.ctx.time = 0.9375;
        r.rodar();
        VERIFICAR(r.px(13, 30).r > 185);
    }

    secao("rastro");
    {
        Rodador r = criar(T, T, cor);
        r.definir("comprimentoAceso", media::Json(0.25));
        r.ctx.time = 0.1875;
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), 148, 175, 191, 255, 8));
        VERIFICAR(corIgual(r.px(90, 13), ar, ag, ab, 255, 5));
    }

    secao("direcao");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.0625;
        r.definir("direcao", media::Json("invertida"));
        r.rodar();
        VERIFICAR(r.px(13, 30).r > 185);
        VERIFICAR(r.px(30, 13).r < 30);
        r.definir("direcao", media::Json("vaievolta"));
        r.ctx.time = 0.05;
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 185);
        r.ctx.time = 0.55;
        r.rodar();
        VERIFICAR(r.px(13, 30).r > 185);
        VERIFICAR(r.px(30, 13).r < 30);
    }

    secao("padroes");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.125;
        r.definir("padrao", media::Json("opostos"));
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 185);
        VERIFICAR(r.px(100, 114).r > 185);
        VERIFICAR(r.px(115, 30).r < 30);
        VERIFICAR(r.px(90, 13).r < 30);
        r.ctx.time = 0.0625;
        r.definir("padrao", media::Json("alternado"));
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 185);
        VERIFICAR(r.px(115, 30).r > 185);
        VERIFICAR(r.px(90, 13).r < 30);
        VERIFICAR(r.px(115, 90).r < 30);
        r.ctx.time = 0.125;
        r.definir("padrao", media::Json("personalizado"));
        r.definir("personalizado", media::Json("1,3;2,4"));
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 185);
        VERIFICAR(r.px(115, 30).r > 185);
        VERIFICAR(r.px(90, 13).r < 30);
        VERIFICAR(r.px(115, 90).r < 30);
        r.ctx.time = 0.625;
        r.rodar();
        VERIFICAR(r.px(90, 13).r > 185);
        VERIFICAR(r.px(115, 90).r > 185);
        VERIFICAR(r.px(30, 13).r < 30);
        r.definir("personalizado", media::Json("abc;99"));
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
    }

    secao("segmentos");
    {
        Rodador r = criar(T, T, cor);
        r.definir("segmentos", media::Json(4));
        r.ctx.time = 0.125;
        r.rodar();
        VERIFICAR(r.px(60, 13).r > 185);
        VERIFICAR(r.px(90, 13).r > 185);
        VERIFICAR(r.px(115, 60).r < 30);
        r.definir("segmentos", media::Json(32));
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
    }

    secao("seguir bpm");
    {
        Rodador r = criar(T, T, cor);
        r.definir("seguirBpm", media::Json(true));
        r.definir("batidasCiclo", media::Json(4.0));
        r.ctx.time = 5.0;
        r.ctx.beat = 0.25;
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 185);
        r.ctx.time = 9.0;
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 185);
        r.ctx.beat = 0.75;
        r.rodar();
        VERIFICAR(r.px(90, 13).r > 185);
    }

    secao("cintilacao");
    {
        Rodador r = criar(T, T, cor);
        todos(r);
        r.definir("cintilacao", media::Json(100.0));
        int minimo = 255, maximo = 0;
        for (int i = 0; i < 16; ++i) {
            r.ctx.time = 0.5 + 0.07 * i;
            r.rodar();
            const int v = r.px(30, 13).r;
            minimo = std::min(minimo, v);
            maximo = std::max(maximo, v);
        }
        VERIFICAR(maximo - minimo > 20);
        r.definir("cintilacao", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), ar, ag, ab, 255, 5));
    }

    secao("barras laterais");
    {
        Rodador r = criar(T, T, cor);
        todos(r);
        r.definir("retX", media::Json(20.0));
        r.definir("retY", media::Json(20.0));
        r.definir("retL", media::Json(60.0));
        r.definir("retA", media::Json(60.0));
        r.definir("barrasQuantidade", media::Json(2));
        r.definir("barrasLargura", media::Json(4.0));
        r.definir("barrasDistancia", media::Json(5.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(20, 64), 0, 0, 0, 255));
        r.definir("barras", media::Json(true));
        r.rodar();
        VERIFICAR(r.px(20, 64).r > 185);
        VERIFICAR(r.px(13, 64).r > 150);
        VERIFICAR(r.px(107, 64).r > 185);
        VERIFICAR(r.px(114, 64).r > 150);
        VERIFICAR(corIgual(r.px(20, 10), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(64, 64), 0, 0, 0, 255));
        r.definir("barrasQuantidade", media::Json(1));
        r.rodar();
        VERIFICAR(r.px(20, 64).r > 185);
        VERIFICAR(corIgual(r.px(13, 64), 0, 0, 0, 255));
    }

    secao("brilho");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.0625;
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 8), 0, 0, 0, 255));
        r.definir("brilho", media::Json(100.0));
        r.rodar();
        const Pixel p = r.px(30, 8);
        VERIFICAR(p.g > 100 && p.b > 140 && p.r > 40);
    }

    secao("flash por evento");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.3125;
        r.rodar();
        VERIFICAR(r.px(30, 13).r < 30);
        r.definir("flash", media::Json(1));
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), 255, 255, 255, 255, 3));
        VERIFICAR(corIgual(r.px(100, 114), 255, 255, 255, 255, 3));
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), 255, 255, 255, 255, 3));
        r.ctx.time = 0.6125;
        r.rodar();
        VERIFICAR(r.px(30, 13).r < 30);
        VERIFICAR(r.ok());
    }

    secao("flash automatico");
    {
        Rodador r = criar(T, T, cor);
        r.definir("flashAutomatico", media::Json(1));
        r.ctx.time = 0.3125;
        r.ctx.beat = 0.5;
        r.rodar();
        VERIFICAR(r.px(30, 13).r < 30);
        r.ctx.beat = 4.5;
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), 255, 255, 255, 255, 3));
        r.ctx.time = 0.6125;
        r.ctx.beat = 4.7;
        r.rodar();
        VERIFICAR(r.px(30, 13).r < 30);
    }

    secao("cor e duracao do flash");
    {
        Rodador r = criar(T, T, cor);
        r.definir("corFlash", media::Json::array({1.0, 0.0, 0.0, 1.0}));
        r.definir("flashDuracao", media::Json(1.0));
        r.ctx.time = 0.3125;
        r.rodar();
        r.definir("flash", media::Json(1));
        r.rodar();
        const Pixel p = r.px(30, 13);
        VERIFICAR(p.r > 200 && p.g > 150 && p.b > 150);
        r.ctx.time = 0.3125 + 0.25;
        r.rodar();
        VERIFICAR(r.px(30, 13).r > 30 || r.px(30, 13).g > 5);
    }

    secao("ruido do flash");
    {
        Rodador r = criar(T, T, cor);
        r.definir("brilho", media::Json(60.0));
        r.ctx.time = 0.3125;
        r.rodar();
        r.definir("flash", media::Json(1));
        r.rodar();
        const std::vector<unsigned char> limpo = lerTudo(r.saida);
        r.rodar();
        VERIFICAR(diferentes(limpo, lerTudo(r.saida)) == 0);
        r.definir("flashRuido", media::Json(100.0));
        r.rodar();
        VERIFICAR(diferentes(limpo, lerTudo(r.saida)) > 50);
    }

    secao("fundos");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.0625;
        r.definir("fundo", media::Json("transparente"));
        r.rodar();
        VERIFICAR(r.px(64, 64).a == 0);
        const Pixel p = r.px(30, 13);
        VERIFICAR(corIgual(p, ar, ag, ab, 255, 5));
        r.definir("fundo", media::Json("original"));
        r.rodar();
        VERIFICAR(corIgual(r.px(64, 64), 200, 100, 50, 255));
        VERIFICAR(r.px(30, 13).r == 255 && r.px(30, 13).g == 255 && r.px(30, 13).b == 255);
        VERIFICAR(r.px(100, 114).r >= 200 && r.px(100, 114).r < 230);
        VERIFICAR(r.ok());
    }

    secao("intensidade");
    {
        Rodador r = criar(T, T, cor);
        r.ctx.time = 0.0625;
        r.definir("intensidade", media::Json(50.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), 99, 117, 128, 255, 5));
        r.definir("intensidade", media::Json(0.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(30, 13), 0, 0, 0, 255));
    }

    secao("forma slices");
    {
        Rodador r = criar(T, T, cor);
        todos(r);
        r.definir("forma", media::Json("slices"));
        r.definir("visao", media::Json(idVisao));
        r.rodar();
        VERIFICAR(r.px(1, 64).r > 185);
        VERIFICAR(r.px(32, 64).r > 185);
        VERIFICAR(r.px(63, 64).r > 185);
        VERIFICAR(corIgual(r.px(16, 64), 0, 0, 0, 255));
        VERIFICAR(corIgual(r.px(97, 64), 0, 0, 0, 255));
        VERIFICAR(r.ok());
        r.definir("visao", media::Json(0));
        r.rodar();
        VERIFICAR(r.px(97, 64).r > 185);
    }

    secao("referencia inexistente");
    {
        Rodador r = criar(T, T, cor);
        r.definir("forma", media::Json("slices"));
        r.definir("visao", media::Json(4242));
        r.rodar();
        VERIFICAR(r.aviso());
        VERIFICAR(corIgual(r.px(64, 64), 200, 100, 50, 255));
    }

    secao("topOrigin");
    {
        Rodador r = criar(T, T, cor);
        todos(r);
        r.definir("retY", media::Json(10.0));
        r.definir("retA", media::Json(30.0));
        r.rodar();
        VERIFICAR(r.px(64, 13).r > 185);
        VERIFICAR(corIgual(r.px(64, 114), 0, 0, 0, 255));
        r.ctx.topOrigin = false;
        r.rodar();
        VERIFICAR(r.px(64, 114).r > 185);
        VERIFICAR(corIgual(r.px(64, 13), 0, 0, 0, 255));
    }

    secao("tamanho diferente do canvas");
    {
        Rodador r = criar(64, 64, cor);
        todos(r);
        r.rodar();
        VERIFICAR(r.px(6, 32).r > 185);
        VERIFICAR(corIgual(r.px(32, 32), 0, 0, 0, 255));
        VERIFICAR(glGetError() == GL_NO_ERROR);
    }

    secao("cantos arredondados");
    {
        Rodador r = criar(T, T, cor);
        todos(r);
        r.definir("raioCanto", media::Json(50.0));
        r.definir("retX", media::Json(10.0));
        r.definir("retY", media::Json(25.0));
        r.definir("retL", media::Json(80.0));
        r.definir("retA", media::Json(50.0));
        r.rodar();
        VERIFICAR(corIgual(r.px(14, 34), 0, 0, 0, 255));
        VERIFICAR(r.px(64, 32).r > 185);
        VERIFICAR(r.px(13, 64).r > 185);
        VERIFICAR(glGetError() == GL_NO_ERROR);
    }

    secao("muitos slices");
    {
        publicarSlices(grade(50, 40, 400, 400, 2), 400, 400);
        Rodador r = criar(400, 400, cor);
        r.definir("forma", media::Json("slices"));
        r.definir("brilho", media::Json(60.0));
        r.definir("padrao", media::Json("alternado"));
        r.ctx.time = 0.4;
        r.rodar();
        VERIFICAR(glGetError() == GL_NO_ERROR);
        VERIFICAR(r.ok());
        publicarSlices(colunas(4, T, T), T, T);
    }

    secao("imagem de exemplo");
    {
        const GLuint grad = criarTextura(640, 360, imagemGradiente(640, 360));
        Rodador r(makeBordaNeon(), 640, 360, grad);
        r.definir("barras", media::Json(true));
        r.definir("segmentos", media::Json(16));
        r.definir("comprimentoAceso", media::Json(0.3));
        r.definir("raioCanto", media::Json(6.0));
        r.definir("retX", media::Json(14.0));
        r.definir("retY", media::Json(12.0));
        r.definir("retL", media::Json(72.0));
        r.definir("retA", media::Json(76.0));
        r.definir("barrasQuantidade", media::Json(2));
        r.definir("barrasAltura", media::Json(70.0));
        r.definir("cintilacao", media::Json(25.0));
        r.definir("brilho", media::Json(70.0));
        r.ctx.time = 0.9;
        r.rodar();
        salvarPng(r.saida, "borda-neon");
        VERIFICAR(glGetError() == GL_NO_ERROR);
        r.definir("flashRuido", media::Json(60.0));
        r.rodar();
        r.definir("flash", media::Json(1));
        r.ctx.time = 0.95;
        r.rodar();
        salvarPng(r.saida, "borda-neon-flash");
        VERIFICAR(glGetError() == GL_NO_ERROR);
        glDeleteTextures(1, &grad);
    }

    VERIFICAR(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &cor);
}

}
