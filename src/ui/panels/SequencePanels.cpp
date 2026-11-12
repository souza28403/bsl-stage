#include "ui/panels/SequencePanels.h"

#include "app/Qa.h"
#include "seq/SeqEdit.h"
#include "seq/SeqFactory.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/SeqShared.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace bsl::ui {

namespace sequi {

Estado& estado()
{
    static Estado e;
    return e;
}

seq::Sequencia* sequenciaAtual(PanelContext& ctx)
{
    return seq::acharSequencia(ctx.composition.sequences, estado().sequenciaId);
}

seq::Visao* visaoAtual(PanelContext& ctx)
{
    const int id = estado().visaoId;
    return id == seq::kVisaoTudo ? nullptr : seq::acharVisao(ctx.composition.sequences, id);
}

int totalDeSlices(const PanelContext& ctx)
{
    int total = 0;
    for (const Screen& tela : ctx.output.screens)
        for (const Slice& s : tela.slices)
            if (s.uid > 0)
                ++total;
    return total;
}

int slicesAcesosNoPasso(const seq::Sequencia& sequencia, int passo)
{
    if (passo < 0 || passo >= static_cast<int>(sequencia.passos.size()))
        return 0;
    return static_cast<int>(sequencia.passos[static_cast<size_t>(passo)].size());
}

void abrirGesto(PanelContext& ctx)
{
    Estado& e = estado();
    if (e.gestoAberto)
        return;
    e.antesGesto = ctx.composition.sequences;
    e.gestoAberto = true;
}

void fecharGesto(PanelContext& ctx)
{
    Estado& e = estado();
    if (!e.gestoAberto)
        return;
    e.gestoAberto = false;
    e.historico.confirmar(e.antesGesto, ctx.composition.sequences);
    e.antesGesto = seq::SequenceSet{};
}

void desfazerSequencias(PanelContext& ctx)
{
    Estado& e = estado();
    fecharGesto(ctx);
    e.status = e.historico.desfazer(ctx.composition.sequences) ? "Desfeito" : "Nada para desfazer";
}

void refazerSequencias(PanelContext& ctx)
{
    Estado& e = estado();
    fecharGesto(ctx);
    e.status = e.historico.refazer(ctx.composition.sequences) ? "Refeito" : "Nada para refazer";
}

void regerarSequenciaAtual(PanelContext& ctx)
{
    seq::Sequencia* s = sequenciaAtual(ctx);
    if (!s || !s->temGerador)
        return;
    seq::aplicarGerador(*s, seq::coletarGeometria(ctx.output));
    Estado& e = estado();
    e.passo = std::clamp(e.passo, 0, std::max(static_cast<int>(s->passos.size()) - 1, 0));
}

void selecionarPasso(PanelContext& ctx, int passo)
{
    Estado& e = estado();
    const seq::Sequencia* s = sequenciaAtual(ctx);
    const int n = s ? static_cast<int>(s->passos.size()) : 0;
    e.passo = n > 0 ? std::clamp(passo, 0, n - 1) : 0;
    e.seguirPasso = true;
}

void navegarPassos(PanelContext& ctx, int delta)
{
    const seq::Sequencia* s = sequenciaAtual(ctx);
    if (!s)
        return;
    selecionarPasso(ctx, seq::moverSelecao(estado().passo, delta, static_cast<int>(s->passos.size())));
}

bool tratarTeclasDePasso(PanelContext& ctx)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return false;
    seq::Sequencia* s = sequenciaAtual(ctx);
    if (!s || io.KeyCtrl)
        return false;
    Estado& e = estado();
    const int n = static_cast<int>(s->passos.size());
    const bool esquerda = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
    const bool direita = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);
    if ((esquerda || direita) && io.KeyAlt) {
        const int alvo = e.passo + (direita ? 1 : -1);
        if (alvo >= 0 && alvo < n) {
            editar(ctx, [&] { seq::trocarPassos(*s, e.passo, alvo); });
            selecionarPasso(ctx, alvo);
        }
        return true;
    }
    if (esquerda || direita) {
        navegarPassos(ctx, direita ? 1 : -1);
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        selecionarPasso(ctx, 0);
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        selecionarPasso(ctx, n - 1);
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        editar(ctx, [&] { seq::limparPasso(*s, e.passo); });
        return true;
    }
    return false;
}

}

