#include "seqfx.h"

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SequenceFeed.h"
#include "fx/bsl/SliceInstancer.h"
#include "render/Fbo.h"

#include <glad/gl.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx;
using namespace bsl::fx::bsllight;

namespace {

std::vector<seq::ItemIntensidade> todas(const std::vector<FeedSlice>& slices, float valor)
{
    std::vector<seq::ItemIntensidade> v;
    for (const FeedSlice& s : slices)
        v.push_back({s.uid, valor});
    std::sort(v.begin(), v.end(), [](const seq::ItemIntensidade& a, const seq::ItemIntensidade& b) { return a.uid < b.uid; });
    return v;
}

void desenharCaso(SliceInstancer& inst, GLuint entrada, int w, int h, Fbo& saida, const std::vector<FeedSlice>& slices,
                  int canvasW, int canvasH, const std::vector<seq::ItemIntensidade>& intensidades,
                  EscalaSeq escala, FundoSeq fundo, bool topOrigin)
{
    std::vector<InstanciaSlice> instancias = montarInstancias(slices, canvasW, canvasH, intensidades, nullptr, 1.0f);
    ConfigDesenho config;
    config.escala = escala;
    config.fundo = fundo;
    config.topOrigin = topOrigin;
    calcularCaixa(instancias, config.caixa);
    inst.desenhar(entrada, w, h, saida, config, instancias);
}

}

