#include "ui/Ui.h"

#include "app/ProjectSession.h"
#include "app/Paths.h"
#include "output/OutputRenderer.h"
#include "ui/Brand.h"
#include "ui/KeysUi.h"
#include "ui/LayoutUi.h"
#include "ui/Theme.h"
#include "ui/ViewUi.h"
#include "ui/Widgets.h"
#include "ui/panels/AiWindow.h"
#include "ui/panels/AudioWindow.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/LegalWindow.h"
#include "ui/panels/BrowserPanel.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/ClipPanel.h"
#include "ui/panels/CompositionPanel.h"
#include "ui/panels/MapView.h"
#include "ui/panels/MappingPanels.h"
#include "ui/panels/NetStreamWindow.h"
#include "ui/panels/PixelMapPanels.h"
#include "ui/panels/PluginsWindow.h"
#include "ui/panels/PluginWizard.h"
#include "ui/panels/PreviewPanel.h"
#include "ui/panels/ReportWindow.h"
#include "ui/panels/StatusBar.h"
#include "ui/panels/TabPanels.h"
#include "ui/panels/TransformEditor.h"
#include "ui/panels/UpdateUi.h"
#include "ui/panels/VideoCompat.h"
#include "app/Version.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

constexpr ImGuiID kDockSpaceId = 0x42534C43;
constexpr ImGuiID kMappingDockSpaceId = 0x42534C4D;
constexpr ImGuiID kPixelMapDockSpaceId = 0x42534C50;

bool g_resetCompLayout = false;
bool g_resetMapLayout = false;
bool g_resetPixelLayout = false;
bool g_resetRequested = false;
bool g_openAbout = false;
int g_focusFilesFrames = 0;
const char* g_focusTab = nullptr;
int g_focusTabFrames = 0;

struct LayoutPlan
{
    float leftWidth;
    float previewShare;
    float bottomHeight;
    float tabsShare;
};