namespace {

using namespace sequi;

struct Renomeacao
{
    int tipo = 0;
    int id = 0;
    char texto[128] = {};
    bool foco = false;
};
Renomeacao g_renomear;

ImU32 cor(const ImVec4& c, float alfa = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alfa));
}

void dim(const char* texto)
{
    ImGui::TextColored(theme::kTextDim, "%s", texto);
}

bool painelSeqFocado(bool comEnvelopes)
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g || !g->NavWindow)
        return false;
    const ImGuiWindow* nav = g->NavWindow;
    for (const char* nome : {panel::kSeqList, panel::kSeqCanvas, panel::kSeqSteps, panel::kSeqGen, panel::kSeqEnv}) {
        if (!comEnvelopes && std::strcmp(nome, panel::kSeqEnv) == 0)
            continue;
        const ImGuiWindow* w = ImGui::FindWindowByName(nome);
        if (w && (nav == w || nav->RootWindow == w))
            return true;
    }
    return false;
}

void iniciarRenomear(int tipo, int id, const std::string& nome)
{
    g_renomear.tipo = tipo;
    g_renomear.id = id;
    g_renomear.foco = true;
    std::snprintf(g_renomear.texto, sizeof(g_renomear.texto), "%s", nome.c_str());
}

bool renomeando(int tipo, int id)
{
    return g_renomear.tipo == tipo && g_renomear.id == id;
}

int campoRenomear()
{
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (g_renomear.foco) {
        ImGui::SetKeyboardFocusHere();
        g_renomear.foco = false;
    }
    const bool enter = ImGui::InputText("##renomear", g_renomear.texto, sizeof(g_renomear.texto),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (enter)
        return 1;
    if (ImGui::IsItemDeactivated())
        return ImGui::IsKeyPressed(ImGuiKey_Escape, false) ? 2 : 1;
    return 0;
}

void soltarLinha(PanelContext& ctx, bool sequencias, int de, int para)
{
    seq::SequenceSet& c = ctx.composition.sequences;
    Estado& e = estado();
    const bool copiar = ImGui::GetIO().KeyAlt;
    editar(ctx, [&] {
        if (sequencias) {
            if (de < 0 || de >= static_cast<int>(c.sequencias.size()))
                return;
            if (copiar) {
                const int novo = seq::duplicarSequencia(c, c.sequencias[static_cast<size_t>(de)].id);
                if (novo > 0) {
                    seq::moverSequencia(c, static_cast<int>(c.sequencias.size()) - 1, para);
                    e.sequenciaId = novo;
                }
            } else {
                seq::moverSequencia(c, de, para);
            }
        } else {
            if (de < 0 || de >= static_cast<int>(c.visoes.size()))
                return;
            if (copiar) {
                const int novo = seq::duplicarVisao(c, c.visoes[static_cast<size_t>(de)].id);
                if (novo > 0) {
                    seq::moverVisao(c, static_cast<int>(c.visoes.size()) - 1, para);
                    e.visaoId = novo;
                }
            } else {
                seq::moverVisao(c, de, para);
            }
        }
    });
}

void centralizarTexto(ImDrawList* dl, ImFont* fonte, float tamanho, ImVec2 centro, ImU32 cor32, const char* texto)
{
    const ImVec2 ts = fonte->CalcTextSizeA(tamanho, FLT_MAX, 0.0f, texto);
    dl->AddText(fonte, tamanho, ImVec2(centro.x - ts.x * 0.5f, centro.y - ts.y * 0.5f), cor32, texto);
}

}

void seqUiReset()
{
    Estado& e = estado();
    e.historico.limpar();
    e.gestoAberto = false;
    e.antesGesto = seq::SequenceSet{};
    e.sequenciaId = 0;
    e.visaoId = seq::kVisaoTudo;
    e.passo = 0;
    e.borda = {};
    e.ajustar = true;
    e.status.clear();
    g_renomear = {};
}

