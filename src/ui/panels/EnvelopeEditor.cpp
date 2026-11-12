#include "ui/panels/EnvelopeEditor.h"

#include "app/Log.h"
#include "app/Qa.h"
#include "seq/EnvelopeStore.h"
#include "seq/SeqView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bsl::ui {

namespace {

enum class TipoGesto { Nada, Ancoras, Alca, Marquee };

struct Gesto
{
    TipoGesto tipo = TipoGesto::Nada;
    ImVec2 inicioTela{};
    seq::PontoF inicioGrafico;
    std::vector<int> indices;
    std::vector<seq::PontoF> inicioPos;
    int alcaIndice = -1;
    bool alcaSaida = false;
    bool moveu = false;
    bool somar = false;
    seq::Envelope antes;
};

struct Estado
{
    int envelopeId = 0;
    int pedidoId = -1;
    bool pedidoAbrir = false;
    std::vector<int> selecao;
    seq::HistoricoCurva historico;
    Gesto gesto;
    bool ima = true;
    bool campoAberto = false;
    seq::Envelope antesCampo;
    int menuTipo = 0;
    int menuIndice = -1;
    std::string status;
    char nome[128] = {};
    int nomeId = -1;
    int excluirId = 0;
};
Estado g;

constexpr int kDivisoesGrade = 20;

ImU32 cor(const ImVec4& c, float alfa = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alfa));
}

void dim(const char* texto)
{
    ImGui::TextColored(theme::kTextDim, "%s", texto);
}

bool selecionada(int indice)
{
    return std::find(g.selecao.begin(), g.selecao.end(), indice) != g.selecao.end();
}

void trocarEnvelope(int id)
{
    g.envelopeId = id;
    g.selecao.clear();
    g.historico.limpar();
    g.gesto = Gesto{};
    g.campoAberto = false;
    g.nomeId = -1;
}

void salvar(PanelContext& ctx)
{
    if (!seq::salvarBiblioteca(ctx.envelopes, seq::caminhoBiblioteca())) {
        g.status = "Não foi possível gravar a biblioteca de envelopes";
        log::warn(g.status);
    }
}

template <typename Funcao>
void editarEnvelope(PanelContext& ctx, seq::Envelope& env, Funcao&& funcao)
{
    const seq::Envelope antes = env;
    funcao();
    if (g.historico.confirmar(antes, env))
        salvar(ctx);
}

void fecharCampo(PanelContext& ctx, seq::Envelope& env)
{
    if (!g.campoAberto)
        return;
    g.campoAberto = false;
    if (g.historico.confirmar(g.antesCampo, env))
        salvar(ctx);
}

void desfazerCurva(PanelContext& ctx, seq::Envelope& env)
{
    fecharCampo(ctx, env);
    if (g.historico.desfazer(env)) {
        g.selecao.clear();
        salvar(ctx);
    }
}

void refazerCurva(PanelContext& ctx, seq::Envelope& env)
{
    fecharCampo(ctx, env);
    if (g.historico.refazer(env)) {
        g.selecao.clear();
        salvar(ctx);
    }
}

void removerSelecionadas(PanelContext& ctx, seq::Envelope& env)
{
    std::vector<int> indices = g.selecao;
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    editarEnvelope(ctx, env, [&] {
        for (int i : indices)
            seq::removerAncora(env, i);
    });
    g.selecao.clear();
}

