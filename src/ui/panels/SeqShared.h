#pragma once

#include "seq/SeqHistory.h"
#include "seq/SeqModel.h"
#include "seq/SeqView.h"
#include "ui/PanelContext.h"

#include <string>
#include <utility>

namespace bsl::ui::sequi {

enum class Ferramenta { Retangulo, Pincel };

struct BordaSelecionada
{
    bool ativa = false;
    bool eixoX = true;
    int indice = -1;
};

struct Estado
{
    int sequenciaId = 0;
    int visaoId = seq::kVisaoTudo;
    int passo = 0;
    Ferramenta ferramenta = Ferramenta::Retangulo;
    bool fantasma = true;
    bool mostrarComposicao = true;
    bool mostrarNomes = true;
    bool editarGrade = false;
    BordaSelecionada borda;
    seq::VistaCanvas vista;
    bool ajustar = true;
    seq::SeqHistory historico;
    bool gestoAberto = false;
    seq::SequenceSet antesGesto;
    std::string status;
    int ultimoPasso = -1;
    bool seguirPasso = false;
    int envelopeId = 0;
};

Estado& estado();

seq::Sequencia* sequenciaAtual(PanelContext& ctx);
seq::Visao* visaoAtual(PanelContext& ctx);
int totalDeSlices(const PanelContext& ctx);
int slicesAcesosNoPasso(const seq::Sequencia& sequencia, int passo);

void abrirGesto(PanelContext& ctx);
void fecharGesto(PanelContext& ctx);

template <typename Funcao>
void editar(PanelContext& ctx, Funcao&& funcao)
{
    const bool jaAberto = estado().gestoAberto;
    if (!jaAberto)
        abrirGesto(ctx);
    std::forward<Funcao>(funcao)();
    if (!jaAberto)
        fecharGesto(ctx);
}

void desfazerSequencias(PanelContext& ctx);
void refazerSequencias(PanelContext& ctx);
void regerarSequenciaAtual(PanelContext& ctx);
void selecionarPasso(PanelContext& ctx, int passo);
void navegarPassos(PanelContext& ctx, int delta);
bool tratarTeclasDePasso(PanelContext& ctx);

}