SeqUiInfo seqUiInfo()
{
    const Estado& e = estado();
    SeqUiInfo i;
    i.sequenceId = e.sequenciaId;
    i.viewId = e.visaoId;
    i.step = e.passo;
    i.tool = static_cast<int>(e.ferramenta);
    i.canUndo = e.historico.podeDesfazer();
    i.canRedo = e.historico.podeRefazer();
    i.gestureOpen = e.gestoAberto;
    i.gridEdit = e.editarGrade;
    i.zoom = e.vista.zoom;
    i.fit = e.ajustar;
    i.ghost = e.fantasma;
    i.backdrop = e.mostrarComposicao;
    i.status = e.status;
    return i;
}

void sequencesPrepare(PanelContext& ctx)
{
    Estado& e = estado();
    seq::SequenceSet& c = ctx.composition.sequences;
    const ImGuiIO& io = ImGui::GetIO();

    if (e.gestoAberto && !ImGui::IsAnyItemActive() && !io.MouseDown[0] && !io.MouseDown[2])
        fecharGesto(ctx);

    if (!seq::acharSequencia(c, e.sequenciaId))
        e.sequenciaId = c.sequencias.empty() ? 0 : c.sequencias.front().id;
    if (e.visaoId != seq::kVisaoTudo && !seq::acharVisao(c, e.visaoId))
        e.visaoId = seq::kVisaoTudo;
    if (const seq::Sequencia* s = seq::acharSequencia(c, e.sequenciaId))
        e.passo = std::clamp(e.passo, 0, std::max(static_cast<int>(s->passos.size()) - 1, 0));
    else
        e.passo = 0;

    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (painelSeqFocado(false) && !io.WantTextInput && !popup && io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, true) && !io.KeyShift)
            desfazerSequencias(ctx);
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, true) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, true)))
            refazerSequencias(ctx);
    }
}

void drawSeqListPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kSeqList);

    Estado& e = estado();
    seq::SequenceSet& c = ctx.composition.sequences;
    const float lado = ImGui::GetFrameHeight();
    const ImGuiStyle& estilo = ImGui::GetStyle();
    int de = -1;
    int para = -1;
    bool linhasSequencias = true;

    ImGui::SeparatorText("Sequências");
    if (ImGui::Button("+ Sequência##seq_nova", ImVec2(-FLT_MIN, 0.0f))) {
        editar(ctx, [&] {
            const std::string nome = "Sequência " + std::to_string(c.sequencias.size() + 1);
            const int id = seq::criarSequencia(c, nome, 8);
            if (id > 0) {
                e.sequenciaId = id;
                e.passo = 0;
            }
        });
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cria uma sequência de 8 passos vazios");

    int excluir = 0;
    for (int i = 0; i < static_cast<int>(c.sequencias.size()); ++i) {
        seq::Sequencia& s = c.sequencias[static_cast<size_t>(i)];
        ImGui::PushID(s.id);
        iconButton(ICON_GRIP, "##seq_alca", "Arraste para reordenar (Alt copia)", ImVec2(lado * 0.7f, lado), IconButton_Flat);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("BSL_SEQ_LINHA", &i, sizeof(int));
            ImGui::TextUnformatted(s.nome.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SameLine(0.0f, estilo.ItemInnerSpacing.x);
        if (renomeando(1, s.id)) {
            const int r = campoRenomear();
            if (r == 1)
                editar(ctx, [&] { seq::renomearSequencia(c, s.id, g_renomear.texto); });
            if (r != 0)
                g_renomear = {};
        } else {
            const float reserva = lado + dp(52.0f);
            const std::string rotulo = (s.nome.empty() ? std::string("(sem nome)") : s.nome) + "##seq_nome";
            if (ImGui::Selectable(rotulo.c_str(), s.id == e.sequenciaId, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x - reserva, 0.0f))) {
                e.sequenciaId = s.id;
                e.passo = std::clamp(e.passo, 0, std::max(static_cast<int>(s.passos.size()) - 1, 0));
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                iniciarRenomear(1, s.id, s.nome);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BSL_SEQ_LINHA")) {
                    de = *static_cast<const int*>(p->Data);
                    para = i;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            char contagem[24];
            std::snprintf(contagem, sizeof(contagem), "%d p.", static_cast<int>(s.passos.size()));
            ImGui::TextColored(theme::kTextDim, "%s", contagem);
            ImGui::SameLine();
            if (iconButton(ICON_X, "##seq_excluir", "Excluir a sequência", ImVec2(lado, lado), IconButton_Flat))
                excluir = s.id;
        }
        ImGui::PopID();
    }
    if (excluir > 0) {
        editar(ctx, [&] { seq::excluirSequencia(c, excluir); });
    } else if (de >= 0 && para >= 0) {
        soltarLinha(ctx, linhasSequencias, de, para);
        de = para = -1;
    }
    if (c.sequencias.empty())
        dim("Nenhuma sequência. Crie uma para começar.");

    linhasSequencias = false;
    ImGui::Spacing();
    ImGui::SeparatorText("Visões");
    if (ImGui::Button("+ Visão##seq_visao_nova", ImVec2(-FLT_MIN, 0.0f))) {
        editar(ctx, [&] {
            const std::string nome = "Visão " + std::to_string(c.visoes.size() + 1);
            const int id = seq::criarVisao(c, nome);
            if (id > 0)
                e.visaoId = id;
        });
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cria um grupo de slices. Alt + clique no Canvas liga e desliga slices na visão selecionada");

    ImGui::PushID("visao_tudo");
    ImGui::Dummy(ImVec2(lado * 0.7f, 1.0f));
    ImGui::SameLine(0.0f, estilo.ItemInnerSpacing.x);
    if (ImGui::Selectable("Tudo##visao_tudo_nome", e.visaoId == seq::kVisaoTudo))
        e.visaoId = seq::kVisaoTudo;
    ImGui::PopID();

    int excluirVisao = 0;
    for (int i = 0; i < static_cast<int>(c.visoes.size()); ++i) {
        seq::Visao& v = c.visoes[static_cast<size_t>(i)];
        ImGui::PushID(v.id + 100000);
        iconButton(ICON_GRIP, "##visao_alca", "Arraste para reordenar (Alt copia)", ImVec2(lado * 0.7f, lado), IconButton_Flat);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("BSL_SEQ_VISAO", &i, sizeof(int));
            ImGui::TextUnformatted(v.nome.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SameLine(0.0f, estilo.ItemInnerSpacing.x);
        if (renomeando(2, v.id)) {
            const int r = campoRenomear();
            if (r == 1)
                editar(ctx, [&] { seq::renomearVisao(c, v.id, g_renomear.texto); });
            if (r != 0)
                g_renomear = {};
        } else {
            const float reserva = lado * 2.0f + dp(52.0f);
            const std::string rotulo = (v.nome.empty() ? std::string("(sem nome)") : v.nome) + "##visao_nome";
            if (ImGui::Selectable(rotulo.c_str(), v.id == e.visaoId, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x - reserva, 0.0f)))
                e.visaoId = v.id;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                iniciarRenomear(2, v.id, v.nome);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BSL_SEQ_VISAO")) {
                    de = *static_cast<const int*>(p->Data);
                    para = i;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::TextColored(theme::kTextDim, "%d", static_cast<int>(v.ligados.size()));
            ImGui::SameLine();
            if (iconButton(v.previaVisivel ? ICON_EYE : ICON_EYE_OFF, "##visao_olho", "Mostrar a visão no Canvas",
                           ImVec2(lado, lado), IconButton_Flat))
                editar(ctx, [&] { v.previaVisivel = !v.previaVisivel; });
            ImGui::SameLine();
            if (iconButton(ICON_X, "##visao_excluir", "Excluir a visão", ImVec2(lado, lado), IconButton_Flat))
                excluirVisao = v.id;
        }
        ImGui::PopID();
    }
    if (excluirVisao > 0)
        editar(ctx, [&] { seq::excluirVisao(c, excluirVisao); });
    else if (de >= 0 && para >= 0)
        soltarLinha(ctx, linhasSequencias, de, para);

    const std::vector<int> orfaos = seq::uidsOrfaos(c, ctx.output);
    if (!orfaos.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(theme::kWarning, ICON_ALERT " %d slice(s) sem correspondência", static_cast<int>(orfaos.size()));
        if (ImGui::Button("Religar slices##seq_religar", ImVec2(-FLT_MIN, 0.0f))) {
            editar(ctx, [&] {
                const int n = seq::religarSlices(c, ctx.output);
                e.status = std::to_string(n) + " slice(s) religado(s)";
            });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Casa os slices que sumiram por tela e nome, e depois por posição");
    }

    endPanel();
    ImGui::PopStyleVar();
}

void drawSeqStepsPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kSeqSteps, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    Estado& e = estado();
    seq::Sequencia* s = sequenciaAtual(ctx);
    if (!s) {
        dim("Selecione ou crie uma sequência na lista.");
        endPanel();
        ImGui::PopStyleVar();
        return;
    }

    const int n = static_cast<int>(s->passos.size());
    const float lado = ImGui::GetFrameHeight();
    const ImVec2 quadrado(lado, lado);

    if (iconButton(ICON_PLUS, "##seq_passo_mais", "Inserir um passo depois do atual", quadrado)) {
        editar(ctx, [&] {
            if (seq::inserirPasso(*s, e.passo + 1))
                selecionarPasso(ctx, e.passo + 1);
        });
    }
    ImGui::SameLine();
    if (iconButton(ICON_MINUS, "##seq_passo_menos", "Remover o passo atual", quadrado)) {
        editar(ctx, [&] {
            if (seq::removerPasso(*s, e.passo))
                selecionarPasso(ctx, e.passo);
        });
    }
    ImGui::SameLine();
    int total = n;
    if (hotInt("##seq_passos_total", total, 1, seq::kMaxPassos, 1.0f, "%d", "passos", INT_MIN, dp(110.0f))) {
        abrirGesto(ctx);
        seq::definirTamanho(*s, total);
        selecionarPasso(ctx, e.passo);
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + dp(10.0f));
    if (iconButton(ICON_ARROW_RIGHT_TO_LINE, "##seq_repetir", "Repetir: copia o passo atual para todos os seguintes", quadrado))
        editar(ctx, [&] { seq::repetirParaFrente(*s, e.passo); });
    ImGui::SameLine();
    if (iconButton(ICON_ARROW_LEFT_RIGHT, "##seq_vaievolta", "Vai e volta: acrescenta os passos em ordem inversa", quadrado))
        editar(ctx, [&] { seq::vaiEVolta(*s); });
    ImGui::SameLine();
    if (ImGui::Button("Limpar passo##seq_limpar_passo"))
        editar(ctx, [&] { seq::limparPasso(*s, e.passo); });
    ImGui::SameLine();
    if (ImGui::Button("Limpar tudo##seq_limpar_tudo"))
        editar(ctx, [&] { seq::limparSequencia(*s); });
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + dp(10.0f));
    pushMono();
    ImGui::TextColored(theme::kTextDim, "Passo %d de %d  |  %d aceso(s) de %d", e.passo + 1, n, slicesAcesosNoPasso(*s, e.passo),
                       totalDeSlices(ctx));
    popMono();
    if (seq::passosDiferentes(ctx.composition.sequences)) {
        ImGui::SameLine();
        ImGui::TextColored(theme::kWarning, ICON_ALERT " As sequências têm números de passos diferentes");
    }

    const float larguraCelula = dp(38.0f);
    const float alturaCelula = dp(46.0f);
    ImGui::BeginChild("##seq_passos_rolagem", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f)
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * larguraCelula * 3.0f);
    if (e.seguirPasso) {
        const float x0 = static_cast<float>(e.passo) * larguraCelula;
        const float visivel = ImGui::GetWindowWidth();
        if (x0 < ImGui::GetScrollX())
            ImGui::SetScrollX(x0);
        else if (x0 + larguraCelula > ImGui::GetScrollX() + visivel)
            ImGui::SetScrollX(x0 + larguraCelula - visivel);
        e.seguirPasso = false;
    }

    const ImVec2 origem = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##seq_passos_area", ImVec2(larguraCelula * static_cast<float>(n), alturaCelula));
    for (ImGuiKey k : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Delete, ImGuiMod_Alt})
        ImGui::SetItemKeyOwner(k);
    static int arrastoOrigem = -1;
    const ImVec2 mouse = io.MousePos;
    if (ImGui::IsItemActivated()) {
        const int i = seq::indiceDaCelula(mouse.x, origem.x, larguraCelula, n);
        if (i >= 0) {
            selecionarPasso(ctx, i);
            arrastoOrigem = i;
        }
    }
    if (ImGui::IsItemDeactivated() && arrastoOrigem >= 0) {
        const int destino = seq::indiceDaCelula(mouse.x, origem.x, larguraCelula, n);
        const seq::ArrastoPasso a = seq::resolverArrasto(arrastoOrigem, destino, n, io.KeyCtrl);
        if (a.tipo == seq::TipoArrasto::Copiar) {
            editar(ctx, [&] { seq::copiarPasso(*s, a.origem, a.destino); });
            selecionarPasso(ctx, a.destino);
        } else if (a.tipo == seq::TipoArrasto::CopiarACadaK) {
            editar(ctx, [&] { seq::copiarPassoACadaK(*s, a.origem, a.k); });
        }
        arrastoOrigem = -1;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        tratarTeclasDePasso(ctx);

    int primeira = 0;
    int ultima = -1;
    seq::faixaVisivel(ImGui::GetScrollX(), ImGui::GetWindowWidth(), larguraCelula, n, primeira, ultima);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* mono = fonts().mono;
    const float corpo = mono->LegacySize * uiScale();
    const bool arrastando = arrastoOrigem >= 0 && ImGui::IsItemActive();
    const int sobre = seq::indiceDaCelula(mouse.x, origem.x, larguraCelula, n);
    for (int i = primeira; i <= ultima; ++i) {
        const ImVec2 a(origem.x + static_cast<float>(i) * larguraCelula + 1.0f, origem.y);
        const ImVec2 b(a.x + larguraCelula - 2.0f, a.y + alturaCelula);
        const int acesos = slicesAcesosNoPasso(*s, i);
        const bool atual = i == e.passo;
        const ImVec4& fundo = atual ? theme::kAccent : (acesos > 0 ? theme::kPanelRaised : theme::kField);
        dl->AddRectFilled(a, b, cor(fundo), rounding());
        dl->AddRect(a, b, cor(atual ? theme::kAccentHover : theme::kBorder), rounding());
        const ImU32 texto = cor(atual ? theme::kOnAccent : theme::kText);
        char num[16];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        centralizarTexto(dl, mono, corpo, ImVec2((a.x + b.x) * 0.5f, a.y + alturaCelula * 0.32f), texto, num);
        if (acesos > 0) {
            std::snprintf(num, sizeof(num), "%d", acesos);
            centralizarTexto(dl, mono, corpo * 0.8f, ImVec2((a.x + b.x) * 0.5f, a.y + alturaCelula * 0.72f),
                             cor(atual ? theme::kOnAccent : theme::kTextDim), num);
        }
        if (arrastando && i == sobre && i != arrastoOrigem)
            dl->AddRect(a, b, cor(theme::kTextStrong), rounding(), 0, std::max(1.5f, dp(2.0f)));
        if (qa::enabled())
            qa::noteRect("seq:passo:" + std::to_string(i), a.x, a.y, b.x, b.y);
    }
    if (arrastando && sobre >= 0 && sobre != arrastoOrigem) {
        const float cx0 = origem.x + (static_cast<float>(arrastoOrigem) + 0.5f) * larguraCelula;
        const float cx1 = origem.x + (static_cast<float>(sobre) + 0.5f) * larguraCelula;
        const float y = origem.y + alturaCelula + dp(6.0f);
        dl->AddLine(ImVec2(cx0, y), ImVec2(cx1, y), cor(theme::kAccent), std::max(1.5f, dp(2.0f)));
        dl->AddCircleFilled(ImVec2(cx1, y), dp(4.0f), cor(theme::kAccent));
    }
    ImGui::EndChild();

    endPanel();
    ImGui::PopStyleVar();
}

}