LayoutPlan planFor(UiLayout layout)
{
    switch (layout) {
    case UiLayout::BigGrid: return {0.20f, 0.45f, 0.30f, 0.50f};
    case UiLayout::BigOutput: return {0.46f, 0.30f, 0.34f, 0.60f};
    default: return {0.30f, 0.50f, 0.44f, 0.50f};
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

const char* shortcutText(std::string& storage, const char* actionId)
{
    storage = actionShortcut(actionId);
    return storage.empty() ? nullptr : storage.c_str();
}

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
        std::string sc;
        if (ImGui::MenuItem(ICON_FILE_PLUS "  Novo##menu_novo", shortcutText(sc, "project.new")))
            project.requestNew();
        if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Abrir…##menu_abrir", shortcutText(sc, "project.open")))
            project.requestOpen();
        drawRecentMenu(ctx);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SAVE "  Salvar##menu_salvar", shortcutText(sc, "project.save")))
            project.requestSave();
        if (ImGui::MenuItem(ICON_SAVE_ALL "  Salvar como…##menu_salvar_como", shortcutText(sc, "project.saveAs")))
            project.requestSaveAs();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_PACKAGE_PLUS "  Novo plugin…##menu_novo_plugin"))
            openNewPluginWizard();
        if (ImGui::MenuItem(ICON_DOWNLOAD "  Importar plugin (.bslplugin)…##menu_importar_plugin"))
            openImportPluginWindow();
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
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SLIDERS "  DMX / Art-Net…##menu_dmx"))
            openDmxWindow();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Exibir")) {
        drawPanelMenuItems(ctx);
        ImGui::Separator();
        drawDetachMenu(ctx);
        if (ImGui::BeginMenu("Layout")) {
            for (int i = 0; i < kUiLayoutCount; ++i) {
                const auto layout = static_cast<UiLayout>(i);
                if (ImGui::MenuItem(uiLayoutLabel(layout), nullptr,
                                    ctx.settings.layout == layout && ctx.settings.lastLayoutPreset.empty())) {
                    ctx.settings.layout = layout;
                    clearActiveLayoutPreset(ctx);
                    ctx.settings.view.detached.clear();
                    showAllPanels(AppMode::Composition);
                    g_resetCompLayout = true;
                }
            }
            drawLayoutPresetsMenu(ctx);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_RESYNC "  Restaurar padrão##layout_restaurar"))
                requestResetLayout();
            ImGui::EndMenu();
        }
        drawWindowMenuItems(ctx);
        ImGui::Separator();
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
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_AUDIO_LINES "  Áudio…##menu_audio"))
            openAudioWindow();
        if (ImGui::MenuItem(ICON_SPARKLES "  IA…##menu_ia"))
            openAiWindow();
        if (ImGui::MenuItem(ICON_PLUG "  Plugins…##menu_plugins"))
            openPluginsWindow();
        if (ImGui::MenuItem(ICON_GLOBE "  Compatibilidade de vídeo…##menu_compat"))
            openCompatWindow();
        if (ImGui::MenuItem(ICON_ZAP "  Atalhos…##menu_atalhos"))
            openKeysEditor();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ajuda")) {
        if (ImGui::MenuItem(ICON_REFRESH "  Procurar atualizações…##menu_atualizar"))
            openUpdateWindow(true);
        if (const std::string news = updateMenuText(ctx); !news.empty()) {
            const std::string label = std::string(ICON_DOWNLOAD "  ") + news + "##menu_atualizacao_nova";
            if (ImGui::MenuItem(label.c_str()))
                openUpdateWindow(false);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_ALERT "  Reportar problema…##menu_reportar"))
            openReportWindow(update::ReportKind::Bug);
        if (ImGui::MenuItem(ICON_SPARKLES "  Sugerir melhoria…##menu_sugerir"))
            openReportWindow(update::ReportKind::Improvement);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FILE_TEXT "  Licenças e avisos…##menu_licencas"))
            openLegalWindow();
        if (ImGui::MenuItem("Sobre"))
            g_openAbout = true;
        ImGui::EndMenu();
    }

    ImGui::Dummy(ImVec2(dp(18.0f), 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(12.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(4.0f, 0.0f));
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
        ImGui::TextColored(theme::kTextDim, "Versão %s", BSLSTAGE_VERSION);
        ImGui::PopFont();
        ImGui::TextUnformatted("© 2026 BSL Light");
        ImGui::TextUnformatted("Software livre sob a GNU GPL, versão 3 (GPL-3.0)");

        ImGui::Spacing();
        ImGui::PushTextWrapPos(dp(420.0f));
        ImGui::TextColored(theme::kTextDim,
                           "Este programa não tem nenhuma garantia. Você pode redistribuí-lo e modificá-lo sob as condições da "
                           "GPL-3.0. O código-fonte completo está disponível em:");
        ImGui::PopTextWrapPos();
        ImGui::TextUnformatted(sourceCodeUrl().c_str());
        if (ImGui::Button(ICON_LINK "  Abrir o código-fonte##sobre_codigo"))
            openSourceCode();
        ImGui::SameLine();
        if (ImGui::Button("Licenças e avisos…##sobre_licencas")) {
            openLegalWindow();
            ImGui::CloseCurrentPopup();
        }

        ImGui::Spacing();
        ImGui::PushTextWrapPos(dp(420.0f));
        ImGui::TextColored(theme::kTextDim, "Bibliotecas de terceiros (GLFW, Dear ImGui, FFmpeg, PDFium, ONNX Runtime e outras) e as "
                                            "fontes Barlow, JetBrains Mono e ícones Lucide: veja Ajuda > Licenças e avisos…");
        ImGui::TextColored(theme::kTextDim, "NDI® is a registered trademark of Vizrt NDI AB. O runtime do NDI não faz parte deste programa.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Fechar", ImVec2(dp(120.0f), 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}

void drawUi(PanelContext& ctx)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    keysFrame(ctx);
    dmxFrame(ctx);
    drawMenuBar(ctx);
    if (g_resetRequested) {
        g_resetRequested = false;
        showAllPanels(ctx.mode);
        if (ctx.mode == AppMode::Mapping) {
            g_resetMapLayout = true;
        } else if (ctx.mode == AppMode::PixelMap) {
            g_resetPixelLayout = true;
        } else {
            g_resetCompLayout = true;
            clearActiveLayoutPreset(ctx);
        }
        ctx.settings.view.detached.clear();
    }
    if (statusBarVisible())
        drawStatusBar(ctx);
    applyPendingLayout(ctx);

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
    if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockId))
        central->SetLocalFlags(central->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
    ImGui::DockSpaceOverViewport(dockId, viewport, viewDockSpaceFlags());

    if (mapping) {
        validateMappingSelection(ctx);
        mapHistoryObserve(ctx);
        if (panelVisible(panel::kMapList))
            drawMappingListPanel(ctx);
        if (panelVisible(panel::kMapInput))
            drawMappingInputPanel(ctx);
        if (panelVisible(panel::kMapOutput))
            drawMappingOutputPanel(ctx);
        if (panelVisible(panel::kMapProps))
            drawMappingPropertiesPanel(ctx);
    } else if (pixelMap) {
        validateMappingSelection(ctx);
        mapHistoryObserve(ctx);
        pixelMapPrepare(ctx);
        if (panelVisible(panel::kPmOverview))
            drawPixelMapOverviewPanel(ctx);
        if (panelVisible(panel::kPmConfig))
            drawPixelMapConfigPanel(ctx);
        if (panelVisible(panel::kPmTable))
            drawPixelMapTablePanel(ctx);
    } else {
        transformHistoryObserve(ctx);
        if (panelVisible(panel::kOutput))
            drawOutputPanel(ctx);
        if (panelVisible(panel::kPreview))
            drawPreviewPanel(ctx);
        else
            previewPanelHidden();
        if (panelVisible(panel::kComposition))
            drawCompositionPanel(ctx);
        if (panelVisible(panel::kClip))
            drawClipPanel(ctx);
        if (panelVisible(panel::kFiles))
            drawBrowserPanel(ctx);
        if (panelVisible(panel::kGenerators))
            drawGeneratorsPanel(ctx);
        if (panelVisible(panel::kEffects))
            drawEffectsPanel(ctx);
        if (panelVisible(panel::kCompositionTab))
            drawCompositionTabPanel(ctx);
        if (panelVisible(panel::kLayerTab))
            drawLayerTabPanel(ctx);
        if (panelVisible(panel::kOutputTab))
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
    drawNetStreamWindow(ctx);
    drawAboutModal();
    drawUpdateWindow(ctx);
    drawReportWindow(ctx);
    drawLegalWindow(ctx);
    drawLayoutWindows(ctx);
    drawKeysWindows(ctx);
    drawPluginsWindow(ctx);
    drawPluginWizard(ctx);
    drawCompatWindow(ctx);
    drawAudioWindow(ctx);
    drawDmxWindow(ctx);
    drawAiWindow(ctx);
    ctx.project.drawModals();
    viewFrame();
}

void requestResetLayout()
{
    g_resetRequested = true;
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
            if (const view::PanelDef* def = view::findPanelByWindow(t.window))
                setPanelVisible(def->key, true);
        }
}

}
