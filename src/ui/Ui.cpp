#include "ui/Ui.h"

#include "app/ProjectSession.h"
#include "app/Paths.h"
#include "output/OutputRenderer.h"
#include "ui/Brand.h"
#include "ui/LayoutUi.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/AiWindow.h"
#include "ui/panels/AudioWindow.h"
#include "ui/panels/BrowserPanel.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/ClipPanel.h"
#include "ui/panels/CompositionPanel.h"
#include "ui/panels/MapView.h"
#include "ui/panels/MappingPanels.h"
#include "ui/panels/PixelMapPanels.h"
#include "ui/panels/PluginsWindow.h"
#include "ui/panels/PreviewPanel.h"
#include "ui/panels/StatusBar.h"
#include "ui/panels/TabPanels.h"
#include "ui/panels/VideoCompat.h"

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

#ifndef BSL_VERSION
#define BSL_VERSION "0.1.0"
#endif

// IDs dos dockspaces. O da Composição mudou ("BSLC") quando o layout foi refeito no estilo media
// server: um imgui.ini antigo, com o dockspace "BSLS", é ignorado e o layout novo é construído.
constexpr ImGuiID kDockSpaceId = 0x42534C43;        // "BSLC": layout do modo Composição
constexpr ImGuiID kMappingDockSpaceId = 0x42534C4D; // "BSLM": layout do modo Mapeamento (salvo à parte)
constexpr ImGuiID kPixelMapDockSpaceId = 0x42534C50; // "BSLP": layout do modo Pixel Map (salvo à parte)

bool g_resetCompLayout = false; // refaz o layout da Composicao (inicio, Restaurar layout, troca de layout)
bool g_resetMapLayout = false;  // idem para o Mapeamento
bool g_resetPixelLayout = false; // idem para o Pixel Map
bool g_openAbout = false;
int g_focusFilesFrames = 0; // depois de montar o layout, seleciona a aba Arquivos (por alguns frames)
const char* g_focusTab = nullptr; // aba pedida por --tab (depois da Arquivos)
int g_focusTabFrames = 0;

// Layouts prontos da Composição. Todas as proporções são frações da janela (nada em pixels), então valem
// em qualquer tamanho e escala.
//  +-----------+---------------------------+
//  | Saída     | Composição (grade + deck  |
//  |           |  + transporte)            |
//  +-----------+-------------+-------------+
//  | Prévia    | Clip        | Abas        |
//  +-----------+-------------+-------------+
struct LayoutPlan
{
    float leftWidth;     // fração da largura para a coluna dos monitores (Saída + Prévia)
    float previewShare;  // fração da altura da coluna dos monitores para a Prévia (embaixo)
    float bottomHeight;  // fração da altura da área à direita para Clip + abas (embaixo)
    float tabsShare;     // fração da largura de Clip + abas para as abas
};

LayoutPlan planFor(UiLayout layout)
{
    switch (layout) {
    case UiLayout::BigGrid: return {0.20f, 0.45f, 0.30f, 0.50f};   // monitores pequenos, grade grande
    case UiLayout::BigOutput: return {0.46f, 0.30f, 0.34f, 0.60f}; // Saída grande para operar olhando a saída
    default: return {0.30f, 0.50f, 0.44f, 0.50f};                  // media server
    }
}