void testarInstanciador()
{
    secao("instanciador");

    const int T = 64;
    Fbo saida(T, T);
    SliceInstancer inst;
    VERIFICAR(inst.pronto());
    VERIFICAR(inst.erro().empty());

    const GLuint cor = criarTextura(T, T, imagemUniforme(T, T, 200, 100, 50));
    const std::vector<FeedSlice> quadrante = {criarSlice(1, 0.0f, 0.0f, 32.0f, 32.0f)};

    secao("intensidades");
    desenharCaso(inst, cor, T, T, saida, quadrante, T, T, todas(quadrante, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
    VERIFICAR(inst.ultimasInstancias() == 1);
    VERIFICAR(corIgual(ler(saida, 10, 10), 200, 100, 50, 255));
    VERIFICAR(corIgual(ler(saida, 50, 50), 0, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 50, 10), 0, 0, 0, 255));
    desenharCaso(inst, cor, T, T, saida, quadrante, T, T, todas(quadrante, 0.5f), EscalaSeq::Mascara, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 10, 10), 100, 50, 25, 255));
    desenharCaso(inst, cor, T, T, saida, quadrante, T, T, todas(quadrante, 0.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
    VERIFICAR(inst.ultimasInstancias() == 0);
    VERIFICAR(corIgual(ler(saida, 10, 10), 0, 0, 0, 255));
    VERIFICAR(glGetError() == GL_NO_ERROR);

    secao("origem");
    desenharCaso(inst, cor, T, T, saida, quadrante, T, T, todas(quadrante, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 10, 10), 200, 100, 50, 255) && corIgual(ler(saida, 10, 50), 0, 0, 0, 255));
    desenharCaso(inst, cor, T, T, saida, quadrante, T, T, todas(quadrante, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, false);
    VERIFICAR(corIgual(ler(saida, 10, 50), 200, 100, 50, 255) && corIgual(ler(saida, 10, 10), 0, 0, 0, 255));

    secao("escalas");
    const GLuint metades = criarTextura(T, T, imagemMetades(T, T));
    const std::vector<FeedSlice> dois = {criarSlice(1, 0.0f, 0.0f, 16.0f, 64.0f), criarSlice(2, 48.0f, 0.0f, 16.0f, 64.0f)};
    desenharCaso(inst, metades, T, T, saida, dois, T, T, todas(dois, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 12, 30), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 60, 30), 0, 0, 255, 255));
    VERIFICAR(corIgual(ler(saida, 30, 30), 0, 0, 0, 255));
    desenharCaso(inst, metades, T, T, saida, dois, T, T, todas(dois, 1.0f), EscalaSeq::Preencher, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 12, 30), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 60, 30), 0, 0, 255, 255));
    VERIFICAR(corIgual(ler(saida, 30, 30), 0, 0, 0, 255));
    desenharCaso(inst, metades, T, T, saida, dois, T, T, todas(dois, 1.0f), EscalaSeq::Esticar, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 4, 30), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 12, 30), 0, 0, 255, 255));
    VERIFICAR(corIgual(ler(saida, 52, 30), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 60, 30), 0, 0, 255, 255));

    const std::vector<FeedSlice> centro = {criarSlice(1, 16.0f, 16.0f, 32.0f, 32.0f)};
    desenharCaso(inst, metades, T, T, saida, centro, T, T, todas(centro, 1.0f), EscalaSeq::Preencher, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 20, 32), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 44, 32), 0, 0, 255, 255));
    desenharCaso(inst, metades, T, T, saida, centro, T, T, todas(centro, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
    VERIFICAR(corIgual(ler(saida, 20, 32), 255, 0, 0, 255));
    VERIFICAR(corIgual(ler(saida, 40, 32), 0, 0, 255, 255));
    VERIFICAR(glGetError() == GL_NO_ERROR);

    secao("transparente");
    desenharCaso(inst, cor, T, T, saida, quadrante, T, T, todas(quadrante, 0.5f), EscalaSeq::Mascara, FundoSeq::Transparente, true);
    VERIFICAR(corIgual(ler(saida, 10, 10), 200, 100, 50, 128));
    VERIFICAR(corIgual(ler(saida, 50, 50), 0, 0, 0, 0));

    secao("canvas diferente do clip");
    {
        const int W = 64, H = 32;
        Fbo menor(W, H);
        const GLuint entrada = criarTextura(W, H, imagemUniforme(W, H, 10, 220, 30));
        const std::vector<FeedSlice> direita = {criarSlice(7, 100.0f, 0.0f, 100.0f, 100.0f)};
        desenharCaso(inst, entrada, W, H, menor, direita, 200, 100, todas(direita, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
        VERIFICAR(corIgual(ler(menor, 48, 16), 10, 220, 30, 255));
        VERIFICAR(corIgual(ler(menor, 10, 16), 0, 0, 0, 255));
        VERIFICAR(corIgual(ler(menor, 31, 16), 0, 0, 0, 255) || corIgual(ler(menor, 33, 16), 10, 220, 30, 255));
        glDeleteTextures(1, &entrada);
    }

    secao("muitos slices");
    {
        std::vector<FeedSlice> grade;
        int uid = 1;
        for (int linha = 0; linha < 40; ++linha)
            for (int coluna = 0; coluna < 50; ++coluna)
                grade.push_back(criarSlice(uid++, static_cast<float>(coluna * 20), static_cast<float>(linha * 20), 18.0f, 18.0f));
        VERIFICAR(grade.size() == 2000);
        desenharCaso(inst, cor, T, T, saida, grade, 1000, 800, todas(grade, 1.0f), EscalaSeq::Mascara, FundoSeq::Preto, true);
        VERIFICAR(inst.ultimasInstancias() == 2000);
        VERIFICAR(glGetError() == GL_NO_ERROR);
        int acesos = 0;
        for (int x = 0; x < T; x += 4)
            if (ler(saida, x, 30).r > 100)
                ++acesos;
        VERIFICAR(acesos > 4);
        desenharCaso(inst, cor, T, T, saida, grade, 1000, 800, todas(grade, 0.25f), EscalaSeq::Esticar, FundoSeq::Transparente, false);
        VERIFICAR(inst.ultimasInstancias() == 2000);
        VERIFICAR(glGetError() == GL_NO_ERROR);
    }

    secao("passagem direta");
    {
        EfeitoSeq inexistente(kSequenciaId, "teste", describeSequencia());
        media::ParamValues valores;
        valores.set("sequencia", media::Json(4242));
        inexistente.setParams(media::ParamSet(describeSequencia(), valores));
        EffectContext ctx;
        ctx.topOrigin = true;
        inexistente.process(ctx, cor, T, T, saida);
        VERIFICAR(corIgual(ler(saida, 50, 50), 200, 100, 50, 255));
        VERIFICAR(inexistente.status().level == media::SourceStatus::Level::Warning);

        seq::SequenceSet conjunto;
        const int id = seq::criarSequencia(conjunto, "A", 2);
        seq::acharSequencia(conjunto, id)->passos[0] = {1};
        SequenceFeed::instance().set(conjunto, {});
        valores.set("sequencia", media::Json(id));
        valores.set("visao", media::Json(99));
        inexistente.setParams(media::ParamSet(describeSequencia(), valores));
        inexistente.process(ctx, cor, T, T, saida);
        VERIFICAR(inexistente.status().level == media::SourceStatus::Level::Warning);
        valores.set("visao", media::Json(0));
        inexistente.setParams(media::ParamSet(describeSequencia(), valores));
        inexistente.process(ctx, cor, T, T, saida);
        VERIFICAR(inexistente.status().level == media::SourceStatus::Level::Ok);
        VERIFICAR(corIgual(ler(saida, 50, 50), 200, 100, 50, 255));
        VERIFICAR(glGetError() == GL_NO_ERROR);
    }

    glDeleteTextures(1, &cor);
    glDeleteTextures(1, &metades);
}

}
