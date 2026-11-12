#pragma once

#include "fx/IEffect.h"
#include "fx/bsl/SliceFeed.h"
#include "media/SourceParams.h"
#include "render/Fbo.h"
#include "seq/SeqModel.h"

#include <glad/gl.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace seqfx {

extern int falhas;
extern int verificacoes;

struct Pixel
{
    int r = 0, g = 0, b = 0, a = 0;
};

inline void secao(const char* nome)
{
    std::printf("[%s]\n", nome);
}

inline void verificar(bool ok, const std::string& o_que)
{
    ++verificacoes;
    if (!ok) {
        ++falhas;
        std::printf("  FALHOU: %s\n", o_que.c_str());
    }
}

bool perto(int a, int b, int tol = 3);
bool corIgual(const Pixel& p, int r, int g, int b, int a, int tol = 3);
GLuint criarTextura(int w, int h, const std::vector<unsigned char>& dados);
std::vector<unsigned char> imagemUniforme(int w, int h, int r, int g, int b);
std::vector<unsigned char> imagemMetades(int w, int h);
Pixel ler(const bsl::Fbo& fbo, int x, int linha);
Pixel lerTextura(GLuint textura, int x, int linha);
bsl::fx::bsllight::FeedSlice criarSlice(int uid, float x, float y, float w, float h);
std::vector<bsl::fx::bsllight::FeedSlice> colunas(int quantidade, int largura, int altura);
void publicarSlices(const std::vector<bsl::fx::bsllight::FeedSlice>& slices, int canvasW, int canvasH);
void publicarConjunto(const bsl::seq::SequenceSet& conjunto);

std::vector<unsigned char> lerTudo(const bsl::Fbo& fbo);
void salvarPng(const bsl::Fbo& fbo, const std::string& nome);
std::vector<unsigned char> imagemGradiente(int w, int h);
std::vector<unsigned char> imagemVertical(int w, int h);
std::vector<bsl::fx::bsllight::FeedSlice> grade(int colunas, int linhas, int largura, int altura, int folga);
int diferentes(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b);

struct Rodador
{
    Rodador(std::unique_ptr<bsl::fx::IEffect> e, int largura, int altura, GLuint textura)
        : efeito(std::move(e)), w(largura), h(altura), saida(largura, altura), entrada(textura)
    {
        ctx.topOrigin = true;
    }

    void rodar()
    {
        efeito->setParams(bsl::media::ParamSet(efeito->paramDescs(), v));
        efeito->process(ctx, entrada, w, h, saida);
    }

    void definir(const char* id, const bsl::media::Json& valor) { v.set(id, valor); }
    Pixel px(int x, int linha) const { return ler(saida, x, linha); }
    bool ok() const { return efeito->status().level == bsl::media::SourceStatus::Level::Ok && efeito->status().text.empty(); }
    bool aviso() const { return efeito->status().level == bsl::media::SourceStatus::Level::Warning; }

    std::unique_ptr<bsl::fx::IEffect> efeito;
    bsl::fx::EffectContext ctx;
    bsl::media::ParamValues v;
    int w;
    int h;
    bsl::Fbo saida;
    GLuint entrada;
};

void testarBase();
void testarInstanciador();
void testarSequencia();
void testarDisparo();
void testarAtraso();
void testarAleatorios();
void testarFonte();
void testarContorno();
void testarLadrilhos();
void testarAmostrador();
void testarVarredura();
void testarBordaNeon();

}

#define VERIFICAR(cond) ::seqfx::verificar((cond), std::string(#cond) + " (linha " + std::to_string(__LINE__) + ")")