void buildDefaultLayout(ImGuiID root, ImVec2 size, UiLayout layout)
{
    const LayoutPlan plan = planFor(layout);
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, size);

    ImGuiID rightArea = root;
    ImGuiID left = 0;
    ImGuiID leftBottom = 0;
    ImGuiID bottomArea = 0;
    ImGuiID tabs = 0;

    left = ImGui::DockBuilderSplitNode(rightArea, ImGuiDir_Left, plan.leftWidth, nullptr, &rightArea);
    leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, plan.previewShare, nullptr, &left);
    bottomArea = ImGui::DockBuilderSplitNode(rightArea, ImGuiDir_Down, plan.bottomHeight, nullptr, &rightArea);
    tabs = ImGui::DockBuilderSplitNode(bottomArea, ImGuiDir_Right, plan.tabsShare, nullptr, &bottomArea);

    ImGui::DockBuilderDockWindow(panel::kOutput, left);
    ImGui::DockBuilderDockWindow(panel::kPreview, leftBottom);
    ImGui::DockBuilderDockWindow(panel::kComposition, rightArea);
    ImGui::DockBuilderDockWindow(panel::kClip, bottomArea);
    ImGui::DockBuilderDockWindow(panel::kFiles, tabs);
    ImGui::DockBuilderDockWindow(panel::kGenerators, tabs);
    ImGui::DockBuilderDockWindow(panel::kEffects, tabs);
    ImGui::DockBuilderDockWindow(panel::kCompositionTab, tabs);
    ImGui::DockBuilderDockWindow(panel::kLayerTab, tabs);
    ImGui::DockBuilderDockWindow(panel::kOutputTab, tabs);
    ImGui::DockBuilderFinish(root);
    g_focusFilesFrames = 4;
}

// Layout inicial do Mapeamento:
//  | Lista |      Entrada       | Propriedades |
//  |       |      Saída         |              |
void buildMappingLayout(ImGuiID root, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, size);

    ImGuiID center = root;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.50f, nullptr, &center);

    ImGui::DockBuilderDockWindow(panel::kMapList, left);
    ImGui::DockBuilderDockWindow(panel::kMapInput, center);
    ImGui::DockBuilderDockWindow(panel::kMapOutput, bottom);
    ImGui::DockBuilderDockWindow(panel::kMapProps, right);
    ImGui::DockBuilderFinish(root);
}

// Layout inicial do Pixel Map:
//  | Configuração |        Overview         |
//  |              |        Tabela           |
void buildPixelMapLayout(ImGuiID root, ImVec2 size)
{
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, size);

    ImGuiID center = root;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.38f, nullptr, &center);

    ImGui::DockBuilderDockWindow(panel::kPmConfig, left);
    ImGui::DockBuilderDockWindow(panel::kPmOverview, center);
    ImGui::DockBuilderDockWindow(panel::kPmTable, bottom);
    ImGui::DockBuilderFinish(root);
}

// Botão de alternância (aceso em vermelho quando ligado)
bool toggleChip(const char* label, bool on)
{
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    }
    const bool pressed = ImGui::Button(label);
    if (on)
        ImGui::PopStyleColor(4);
    return pressed;
}

// Arquivo > Abrir recente: últimos projetos (os que não existem mais somem da lista)
void drawRecentMenu(PanelContext& ctx)
{
    Settings& settings = ctx.settings;
    if (!ImGui::BeginMenu(ICON_FOLDER "  Abrir recente##menu_recentes", !settings.recentProjects.empty()))
        return;

    std::vector<std::string> gone;
    for (const std::string& utf8 : settings.recentProjects) {
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(std::u8string(utf8.begin(), utf8.end())), ec))
            gone.push_back(utf8);
    }
    if (!gone.empty()) {
        for (const std::string& g : gone)
            settings.removeRecentProject(g);
        settings.save();
    }

    std::string chosen;
    int index = 0;
    for (const std::string& utf8 : settings.recentProjects) {
        const std::filesystem::path file(std::u8string(utf8.begin(), utf8.end()));
        const std::string label = paths::toUtf8(file.filename()) + "##recente" + std::to_string(index++);
        if (ImGui::MenuItem(label.c_str()))
            chosen = utf8;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", utf8.c_str());
    }
    ImGui::EndMenu();
    if (!chosen.empty())
        ctx.project.requestOpenPath(std::filesystem::path(std::u8string(chosen.begin(), chosen.end())));
}

// Atalhos de projeto (ignorados enquanto um modal está aberto)
void handleProjectShortcuts(PanelContext& ctx)
{
    if (ctx.project.busy())
        return;
    constexpr ImGuiInputFlags flags = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, flags))
        ctx.project.requestSaveAs();
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, flags))
        ctx.project.requestSave();
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, flags))
        ctx.project.requestOpen();
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, flags))
        ctx.project.requestNew();
}