void iniciarGesto(PanelContext& ctx, seq::Envelope& env, const seq::AreaGrafico& area, ImVec2 mouse, bool duploClique)
{
    const ImGuiIO& io = ImGui::GetIO();
    const seq::PontoF gp = seq::telaParaGrafico(area, mouse.x, mouse.y);
    const seq::AlvoCurva alvo = seq::acharAlvoCurva(env, area, mouse.x, mouse.y, dp(8.0f), g.selecao);

    if (duploClique) {
        if (alvo.tipo == seq::TipoAlvo::Ancora) {
            editarEnvelope(ctx, env, [&] { seq::removerAncora(env, alvo.indice); });
            g.selecao.clear();
        } else if (alvo.tipo == seq::TipoAlvo::Segmento) {
            int criada = -1;
            editarEnvelope(ctx, env, [&] { criada = seq::inserirAncora(env, gp.x); });
            if (criada >= 0)
                g.selecao = {criada};
        }
        return;
    }

    Gesto novo;
    novo.inicioTela = mouse;
    novo.inicioGrafico = gp;
    novo.antes = env;
    if (alvo.tipo == seq::TipoAlvo::Ancora) {
        if (io.KeyShift || io.KeyCtrl) {
            if (selecionada(alvo.indice))
                g.selecao.erase(std::remove(g.selecao.begin(), g.selecao.end(), alvo.indice), g.selecao.end());
            else
                g.selecao.push_back(alvo.indice);
        } else if (!selecionada(alvo.indice)) {
            g.selecao = {alvo.indice};
        }
        novo.tipo = TipoGesto::Ancoras;
        novo.indices = g.selecao;
        for (int i : novo.indices)
            novo.inicioPos.push_back({env.ancoras[static_cast<size_t>(i)].x, env.ancoras[static_cast<size_t>(i)].y});
    } else if (alvo.tipo == seq::TipoAlvo::AlcaEntrada || alvo.tipo == seq::TipoAlvo::AlcaSaida) {
        novo.tipo = TipoGesto::Alca;
        novo.alcaIndice = alvo.indice;
        novo.alcaSaida = alvo.tipo == seq::TipoAlvo::AlcaSaida;
        if (io.KeyAlt)
            seq::definirAlcasLigadas(env, alvo.indice, false);
    } else {
        novo.tipo = TipoGesto::Marquee;
        novo.somar = io.KeyShift || io.KeyCtrl;
        if (!novo.somar)
            g.selecao.clear();
    }
    g.gesto = std::move(novo);
}

void atualizarGesto(PanelContext& ctx, seq::Envelope& env, const seq::AreaGrafico& area, ImVec2 mouse, bool terminou,
                    seq::RetF& marquee, bool& mostrarMarquee)
{
    const ImGuiIO& io = ImGui::GetIO();
    Gesto& gs = g.gesto;
    if (!gs.moveu && std::hypot(mouse.x - gs.inicioTela.x, mouse.y - gs.inicioTela.y) > dp(3.0f))
        gs.moveu = true;
    const seq::PontoF gp = seq::telaParaGrafico(area, mouse.x, mouse.y);
    const float larguraArea = std::max(area.x1 - area.x0, 1.0f);

    if (gs.tipo == TipoGesto::Ancoras && gs.moveu && !gs.indices.empty()) {
        float dx = gp.x - gs.inicioGrafico.x;
        float dy = gp.y - gs.inicioGrafico.y;
        if (io.KeyShift)
            seq::travarEixo(0.0f, 0.0f, dx, dy);
        if (g.ima && !io.KeyCtrl) {
            const float limiar = dp(6.0f) / larguraArea;
            const seq::PontoF base = gs.inicioPos.front();
            dx = seq::imaNaGrade(base.x + dx, kDivisoesGrade, limiar) - base.x;
            dy = seq::imaNaGrade(base.y + dy, kDivisoesGrade, limiar) - base.y;
        }
        env = gs.antes;
        seq::moverAncorasPor(env, gs.indices, gs.inicioPos, dx, dy);
    } else if (gs.tipo == TipoGesto::Alca && gs.moveu) {
        const seq::Ancora& a = env.ancoras[static_cast<size_t>(gs.alcaIndice)];
        float dx = gp.x - a.x;
        float dy = gp.y - a.y;
        if (io.KeyShift)
            seq::travarEixo(0.0f, 0.0f, dx, dy);
        seq::definirAlca(env, gs.alcaIndice, gs.alcaSaida, dx, dy);
    } else if (gs.tipo == TipoGesto::Marquee) {
        marquee = seq::normalizarRet(gs.inicioGrafico.x, gs.inicioGrafico.y, gp.x, gp.y);
        mostrarMarquee = gs.moveu && !terminou;
        if (terminou && gs.moveu) {
            std::vector<int> dentro = seq::ancorasNoRetangulo(env, marquee);
            if (gs.somar) {
                for (int i : dentro)
                    if (!selecionada(i))
                        g.selecao.push_back(i);
            } else {
                g.selecao = std::move(dentro);
            }
        }
    }

    if (terminou) {
        if ((gs.tipo == TipoGesto::Ancoras || gs.tipo == TipoGesto::Alca) && g.historico.confirmar(gs.antes, env))
            salvar(ctx);
        g.gesto = Gesto{};
    }
}

}

