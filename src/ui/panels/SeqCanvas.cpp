#include "ui/panels/SeqCanvas.h"

#include "app/Qa.h"
#include "render/Compositor.h"
#include "seq/SeqEdit.h"
#include "seq/SeqFactory.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/SeqShared.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

using namespace sequi;

enum class TipoGesto { Nada, Pan, Retangulo, Pincel, Borda };

struct Gesto
{
    TipoGesto tipo = TipoGesto::Nada;
    ImVec2 inicioTela{};
    seq::PontoF inicioConteudo;
    seq::PontoF anterior;
    seq::VistaCanvas vistaInicial;
    seq::ModoEdicao modo = seq::ModoEdicao::Inverter;
    seq::AlvoEdicao alvo = seq::AlvoEdicao::Passo;
    seq::GestoPincel pincel;
    bool moveu = false;
    bool bordaX = true;
    int bordaIndice = -1;
};
Gesto g_gesto;

struct Barra
{
    float direita = 0.0f;
    bool primeiro = true;
    float folga = 0.0f;

    Barra() { direita = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }
    void grupo() { folga = dp(10.0f); }
    void lugar(float largura)
    {
        if (!primeiro) {
            const float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + folga;
            if (x + largura <= direita)
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + folga);
        }
        primeiro = false;
        folga = 0.0f;
    }
};

bool barraIcone(Barra& barra, const char* icone, const char* id, const char* dica, bool ativo = false)
{
    barra.lugar(ImGui::GetFrameHeight());
    return iconButton(icone, id, dica, ImVec2(0.0f, 0.0f), ativo ? IconButton_Active : 0);
}

bool barraChip(Barra& barra, const char* rotulo, bool ligado, const char* dica)
{
    const float largura = ImGui::CalcTextSize(rotulo, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    barra.lugar(largura);
    if (ligado) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    }
    const bool apertado = ImGui::Button(rotulo);
    if (ligado)
        ImGui::PopStyleColor(4);
    if (dica && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", dica);
    return apertado;
}

ImU32 cor(const ImVec4& c, float alfa = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alfa));
}

constexpr ImU32 kPaletaVisoes[] = {IM_COL32(47, 127, 224, 255), IM_COL32(30, 167, 90, 255), IM_COL32(245, 165, 36, 255),
                                   IM_COL32(190, 90, 220, 255), IM_COL32(40, 200, 210, 255), IM_COL32(230, 120, 60, 255)};

struct SliceInfo
{
    const Screen* tela = nullptr;
    const Slice* slice = nullptr;
};

void aplicar(PanelContext& ctx, const std::vector<int>& uids, seq::ModoEdicao modo, seq::AlvoEdicao alvo)
{
    if (uids.empty())
        return;
    Estado& e = estado();
    if (alvo == seq::AlvoEdicao::Passo) {
        seq::Sequencia* s = sequenciaAtual(ctx);
        if (s && seq::passoValido(*s, e.passo))
            seq::aplicarEdicao(s->passos[static_cast<size_t>(e.passo)], uids, modo);
    } else if (alvo == seq::AlvoEdicao::Visao) {
        if (seq::Visao* v = visaoAtual(ctx))
            seq::aplicarEdicaoVisao(*v, uids, modo);
    }
}

void tornarGradeManual(PanelContext& ctx, seq::Sequencia& s)
{
    if (s.gerador.bordasManuais)
        return;
    const seq::Grade grade = seq::gradeDoGerador(seq::coletarGeometria(ctx.output), s.gerador);
    s.gerador.bordasManuais = true;
    s.gerador.bordasX = grade.bordasX;
    s.gerador.bordasY = grade.bordasY;
}

std::vector<float>& bordasDoEixo(seq::Sequencia& s, bool eixoX)
{
    return eixoX ? s.gerador.bordasX : s.gerador.bordasY;
}

}

void drawSeqCanvasPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kSeqCanvas, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    Estado& e = estado();
    seq::SequenceSet& conjunto = ctx.composition.sequences;
    seq::Sequencia* sequencia = sequenciaAtual(ctx);
    const ImGuiIO& io = ImGui::GetIO();
    const float cw = static_cast<float>(std::max(ctx.canvas.width(), 1));
    const float ch = static_cast<float>(std::max(ctx.canvas.height(), 1));

    {
        Barra barra;
        if (barraChip(barra, "Retângulo##seq_ferr_ret", e.ferramenta == Ferramenta::Retangulo,
                      "Clique inverte um slice; arrastar desenha um retângulo que inverte os slices tocados"))
            e.ferramenta = Ferramenta::Retangulo;
        if (barraChip(barra, "Pincel##seq_ferr_pincel", e.ferramenta == Ferramenta::Pincel,
                      "Arraste por cima dos slices: cada um é invertido uma vez por gesto"))
            e.ferramenta = Ferramenta::Pincel;
        barra.grupo();
        if (barraIcone(barra, ICON_LAYERS, "##seq_fantasma", "Fantasma: contorno dos slices acesos no passo anterior", e.fantasma))
            e.fantasma = !e.fantasma;
        if (barraIcone(barra, ICON_IMAGE, "##seq_composicao", "Mostrar a composição ao vivo atrás dos slices", e.mostrarComposicao))
            e.mostrarComposicao = !e.mostrarComposicao;
        if (barraIcone(barra, ICON_TYPE, "##seq_nomes", "Mostrar os nomes dos slices", e.mostrarNomes))
            e.mostrarNomes = !e.mostrarNomes;
        barra.grupo();
        if (barraIcone(barra, ICON_MAXIMIZE, "##seq_ajustar", "Ajustar à vista (Ctrl+0)"))
            e.ajustar = true;
        barra.lugar(ImGui::CalcTextSize("100%").x + ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::Button("100%##seq_100")) {
            e.ajustar = false;
            e.vista.zoom = 1.0f;
            e.vista.panX = 0.0f;
            e.vista.panY = 0.0f;
        }
        barra.grupo();
        ImGui::BeginDisabled(!e.historico.podeDesfazer());
        if (barraIcone(barra, ICON_UNDO, "##seq_desfazer", "Desfazer (Ctrl+Z)"))
            desfazerSequencias(ctx);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!e.historico.podeRefazer());
        if (barraIcone(barra, ICON_REDO, "##seq_refazer", "Refazer (Ctrl+Y)"))
            refazerSequencias(ctx);
        ImGui::EndDisabled();
        barra.grupo();
        ImGui::BeginDisabled(!sequencia);
        if (barraIcone(barra, ICON_CHEVRON_LEFT, "##seq_passo_ant", "Passo anterior (seta esquerda)"))
            navegarPassos(ctx, -1);
        char texto[40];
        std::snprintf(texto, sizeof(texto), "Passo %d/%d",
                      sequencia ? e.passo + 1 : 0, sequencia ? static_cast<int>(sequencia->passos.size()) : 0);
        barra.lugar(ImGui::CalcTextSize(texto).x);
        ImGui::AlignTextToFramePadding();
        pushMono();
        ImGui::TextUnformatted(texto);
        popMono();
        if (barraIcone(barra, ICON_CHEVRON_RIGHT, "##seq_passo_prox", "Próximo passo (seta direita)"))
            navegarPassos(ctx, 1);
        ImGui::EndDisabled();
    }

    const float legendaAltura = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 disponivel = ImGui::GetContentRegionAvail();
    const ImVec2 tamanho(disponivel.x, disponivel.y - legendaAltura);
    if (tamanho.x < dp(60.0f) || tamanho.y < dp(60.0f)) {
        endPanel();
        ImGui::PopStyleVar();
        return;
    }
    ImGui::InvisibleButton("##seq_vista", tamanho, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    for (ImGuiKey k : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Delete, ImGuiKey_Backspace,
                       ImGuiKey_Space, ImGuiMod_Alt})
        ImGui::SetItemKeyOwner(k);
    const bool sobre = ImGui::IsItemHovered();
    const bool ativo = ImGui::IsItemActive();
    const bool ativou = ImGui::IsItemActivated();
    const bool desativou = ImGui::IsItemDeactivated();
    const bool focado = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 vpMin = ImGui::GetItemRectMin();
    const ImVec2 vpMax = ImGui::GetItemRectMax();
    const seq::PontoF origem{vpMin.x, vpMin.y};
    const ImVec2 mouse = io.MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (e.ajustar)
        e.vista = seq::ajustarVista(cw, ch, tamanho.x, tamanho.y, dp(24.0f));

    auto paraTela = [&](float x, float y) {
        const seq::PontoF p = seq::paraTela(e.vista, origem, x, y);
        return ImVec2(p.x, p.y);
    };
    auto doMouse = [&]() { return seq::paraConteudo(e.vista, origem, mouse.x, mouse.y); };

    if (sobre && io.MouseWheel != 0.0f && g_gesto.tipo == TipoGesto::Nada) {
        e.vista = seq::zoomNoPonto(e.vista, origem, mouse.x, mouse.y, io.MouseWheel);
        e.ajustar = false;
    }
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (focado && !io.WantTextInput && !popup && g_gesto.tipo == TipoGesto::Nada) {
        const seq::PontoF centro{vpMin.x + tamanho.x * 0.5f, vpMin.y + tamanho.y * 0.5f};
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, true))) {
            e.vista = seq::zoomPorFator(e.vista, origem, centro.x, centro.y, 1.25f);
            e.ajustar = false;
        } else if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, true))) {
            e.vista = seq::zoomPorFator(e.vista, origem, centro.x, centro.y, 0.8f);
            e.ajustar = false;
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) {
            e.ajustar = true;
        } else if (!io.KeyCtrl) {
            bool tratada = false;
            if (e.editarGrade && sequencia && sequencia->temGerador && e.borda.ativa
                && (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false))) {
                editar(ctx, [&] {
                    if (seq::removerBorda(bordasDoEixo(*sequencia, e.borda.eixoX), e.borda.indice))
                        regerarSequenciaAtual(ctx);
                });
                e.borda = {};
                tratada = true;
            } else if (e.editarGrade && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                e.borda = {};
                tratada = true;
            }
            if (!tratada)
                tratarTeclasDePasso(ctx);
        }
    }

    std::vector<seq::SliceCanvas> lista;
    std::vector<SliceInfo> infos;
    for (const Screen& tela : ctx.output.screens)
        for (const Slice& sl : tela.slices) {
            if (sl.uid <= 0)
                continue;
            const float x0 = static_cast<float>(sl.input.x);
            const float y0 = static_cast<float>(sl.input.y);
            lista.push_back({sl.uid, {x0, y0, x0 + static_cast<float>(sl.input.w), y0 + static_cast<float>(sl.input.h)}});
            infos.push_back({&tela, &sl});
        }

    const bool espaco = ImGui::IsKeyDown(ImGuiKey_Space) && focado;
    const bool gradeAtiva = e.editarGrade && sequencia && sequencia->temGerador;
    seq::Grade grade;
    if (gradeAtiva)
        grade = seq::gradeDoGerador(seq::coletarGeometria(ctx.output), sequencia->gerador);
    const float tolerancia = dp(6.0f) / std::max(e.vista.zoom, seq::kZoomMinimo);

    if (ativou) {
        e.status.clear();
        Gesto novo;
        novo.inicioTela = mouse;
        novo.inicioConteudo = doMouse();
        novo.anterior = novo.inicioConteudo;
        novo.vistaInicial = e.vista;
        if (io.MouseDown[ImGuiMouseButton_Middle] || (espaco && io.MouseDown[ImGuiMouseButton_Left])) {
            novo.tipo = TipoGesto::Pan;
        } else if (gradeAtiva) {
            const int ix = seq::acharBorda(grade.bordasX, novo.inicioConteudo.x, tolerancia);
            const int iy = ix >= 0 ? -1 : seq::acharBorda(grade.bordasY, novo.inicioConteudo.y, tolerancia);
            if (io.KeyAlt) {
                abrirGesto(ctx);
                tornarGradeManual(ctx, *sequencia);
                const bool horizontal = io.KeyShift;
                const int novoIndice = seq::inserirBorda(bordasDoEixo(*sequencia, !horizontal),
                                                          horizontal ? novo.inicioConteudo.y : novo.inicioConteudo.x);
                if (novoIndice >= 0) {
                    novo.tipo = TipoGesto::Borda;
                    novo.bordaX = !horizontal;
                    novo.bordaIndice = novoIndice;
                    e.borda = {true, !horizontal, novoIndice};
                    regerarSequenciaAtual(ctx);
                }
            } else if (ix >= 0 || iy >= 0) {
                abrirGesto(ctx);
                tornarGradeManual(ctx, *sequencia);
                novo.tipo = TipoGesto::Borda;
                novo.bordaX = ix >= 0;
                novo.bordaIndice = ix >= 0 ? seq::acharBorda(sequencia->gerador.bordasX, novo.inicioConteudo.x, tolerancia)
                                           : seq::acharBorda(sequencia->gerador.bordasY, novo.inicioConteudo.y, tolerancia);
                e.borda = {novo.bordaIndice >= 0, novo.bordaX, novo.bordaIndice};
            } else {
                e.borda = {};
            }
        } else {
            novo.alvo = seq::alvoDaEdicao(io.KeyAlt, e.visaoId);
            novo.modo = seq::modoDosModificadores(io.KeyShift, io.KeyCtrl);
            if (!sequencia && novo.alvo == seq::AlvoEdicao::Passo) {
                e.status = "Crie ou selecione uma sequência na lista";
            } else if (novo.alvo == seq::AlvoEdicao::Nenhum) {
                e.status = "Selecione uma Visão na lista para editá-la com Alt";
            } else {
                abrirGesto(ctx);
                novo.tipo = e.ferramenta == Ferramenta::Pincel ? TipoGesto::Pincel : TipoGesto::Retangulo;
                if (novo.tipo == TipoGesto::Pincel) {
                    novo.pincel.reiniciar();
                    aplicar(ctx, novo.pincel.tocar(lista, novo.inicioConteudo.x, novo.inicioConteudo.y), novo.modo, novo.alvo);
                }
            }
        }
        g_gesto = std::move(novo);
    }

    seq::RetF retanguloEdicao;
    bool desenharRetangulo = false;
    if (g_gesto.tipo != TipoGesto::Nada && (ativo || desativou)) {
        Gesto& gs = g_gesto;
        if (!gs.moveu && std::hypot(mouse.x - gs.inicioTela.x, mouse.y - gs.inicioTela.y) > dp(3.0f))
            gs.moveu = true;
        const seq::PontoF agora = doMouse();
        if (gs.tipo == TipoGesto::Pan) {
            e.vista.panX = gs.vistaInicial.panX + (mouse.x - gs.inicioTela.x);
            e.vista.panY = gs.vistaInicial.panY + (mouse.y - gs.inicioTela.y);
            e.ajustar = false;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (gs.tipo == TipoGesto::Pincel) {
            if (gs.moveu)
                aplicar(ctx,
                        gs.pincel.tocarSegmento(lista, gs.anterior.x, gs.anterior.y, agora.x, agora.y,
                                                 1.0f / std::max(e.vista.zoom, seq::kZoomMinimo)),
                        gs.modo, gs.alvo);
            gs.anterior = agora;
        } else if (gs.tipo == TipoGesto::Retangulo) {
            retanguloEdicao = seq::normalizarRet(gs.inicioConteudo.x, gs.inicioConteudo.y, agora.x, agora.y);
            desenharRetangulo = gs.moveu;
            if (desativou) {
                if (gs.moveu) {
                    aplicar(ctx, seq::slicesNoRetangulo(lista, retanguloEdicao), gs.modo, gs.alvo);
                } else {
                    const int i = seq::acharSliceEm(lista, agora.x, agora.y);
                    if (i >= 0)
                        aplicar(ctx, {lista[static_cast<size_t>(i)].uid}, gs.modo, gs.alvo);
                }
                desenharRetangulo = false;
            }
        } else if (gs.tipo == TipoGesto::Borda && sequencia) {
            seq::moverBorda(bordasDoEixo(*sequencia, gs.bordaX), gs.bordaIndice, gs.bordaX ? agora.x : agora.y);
            ImGui::SetMouseCursor(gs.bordaX ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
            regerarSequenciaAtual(ctx);
        }
        if (desativou)
            g_gesto = Gesto{};
    } else if (g_gesto.tipo != TipoGesto::Nada) {
        g_gesto = Gesto{};
    }

    int hover = -1;
    if (sobre && g_gesto.tipo == TipoGesto::Nada) {
        const seq::PontoF m = doMouse();
        hover = seq::acharSliceEm(lista, m.x, m.y);
        if (gradeAtiva) {
            if (seq::acharBorda(grade.bordasX, m.x, tolerancia) >= 0)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            else if (seq::acharBorda(grade.bordasY, m.y, tolerancia) >= 0)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            hover = -1;
        }
    }

    dl->PushClipRect(vpMin, vpMax, true);
    dl->AddRectFilled(vpMin, vpMax, cor(theme::kField));
    const ImVec2 ia = paraTela(0.0f, 0.0f);
    const ImVec2 ib = paraTela(cw, ch);
    GLuint textura = e.mostrarComposicao ? ctx.canvas.texture() : 0;
    if (textura != 0) {
        const float s = ctx.canvas.uvMax();
        const float hx = 0.5f / cw;
        const float hy = 0.5f / ch;
        dl->AddImage(static_cast<ImTextureID>(textura), ia, ib, ImVec2(hx, s - hy), ImVec2(s - hx, hy));
        dl->AddRectFilled(ia, ib, IM_COL32(0, 0, 0, 90));
    } else {
        dl->AddRectFilled(ia, ib, IM_COL32(6, 6, 6, 255));
    }
    dl->AddRect(ia, ib, cor(theme::kBorder));

    const seq::Passo* passoAtual = (sequencia && seq::passoValido(*sequencia, e.passo)) ? &sequencia->passos[static_cast<size_t>(e.passo)] : nullptr;
    const seq::Passo* passoAnterior =
        (sequencia && e.fantasma && e.passo > 0 && seq::passoValido(*sequencia, e.passo - 1)) ? &sequencia->passos[static_cast<size_t>(e.passo) - 1] : nullptr;
    const seq::Visao* visaoSel = visaoAtual(ctx);
    const float linha1 = std::max(1.0f, dp(1.0f));
    const float linha2 = std::max(1.5f, dp(2.0f));
    ImFont* mono = fonts().mono;
    const float corpo = mono->LegacySize * uiScale();
    const bool alvoVisao = io.KeyAlt && visaoSel != nullptr;

    int acesos = 0;
    for (size_t i = 0; i < lista.size(); ++i) {
        const seq::SliceCanvas& sc = lista[i];
        const ImVec2 a = paraTela(sc.r.x0, sc.r.y0);
        const ImVec2 b = paraTela(sc.r.x1, sc.r.y1);
        if (b.x < vpMin.x || a.x > vpMax.x || b.y < vpMin.y || a.y > vpMax.y)
            continue;
        const bool aceso = passoAtual && seq::passoContem(*passoAtual, sc.uid);
        if (aceso)
            ++acesos;
        const bool fantasma = passoAnterior && !aceso && seq::passoContem(*passoAnterior, sc.uid);
        const bool naVisao = visaoSel && std::binary_search(visaoSel->ligados.begin(), visaoSel->ligados.end(), sc.uid);
        if (aceso)
            dl->AddRectFilled(a, b, cor(theme::kAccent, 0.62f));
        else
            dl->AddRectFilled(a, b, cor(theme::kPanelRaised, e.mostrarComposicao ? 0.30f : 0.75f));
        if (alvoVisao && naVisao)
            dl->AddRectFilled(a, b, cor(theme::kInfo, 0.35f));
        dl->AddRect(a, b, cor(aceso ? theme::kAccentHover : theme::kBorder), 0.0f, 0, aceso ? linha2 : linha1);
        if (fantasma)
            drawDashedRect(dl, a, b, IM_COL32(255, 255, 255, 150), dp(5.0f), dp(4.0f));
        if (naVisao)
            dl->AddRect(ImVec2(a.x + linha2, a.y + linha2), ImVec2(b.x - linha2, b.y - linha2), cor(theme::kInfo), 0.0f, 0, linha2);
        int ordem = 0;
        for (const seq::Visao& v : conjunto.visoes) {
            if (v.previaVisivel && (!visaoSel || v.id != visaoSel->id)
                && std::binary_search(v.ligados.begin(), v.ligados.end(), sc.uid)) {
                const float r = dp(3.5f);
                dl->AddCircleFilled(ImVec2(a.x + dp(7.0f) + static_cast<float>(ordem) * dp(8.0f), b.y - dp(7.0f)), r,
                                    kPaletaVisoes[static_cast<size_t>(v.id) % (sizeof(kPaletaVisoes) / sizeof(kPaletaVisoes[0]))]);
                ++ordem;
            }
        }
        if (e.mostrarNomes && (b.x - a.x) > dp(46.0f) && (b.y - a.y) > corpo * 1.6f && infos[i].slice) {
            const std::string nome = infos[i].slice->name.empty() ? "#" + std::to_string(sc.uid) : infos[i].slice->name;
            const ImVec2 ts = mono->CalcTextSizeA(corpo, FLT_MAX, 0.0f, nome.c_str());
            const ImVec4 clip(a.x, a.y, b.x, b.y);
            dl->AddText(mono, corpo, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
                        cor(aceso ? theme::kOnAccent : theme::kText), nome.c_str(), nullptr, 0.0f, &clip);
        }
        if (static_cast<int>(i) == hover)
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 190), 0.0f, 0, linha2);
        if (qa::enabled())
            qa::noteRect("seq:slice:" + std::to_string(sc.uid), a.x, a.y, b.x, b.y);
    }

    if (gradeAtiva) {
        const ImVec4 corGrade = theme::kWarning;
        for (size_t i = 0; i < grade.bordasX.size(); ++i) {
            const ImVec2 p0 = paraTela(grade.bordasX[i], 0.0f);
            const ImVec2 p1 = paraTela(grade.bordasX[i], ch);
            const bool sel = e.borda.ativa && e.borda.eixoX && e.borda.indice == static_cast<int>(i);
            dl->AddLine(p0, p1, cor(corGrade, sel ? 1.0f : 0.75f), sel ? linha2 * 1.5f : linha2);
            if (qa::enabled())
                qa::noteRect("seq:borda:x:" + std::to_string(i), p0.x - 3.0f, p0.y, p0.x + 3.0f, p1.y);
        }
        for (size_t i = 0; i < grade.bordasY.size(); ++i) {
            const ImVec2 p0 = paraTela(0.0f, grade.bordasY[i]);
            const ImVec2 p1 = paraTela(cw, grade.bordasY[i]);
            const bool sel = e.borda.ativa && !e.borda.eixoX && e.borda.indice == static_cast<int>(i);
            dl->AddLine(p0, p1, cor(corGrade, sel ? 1.0f : 0.75f), sel ? linha2 * 1.5f : linha2);
            if (qa::enabled())
                qa::noteRect("seq:borda:y:" + std::to_string(i), p0.x, p0.y - 3.0f, p1.x, p0.y + 3.0f);
        }
    }

    if (desenharRetangulo) {
        const ImVec2 a = paraTela(retanguloEdicao.x0, retanguloEdicao.y0);
        const ImVec2 b = paraTela(retanguloEdicao.x1, retanguloEdicao.y1);
        const ImVec4& c = g_gesto.alvo == seq::AlvoEdicao::Visao ? theme::kInfo : theme::kAccent;
        dl->AddRectFilled(a, b, cor(c, 0.18f));
        dl->AddRect(a, b, cor(c), 0.0f, 0, linha1);
    }

    if (io.KeyAlt && sobre && !gradeAtiva) {
        const std::string aviso = visaoSel ? "Editando a Visão \"" + visaoSel->nome + "\"" : "Alt: selecione uma Visão na lista";
        const ImVec2 ts = ImGui::CalcTextSize(aviso.c_str());
        const ImVec2 p(vpMin.x + dp(10.0f), vpMin.y + dp(8.0f));
        dl->AddRectFilled(ImVec2(p.x - dp(6.0f), p.y - dp(3.0f)), ImVec2(p.x + ts.x + dp(6.0f), p.y + ts.y + dp(3.0f)),
                          cor(theme::kBackground, 0.85f), rounding());
        dl->AddText(p, cor(visaoSel ? theme::kInfo : theme::kWarning), aviso.c_str());
    } else if (!sequencia) {
        const char* aviso = "Crie uma sequência na lista para editar os passos";
        const ImVec2 ts = ImGui::CalcTextSize(aviso);
        const ImVec2 p(vpMin.x + dp(10.0f), vpMin.y + dp(8.0f));
        dl->AddRectFilled(ImVec2(p.x - dp(6.0f), p.y - dp(3.0f)), ImVec2(p.x + ts.x + dp(6.0f), p.y + ts.y + dp(3.0f)),
                          cor(theme::kBackground, 0.85f), rounding());
        dl->AddText(p, cor(theme::kWarning), aviso);
    }
    dl->PopClipRect();

    if (qa::enabled()) {
        qa::noteRect("seq:canvas", ia.x, ia.y, ib.x, ib.y);
        qa::noteRect("seq:vista", vpMin.x, vpMin.y, vpMax.x, vpMax.y);
    }

    if (hover >= 0 && hover < static_cast<int>(infos.size()) && infos[static_cast<size_t>(hover)].slice) {
        const SliceInfo& info = infos[static_cast<size_t>(hover)];
        ImGui::SetTooltip("%s\nTela: %s", info.slice->name.c_str(), info.tela ? info.tela->name.c_str() : "");
    }

    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%d slice(s)  |  %d aceso(s) no passo  |  %d%%%s%s", static_cast<int>(lista.size()), acesos,
                      static_cast<int>(std::lround(e.vista.zoom * 100.0f)), e.status.empty() ? "" : "  |  ",
                      e.status.c_str());
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%s", buf);
        popMono();
    }
    endPanel();
    ImGui::PopStyleVar();
}

}