// Barra de menu: monograma BSL a esquerda e menus Arquivo / Composição / Saída / Ver / Ajuda
void drawMenuBar(PanelContext& ctx)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    Composition& comp = ctx.composition;

    const Texture& mark = brand().monogram;
    if (mark) {
        const float h = dp(22.0f);
        const float w = h * static_cast<float>(mark.width) / static_cast<float>(mark.height);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float barH = ImGui::GetWindowHeight();
        const float top = p.y + (barH - h) * 0.5f - (p.y - ImGui::GetWindowPos().y);
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(mark.id), ImVec2(p.x + dp(2.0f), top),
                                             ImVec2(p.x + dp(2.0f) + w, top + h));
        ImGui::Dummy(ImVec2(w + dp(14.0f), 1.0f));
    }

    if (ImGui::BeginMenu("Arquivo")) {
        ProjectSession& project = ctx.project;
        if (ImGui::MenuItem(ICON_FILE_PLUS "  Novo##menu_novo", "Ctrl+N"))
            project.requestNew();
        if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Abrir…##menu_abrir", "Ctrl+O"))
            project.requestOpen();
        drawRecentMenu(ctx);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SAVE "  Salvar##menu_salvar", "Ctrl+S"))
            project.requestSave();
        if (ImGui::MenuItem(ICON_SAVE_ALL "  Salvar como…##menu_salvar_como", "Ctrl+Shift+S"))
            project.requestSaveAs();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_X "  Sair##menu_sair", "Alt+F4"))
            ctx.quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Composição")) {
        if (ImGui::MenuItem("+ Camada"))
            comp.addLayer();
        if (ImGui::MenuItem("+ Coluna"))
            comp.addColumn();
        if (ImGui::MenuItem("+ Deck")) {
            comp.currentDeck = comp.addDeck();
            if (ctx.selection.kind == Selection::Kind::Clip)
                ctx.selection = {};
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Limpar tudo"))
            comp.stopAll();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Saída")) {
        if (ImGui::MenuItem("Identificar", nullptr, ctx.outputRenderer.identify()))
            ctx.outputRenderer.setIdentify(!ctx.outputRenderer.identify());
        if (ImGui::MenuItem("Abrir Mapeamento"))
            ctx.mode = AppMode::Mapping;
        if (ImGui::MenuItem("Abrir Pixel Map"))
            ctx.mode = AppMode::PixelMap;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ver")) {
        if (ImGui::BeginMenu("Tamanho da interface")) {
            Settings& st = ctx.settings;
            if (ImGui::MenuItem("Acompanhar a janela (recomendado)", nullptr, st.uiScaleMode == UiScaleMode::Window))
                st.uiScaleMode = UiScaleMode::Window;
            if (ImGui::MenuItem("Automático (escala do Windows)", nullptr, st.uiScaleMode == UiScaleMode::System))
                st.uiScaleMode = UiScaleMode::System;
            ImGui::Separator();
            constexpr float kSteps[] = {0.9f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
            for (float step : kSteps) {
                char label[16];
                std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(step * 100.0f + 0.5f));
                const bool on = st.uiScaleMode == UiScaleMode::Fixed && std::abs(st.uiScale - step) < 0.01f;
                if (ImGui::MenuItem(label, nullptr, on)) {
                    st.uiScaleMode = UiScaleMode::Fixed;
                    st.uiScale = step;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Layout")) {
            for (int i = 0; i < kUiLayoutCount; ++i) {
                const auto layout = static_cast<UiLayout>(i);
                if (ImGui::MenuItem(uiLayoutLabel(layout), nullptr,
                                    ctx.settings.layout == layout && ctx.settings.lastLayoutPreset.empty())) {
                    ctx.settings.layout = layout;
                    clearActiveLayoutPreset(ctx);
                    g_resetCompLayout = true; // vale ja na Composicao (ou ao voltar a ela, vindo do Mapeamento)
                }
            }
            drawLayoutPresetsMenu(ctx); // Salvar layout atual…, os do usuário (Alt+1..9) e Gerenciar…
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_AUDIO_LINES "  Áudio…##menu_audio"))
            openAudioWindow();
        if (ImGui::MenuItem(ICON_SPARKLES "  IA…##menu_ia"))
            openAiWindow();
        if (ImGui::MenuItem(ICON_PLUG "  Plugins…##menu_plugins"))
            openPluginsWindow();
        if (ImGui::MenuItem(ICON_GLOBE "  Compatibilidade de vídeo…##menu_compat"))
            openCompatWindow();
        ImGui::Separator();
        if (ImGui::MenuItem("Restaurar layout")) {
            if (ctx.mode == AppMode::Mapping) {
                g_resetMapLayout = true;
            } else if (ctx.mode == AppMode::PixelMap) {
                g_resetPixelLayout = true;
            } else {
                g_resetCompLayout = true;
                clearActiveLayoutPreset(ctx); // volta ao layout pronto
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ajuda")) {
        if (ImGui::MenuItem("Sobre"))
            g_openAbout = true;
        ImGui::EndMenu();
    }

    // Alternância de layout: Composição | Mapeamento | Pixel Map
    ImGui::Dummy(ImVec2(dp(18.0f), 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(12.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(4.0f, 0.0f));
    // "###modo_*": o menu "Composição" usa o mesmo rotulo nesta barra (conflito de ID no ImGui)
    if (toggleChip("Composição###modo_composicao", ctx.mode == AppMode::Composition))
        ctx.mode = AppMode::Composition;
    if (toggleChip("Mapeamento###modo_mapeamento", ctx.mode == AppMode::Mapping))
        ctx.mode = AppMode::Mapping;
    if (toggleChip("Pixel Map###modo_pixelmap", ctx.mode == AppMode::PixelMap))
        ctx.mode = AppMode::PixelMap;
    if (ctx.mode == AppMode::Mapping) {
        ImGui::Dummy(ImVec2(dp(14.0f), 1.0f));
        if (toggleChip(ICON_SCAN "  Identificar##map_identificar", ctx.outputRenderer.identify()))
            ctx.outputRenderer.setIdentify(!ctx.outputRenderer.identify());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Desenha borda, nome e grade de teste de cada slice nas saídas reais");
    }
    ImGui::PopStyleVar(2);
    ImGui::EndMainMenuBar();
}

void drawAboutModal()
{
    if (g_openAbout) {
        ImGui::OpenPopup("Sobre o BSL Stage");
        g_openAbout = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Sobre o BSL Stage", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const Texture& mark = brand().monogram;
        if (mark) {
            const float w = dp(160.0f);
            const float h = w * static_cast<float>(mark.height) / static_cast<float>(mark.width);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
            ImGui::Image(static_cast<ImTextureID>(mark.id), ImVec2(w, h));
        }
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize * 1.4f);
        ImGui::TextUnformatted("BSL Stage");
        ImGui::PopFont();
        ImGui::PushFont(fonts().mono, fonts().mono->LegacySize);
        ImGui::TextColored(theme::kTextDim, "Versão %s", BSL_VERSION);
        ImGui::PopFont();
        ImGui::TextUnformatted("Licença GPL-3.0");

        ImGui::Spacing();
        ImGui::SeparatorText("Bibliotecas");
        ImGui::BulletText("GLFW 3.4 (zlib)");
        ImGui::BulletText("glad / OpenGL 3.3 core");
        ImGui::BulletText("Dear ImGui 1.93 docking (MIT)");
        ImGui::BulletText("FFmpeg 9 (LGPL)");
        ImGui::BulletText("stb_image / stb_image_write / stb_truetype (domínio público)");
        ImGui::BulletText("nlohmann/json (MIT)");
        ImGui::BulletText("Spout2 SDK 2.007 (BSD-2)");
        ImGui::BulletText("NDI (runtime da NDI instalado à parte; este programa não o inclui)");

        ImGui::Spacing();
        ImGui::TextColored(theme::kTextDim, "Fontes: Barlow, JetBrains Mono (OFL); ícones Lucide (ISC)");
        ImGui::TextColored(theme::kTextDim, "NDI® is a registered trademark of Vizrt NDI AB");
        ImGui::Spacing();
        if (ImGui::Button("Fechar", ImVec2(dp(120.0f), 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace

void drawUi(PanelContext& ctx)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // As barras de menu e de status reservam as faixas de cima e de baixo antes do dockspace
    drawMenuBar(ctx);
    drawStatusBar(ctx);
    handleProjectShortcuts(ctx);
    handleLayoutShortcuts(ctx);
    applyPendingLayout(ctx); // preset de layout pedido (menu, Gerenciar, Alt+1..9) ou o último usado, ao abrir o app

    // Cada modo tem seu próprio dockspace (e painéis com nomes diferentes), então o imgui.ini
    // guarda os layouts separados.
    const bool mapping = ctx.mode == AppMode::Mapping;
    const bool pixelMap = ctx.mode == AppMode::PixelMap;
    const ImGuiID dockId = mapping ? kMappingDockSpaceId : pixelMap ? kPixelMapDockSpaceId : kDockSpaceId;
    bool& reset = mapping ? g_resetMapLayout : pixelMap ? g_resetPixelLayout : g_resetCompLayout;
    if (!ImGui::DockBuilderGetNode(dockId) || reset) {
        reset = false;
        if (mapping)
            buildMappingLayout(dockId, viewport->WorkSize);
        else if (pixelMap)
            buildPixelMapLayout(dockId, viewport->WorkSize);
        else
            buildDefaultLayout(dockId, viewport->WorkSize, ctx.settings.layout);
    }
    // Sem "central node": o ImGui entrega ao central todo o espaco que sobra ao redimensionar a janela e mantem os
    // outros paineis em pixels fixos. Sem ele, a divisao segue as proporcoes (SizeRef) e o layout acompanha a janela.
    if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockId))
        central->SetLocalFlags(central->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
    ImGui::DockSpaceOverViewport(dockId, viewport);

    if (mapping) {
        validateMappingSelection(ctx);
        mapHistoryObserve(ctx); // uma operacao de desfazer por gesto (arraste, valor digitado, botao...)
        drawMappingListPanel(ctx);
        drawMappingInputPanel(ctx);
        drawMappingOutputPanel(ctx);
        drawMappingPropertiesPanel(ctx);
    } else if (pixelMap) {
        validateMappingSelection(ctx);
        mapHistoryObserve(ctx); // "Criar mapeamento" mexe no OutputSetup: entra no desfazer do mapeamento
        pixelMapPrepare(ctx);
        drawPixelMapOverviewPanel(ctx);
        drawPixelMapConfigPanel(ctx);
        drawPixelMapTablePanel(ctx);
    } else {
        drawOutputPanel(ctx);
        drawPreviewPanel(ctx);
        drawCompositionPanel(ctx);
        drawClipPanel(ctx);
        drawBrowserPanel(ctx);
        drawGeneratorsPanel(ctx);
        drawEffectsPanel(ctx);
        drawCompositionTabPanel(ctx);
        drawLayerTabPanel(ctx);
        drawOutputTabPanel(ctx);
        if (g_focusFilesFrames > 0 && ImGui::FindWindowByName(panel::kFiles)) {
            ImGui::SetWindowFocus(panel::kFiles);
            --g_focusFilesFrames;
        } else if (g_focusTabFrames > 0 && g_focusTab && ImGui::FindWindowByName(g_focusTab)) {
            ImGui::SetWindowFocus(g_focusTab);
            --g_focusTabFrames;
        }
    }

    drawCaptureTargetChooser(ctx);
    drawAboutModal();
    drawLayoutWindows(ctx);
    drawPluginsWindow(ctx);
    drawCompatWindow(ctx);
    drawAudioWindow(ctx);
    drawAiWindow(ctx);
    ctx.project.drawModals();
}

void requestFocusTab(const std::string& key)
{
    struct Tab { const char* key; const char* window; };
    static const Tab tabs[] = {{"arquivos", panel::kFiles},         {"geradores", panel::kGenerators},
                               {"fontes", panel::kGenerators},
                               {"efeitos", panel::kEffects},        {"composicao", panel::kCompositionTab},
                               {"camada", panel::kLayerTab},        {"saida", panel::kOutputTab}};
    for (const Tab& t : tabs)
        if (key == t.key) {
            g_focusTab = t.window;
            g_focusTabFrames = 6;
        }
}

} // namespace bsl::ui