void openEnvelopeEditor(int envelopeId)
{
    g.pedidoAbrir = true;
    g.pedidoId = envelopeId;
}

bool envelopeEditorTakeOpenRequest()
{
    const bool pedido = g.pedidoAbrir;
    g.pedidoAbrir = false;
    return pedido;
}

void envelopeEditorReset()
{
    g.selecao.clear();
    g.historico.limpar();
    g.gesto = Gesto{};
    g.campoAberto = false;
    g.nomeId = -1;
    g.status.clear();
}

EnvelopeUiInfo envelopeUiInfo(const seq::BibliotecaEnvelopes& biblioteca)
{
    EnvelopeUiInfo i;
    i.envelopeId = g.envelopeId;
    if (const seq::Envelope* e = seq::acharEnvelope(biblioteca, g.envelopeId))
        i.anchors = static_cast<int>(e->ancoras.size());
    i.selected = static_cast<int>(g.selecao.size());
    i.canUndo = g.historico.podeDesfazer();
    i.canRedo = g.historico.podeRefazer();
    i.dragging = g.gesto.tipo != TipoGesto::Nada;
    i.snap = g.ima;
    i.status = g.status;
    return i;
}

void drawEnvelopePanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kSeqEnv);

    seq::BibliotecaEnvelopes& lib = ctx.envelopes;
    if (lib.lista.empty())
        seq::garantirBiblioteca(lib);
    if (g.pedidoId >= 0) {
        const int pedido = g.pedidoId;
        g.pedidoId = -1;
        if (pedido > 0 && seq::acharEnvelope(lib, pedido) && pedido != g.envelopeId)
            trocarEnvelope(pedido);
    }
    seq::Envelope* env = seq::acharEnvelope(lib, g.envelopeId);
    if (!env) {
        trocarEnvelope(lib.lista.front().id);
        env = seq::acharEnvelope(lib, g.envelopeId);
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (g.campoAberto && !ImGui::IsAnyItemActive())
        fecharCampo(ctx, *env);

    const float lado = ImGui::GetFrameHeight();
    const ImVec2 quadrado(lado, lado);
    if (iconButton(ICON_PLUS, "##env_novo", "Novo envelope (curva S)", quadrado)) {
        const int id = seq::criarEnvelope(lib, "Envelope " + std::to_string(lib.lista.size() + 1));
        if (id > 0) {
            trocarEnvelope(id);
            salvar(ctx);
        }
    }
    ImGui::SameLine();
    if (iconButton(ICON_COPY, "##env_duplicar", "Duplicar o envelope", quadrado)) {
        const int id = seq::duplicarEnvelope(lib, env->id);
        if (id > 0) {
            trocarEnvelope(id);
            salvar(ctx);
        }
    }
    ImGui::SameLine();
    if (iconButton(ICON_TRASH, "##env_excluir", "Excluir o envelope", quadrado)) {
        const std::vector<int> usados = ctx.composition.envelopesUsados();
        if (std::find(usados.begin(), usados.end(), env->id) != usados.end()) {
            g.excluirId = env->id;
            ImGui::OpenPopup("Excluir envelope###env_excluir_modal");
        } else {
            seq::excluirEnvelope(lib, env->id);
            trocarEnvelope(lib.lista.front().id);
            salvar(ctx);
        }
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + dp(10.0f));
    ImGui::BeginDisabled(!g.historico.podeDesfazer());
    if (iconButton(ICON_UNDO, "##env_desfazer", "Desfazer (Ctrl+Z)", quadrado))
        desfazerCurva(ctx, *env);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g.historico.podeRefazer());
    if (iconButton(ICON_REDO, "##env_refazer", "Refazer (Ctrl+Y)", quadrado))
        refazerCurva(ctx, *env);
    ImGui::EndDisabled();
    ImGui::SameLine();
    iconToggle(ICON_MAGNET, "##env_ima", g.ima, "Ímã: encaixa na grade de 5% (Ctrl desliga enquanto segura)", quadrado);

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Excluir envelope###env_excluir_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Este envelope é usado por efeitos do projeto.");
        ImGui::TextUnformatted("Os efeitos passam a avisar que o envelope não existe.");
        if (ImGui::Button("Excluir##env_excluir_ok", ImVec2(dp(110.0f), 0.0f))) {
            seq::excluirEnvelope(lib, g.excluirId);
            trocarEnvelope(lib.lista.front().id);
            salvar(ctx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar##env_excluir_cancelar", ImVec2(dp(110.0f), 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    env = seq::acharEnvelope(lib, g.envelopeId);
    if (!env) {
        endPanel();
        ImGui::PopStyleVar();
        return;
    }

    ImGui::BeginChild("##env_lista", ImVec2(0.0f, dp(92.0f)), true);
    for (const seq::Envelope& item : lib.lista) {
        ImGui::PushID(item.id);
        const std::string rotulo = (item.nome.empty() ? std::string("(sem nome)") : item.nome) + "##env_item";
        if (ImGui::Selectable(rotulo.c_str(), item.id == g.envelopeId) && item.id != g.envelopeId) {
            ImGui::PopID();
            trocarEnvelope(item.id);
            ImGui::EndChild();
            endPanel();
            ImGui::PopStyleVar();
            return;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (g.nomeId != env->id) {
        std::snprintf(g.nome, sizeof(g.nome), "%s", env->nome.c_str());
        g.nomeId = env->id;
    }
    dim("Nome");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##env_nome", g.nome, sizeof(g.nome)))
        seq::renomearEnvelope(lib, env->id, g.nome);
    if (ImGui::IsItemDeactivatedAfterEdit())
        salvar(ctx);

    const ImVec2 disponivel = ImGui::GetContentRegionAvail();
    const float altura = std::max(dp(150.0f), std::min(disponivel.x * 0.8f, disponivel.y - dp(96.0f)));
    ImGui::InvisibleButton("##env_grafico", ImVec2(std::max(disponivel.x, dp(80.0f)), altura), ImGuiButtonFlags_MouseButtonLeft);
    for (ImGuiKey k : {ImGuiKey_Delete, ImGuiKey_Backspace, ImGuiKey_Escape})
        ImGui::SetItemKeyOwner(k);
    const bool sobre = ImGui::IsItemHovered();
    const bool ativou = ImGui::IsItemActivated();
    const bool ativo = ImGui::IsItemActive();
    const bool desativou = ImGui::IsItemDeactivated();
    const ImVec2 vpMin = ImGui::GetItemRectMin();
    const ImVec2 vpMax = ImGui::GetItemRectMax();
    const bool focado = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const float margem = dp(16.0f);
    const seq::AreaGrafico area{vpMin.x + margem, vpMin.y + margem, vpMax.x - margem, vpMax.y - margem};
    const ImVec2 mouse = io.MousePos;

    if (ativou) {
        fecharCampo(ctx, *env);
        iniciarGesto(ctx, *env, area, mouse, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
        env = seq::acharEnvelope(lib, g.envelopeId);
    }
    seq::RetF marquee;
    bool mostrarMarquee = false;
    if (g.gesto.tipo != TipoGesto::Nada && (ativo || desativou))
        atualizarGesto(ctx, *env, area, mouse, desativou, marquee, mostrarMarquee);
    else if (g.gesto.tipo != TipoGesto::Nada)
        g.gesto = Gesto{};

    if (sobre && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const seq::AlvoCurva alvo = seq::acharAlvoCurva(*env, area, mouse.x, mouse.y, dp(8.0f), g.selecao);
        if (alvo.tipo == seq::TipoAlvo::Segmento || alvo.tipo == seq::TipoAlvo::Ancora) {
            g.menuTipo = alvo.tipo == seq::TipoAlvo::Segmento ? 1 : 2;
            g.menuIndice = alvo.indice;
            ImGui::OpenPopup("##env_menu");
        }
    }
    if (ImGui::BeginPopup("##env_menu")) {
        const int n = static_cast<int>(env->ancoras.size());
        if (g.menuTipo == 1 && g.menuIndice >= 0 && g.menuIndice + 1 < n) {
            const bool reto = env->ancoras[static_cast<size_t>(g.menuIndice)].reto;
            if (ImGui::MenuItem(reto ? "Tornar curvo##env_ctx_reto" : "Tornar reto##env_ctx_reto"))
                editarEnvelope(ctx, *env, [&] { seq::definirReto(*env, g.menuIndice, !reto); });
        } else if (g.menuTipo == 2 && g.menuIndice >= 0 && g.menuIndice < n) {
            const bool ligadas = env->ancoras[static_cast<size_t>(g.menuIndice)].alcasLigadas;
            if (ImGui::MenuItem(ligadas ? "Separar alças##env_ctx_alcas" : "Ligar alças##env_ctx_alcas"))
                editarEnvelope(ctx, *env, [&] { seq::definirAlcasLigadas(*env, g.menuIndice, !ligadas); });
            const bool podeRemover = g.menuIndice > 0 && g.menuIndice + 1 < n;
            if (ImGui::MenuItem("Remover âncora##env_ctx_remover", nullptr, false, podeRemover)) {
                editarEnvelope(ctx, *env, [&] { seq::removerAncora(*env, g.menuIndice); });
                g.selecao.clear();
            }
        }
        ImGui::EndPopup();
    }

    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (focado && !io.WantTextInput && !popup && g.gesto.tipo == TipoGesto::Nada) {
        if (io.KeyCtrl && !io.KeyAlt) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, true) && !io.KeyShift)
                desfazerCurva(ctx, *env);
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, true) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, true)))
                refazerCurva(ctx, *env);
            else if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
                g.selecao.clear();
                for (int i = 0; i < static_cast<int>(env->ancoras.size()); ++i)
                    g.selecao.push_back(i);
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            removerSelecionadas(ctx, *env);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g.selecao.clear();
        }
    }
    {
        const int n = static_cast<int>(env->ancoras.size());
        g.selecao.erase(std::remove_if(g.selecao.begin(), g.selecao.end(), [&](int i) { return i < 0 || i >= n; }), g.selecao.end());
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(vpMin, vpMax, true);
    dl->AddRectFilled(vpMin, vpMax, cor(theme::kField), rounding());
    const float linha1 = std::max(1.0f, dp(1.0f));
    const float linha2 = std::max(1.5f, dp(2.0f));
    for (int i = 0; i <= kDivisoesGrade; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(kDivisoesGrade);
        const bool forte = i % 5 == 0;
        const ImU32 c = cor(theme::kTextStrong, forte ? 0.14f : 0.05f);
        const seq::PontoF a = seq::graficoParaTela(area, v, 0.0f);
        const seq::PontoF b = seq::graficoParaTela(area, v, 1.0f);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), c);
        const seq::PontoF c0 = seq::graficoParaTela(area, 0.0f, v);
        const seq::PontoF c1 = seq::graficoParaTela(area, 1.0f, v);
        dl->AddLine(ImVec2(c0.x, c0.y), ImVec2(c1.x, c1.y), c);
    }
    dl->AddRect(ImVec2(area.x0, area.y0), ImVec2(area.x1, area.y1), cor(theme::kBorder));

    std::vector<ImVec2> pontos;
    pontos.reserve(seq::kTamanhoTabela);
    for (int i = 0; i < seq::kTamanhoTabela; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(seq::kTamanhoTabela - 1);
        const float y = env->tabela.size() == static_cast<size_t>(seq::kTamanhoTabela) ? env->tabela[static_cast<size_t>(i)]
                                                                                       : env->avaliar(x);
        const seq::PontoF p = seq::graficoParaTela(area, x, y);
        pontos.push_back(ImVec2(p.x, p.y));
    }
    dl->AddPolyline(pontos.data(), static_cast<int>(pontos.size()), cor(theme::kAccent), 0, linha2);

    const int total = static_cast<int>(env->ancoras.size());
    for (int i : g.selecao) {
        if (i < 0 || i >= total)
            continue;
        const seq::Ancora& a = env->ancoras[static_cast<size_t>(i)];
        const seq::PontoF centro = seq::graficoParaTela(area, a.x, a.y);
        auto alca = [&](float ox, float oy) {
            const seq::PontoF h = seq::graficoParaTela(area, a.x + ox, a.y + oy);
            dl->AddLine(ImVec2(centro.x, centro.y), ImVec2(h.x, h.y), cor(theme::kTextDim), linha1);
            dl->AddCircleFilled(ImVec2(h.x, h.y), dp(4.0f), cor(theme::kInfo));
            dl->AddCircle(ImVec2(h.x, h.y), dp(4.0f), cor(theme::kTextStrong), 0, linha1);
        };
        if (i + 1 < total && !a.reto)
            alca(a.saidaX, a.saidaY);
        if (i > 0 && !env->ancoras[static_cast<size_t>(i) - 1].reto)
            alca(a.entradaX, a.entradaY);
    }
    const float meio = dp(5.0f);
    for (int i = 0; i < total; ++i) {
        const seq::Ancora& a = env->ancoras[static_cast<size_t>(i)];
        const seq::PontoF c = seq::graficoParaTela(area, a.x, a.y);
        const bool sel = selecionada(i);
        dl->AddRectFilled(ImVec2(c.x - meio, c.y - meio), ImVec2(c.x + meio, c.y + meio),
                          sel ? cor(theme::kAccent) : cor(theme::kTextStrong));
        dl->AddRect(ImVec2(c.x - meio, c.y - meio), ImVec2(c.x + meio, c.y + meio), sel ? cor(theme::kTextStrong) : cor(theme::kBackground),
                    0.0f, 0, linha1);
        if (qa::enabled())
            qa::noteRect("env:ancora:" + std::to_string(i), c.x - meio, c.y - meio, c.x + meio, c.y + meio);
    }
    if (mostrarMarquee) {
        const seq::PontoF a = seq::graficoParaTela(area, marquee.x0, marquee.y1);
        const seq::PontoF b = seq::graficoParaTela(area, marquee.x1, marquee.y0);
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y), cor(theme::kAccent, 0.16f));
        dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), cor(theme::kAccent), 0.0f, 0, linha1);
    }
    dl->PopClipRect();
    if (qa::enabled()) {
        qa::noteRect("env:grafico", area.x0, area.y0, area.x1, area.y1);
        qa::noteRect("env:vista", vpMin.x, vpMin.y, vpMax.x, vpMax.y);
    }

    if (g.selecao.size() == 1) {
        const int idx = g.selecao.front();
        const seq::Ancora a = env->ancoras[static_cast<size_t>(idx)];
        float x = a.x;
        float y = a.y;
        const float metade = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f / uiScale();
        const bool fimX = idx == 0 || idx + 1 == static_cast<int>(env->ancoras.size());
        ImGui::BeginDisabled(fimX);
        bool mudou = hotNumber("##env_x", x, 0.0f, 1.0f, 0.002f, "%.3f", "x", NAN, metade);
        ImGui::EndDisabled();
        ImGui::SameLine();
        mudou = hotNumber("##env_y", y, 0.0f, 1.0f, 0.002f, "%.3f", "y", NAN, -1.0f) || mudou;
        if (mudou) {
            if (!g.campoAberto) {
                g.antesCampo = *env;
                g.campoAberto = true;
            }
            seq::moverAncora(*env, idx, seq::limitarXAncora(*env, idx, x), y);
        }
        bool ligadas = a.alcasLigadas;
        if (ImGui::Checkbox("Alças ligadas##env_alcas_ligadas", &ligadas))
            editarEnvelope(ctx, *env, [&] { seq::definirAlcasLigadas(*env, idx, ligadas); });
        if (idx + 1 < static_cast<int>(env->ancoras.size())) {
            ImGui::SameLine();
            bool reto = a.reto;
            if (ImGui::Checkbox("Segmento reto##env_reto", &reto))
                editarEnvelope(ctx, *env, [&] { seq::definirReto(*env, idx, reto); });
        }
    } else if (g.selecao.size() > 1) {
        dim("Várias âncoras selecionadas: arraste para mover juntas.");
    } else {
        dim("Duplo clique na linha cria uma âncora; na âncora, remove.");
    }
    if (!g.status.empty())
        ImGui::TextColored(theme::kWarning, "%s", g.status.c_str());

    endPanel();
    ImGui::PopStyleVar();
}

}
