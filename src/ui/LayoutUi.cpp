#include "ui/LayoutUi.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/ProjectSession.h"
#include "app/Settings.h"
#include "io/FileDialogs.h"
#include "ui/KeysUi.h"
#include "ui/LayoutPreset.h"
#include "ui/Theme.h"
#include "ui/ViewState.h"
#include "ui/Widgets.h"
#include "ui/panels/PreviewPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace bsl::ui {

namespace {

using layout::LayoutPreset;
using layout::PresetStore;

PresetStore& store()
{
    static std::unique_ptr<PresetStore> s;
    if (!s) {
        s = std::make_unique<PresetStore>(paths::appDataDir() / L"layouts");
        s->reload();
    }
    return *s;
}

std::optional<LayoutPreset> g_pending;
bool g_restoreChecked = false;

bool g_openSave = false;
bool g_focusName = false;
bool g_includeScale = false;
char g_nameBuf[96] = {};
std::string g_saveError;

bool g_openManage = false;
int g_renameIndex = -1;
bool g_renameFocus = false;
char g_renameBuf[96] = {};
std::string g_status;

std::string g_deleteName;
bool g_deleteOpened = false;

int g_exportRequest = -1;
bool g_importRequest = false;

const char* keyOf(UiLayout l)
{
    return uiLayoutKey(l);
}

LayoutPreset capture(const PanelContext& ctx, const std::string& name, bool includeScale)
{
    LayoutPreset p;
    p.name = name;
    size_t n = 0;
    const char* ini = ImGui::SaveIniSettingsToMemory(&n);
    p.dock = layout::extractDock(std::string(ini, n));
    p.hidden = ctx.settings.view.hidden;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    p.refW = static_cast<int>(std::lround(vp->WorkSize.x));
    p.refH = static_cast<int>(std::lround(vp->WorkSize.y));
    p.builtin = keyOf(ctx.settings.layout);

    const PreviewOptions& o = previewOptions();
    p.preview.source = o.source == PreviewSource::Layer ? "layer" : "clip";
    p.preview.checker = o.checker;
    p.preview.fit = o.fit == FitMode::Fill ? "fill" : "fit";

    p.hasUiScale = includeScale;
    p.uiMode = ctx.settings.uiScaleMode == UiScaleMode::System  ? "system"
               : ctx.settings.uiScaleMode == UiScaleMode::Fixed ? "fixed"
                                                                : "window";
    p.uiScale = ctx.settings.uiScale;
    return p;
}

void requestApply(const LayoutPreset& p)
{
    g_pending = p;
}

void applyPreset(PanelContext& ctx, const LayoutPreset& p, bool withDock = true)
{
    if (withDock
        && (layout::hasDockRoot(p.dock, layout::kCompDockRoot) || layout::hasDockRoot(p.dock, layout::kMapDockRoot))) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const double sx = p.refW > 0 ? static_cast<double>(vp->WorkSize.x) / p.refW : 1.0;
        const double sy = p.refH > 0 ? static_cast<double>(vp->WorkSize.y) / p.refH : 1.0;
        size_t n = 0;
        const char* cur = ImGui::SaveIniSettingsToMemory(&n);
        const std::string merged = layout::applyDock(std::string(cur, n), p.dock, sx, sy);
        ImGui::LoadIniSettingsFromMemory(merged.c_str(), merged.size());
        ImGui::MarkIniSettingsDirty();
    }

    if (withDock)
        ctx.settings.view.hidden = view::sanitizeKeys(p.hidden);

    PreviewOptions& o = previewOptions();
    o.source = p.preview.source == "layer" ? PreviewSource::Layer : PreviewSource::Clip;
    o.checker = p.preview.checker;
    o.fit = p.preview.fit == "fill" ? FitMode::Fill : FitMode::Fit;

    for (int i = 0; i < kUiLayoutCount; ++i)
        if (p.builtin == keyOf(static_cast<UiLayout>(i)))
            ctx.settings.layout = static_cast<UiLayout>(i);
    if (p.hasUiScale && withDock) {
        ctx.settings.uiScaleMode = p.uiMode == "system" ? UiScaleMode::System
                                   : p.uiMode == "fixed" ? UiScaleMode::Fixed
                                                         : UiScaleMode::Window;
        ctx.settings.uiScale = p.uiScale;
    }
    ctx.settings.lastLayoutPreset = p.name;
    ctx.settings.save();
    log::info("Layout aplicado: " + p.name);
}

std::string defaultName()
{
    for (int n = store().count() + 1; n < 1000; ++n) {
        const std::string name = "Layout " + std::to_string(n);
        if (store().find(name) < 0)
            return name;
    }
    return "Layout";
}

void setNameBuffer(const std::string& s)
{
    std::snprintf(g_nameBuf, sizeof(g_nameBuf), "%s", s.c_str());
}

void drawSaveModal(PanelContext& ctx)
{
    if (g_openSave) {
        setNameBuffer(defaultName());
        g_focusName = true;
        g_includeScale = false;
        g_saveError.clear();
        ImGui::OpenPopup("Salvar layout");
        g_openSave = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Salvar layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::TextUnformatted("Nome do layout");
    if (g_focusName) {
        ImGui::SetKeyboardFocusHere();
        g_focusName = false;
    }
    ImGui::SetNextItemWidth(dp(300.0f));
    const bool enter = ImGui::InputText("##layout_nome", g_nameBuf, sizeof(g_nameBuf),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::Checkbox("Incluir o tamanho da interface##layout_incluir_escala", &g_includeScale);
    ImGui::PushTextWrapPos(dp(300.0f));
    ImGui::TextColored(theme::kTextDim,
                       "Guarda a posição e o tamanho dos painéis (inclusive os soltos em janela), quais estão ocultos, o modo e as opções da Prévia.");
    ImGui::PopTextWrapPos();

    const std::string name = layout::sanitizeName(g_nameBuf);
    if (!name.empty() && store().find(name) >= 0)
        ImGui::TextColored(theme::kWarning, "Já existe um layout com esse nome: ele será substituído.");
    if (!g_saveError.empty())
        ImGui::TextColored(theme::kError, "%s", g_saveError.c_str());

    ImGui::Spacing();
    bool close = false;
    ImGui::BeginDisabled(name.empty());
    if (ImGui::Button("Salvar##layout_salvar_ok", ImVec2(dp(140.0f), 0.0f)) || (enter && !name.empty())) {
        std::string error;
        if (store().save(capture(ctx, name, g_includeScale), &error)) {
            const int index = store().find(name);
            ctx.settings.lastLayoutPreset = index >= 0 ? store().entries()[static_cast<size_t>(index)].preset.name : name;
            ctx.settings.save();
            log::info("Layout salvo: " + name);
            close = true;
        } else {
            g_saveError = error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar##layout_salvar_cancelar", ImVec2(dp(140.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
    if (close)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void deleteLayoutByName(PanelContext& ctx, const std::string& name)
{
    PresetStore& st = store();
    const int index = st.find(name);
    if (index < 0)
        return;
    const std::string realName = st.entries()[static_cast<size_t>(index)].preset.name;
    if (st.remove(index)) {
        if (ctx.settings.lastLayoutPreset == realName) {
            ctx.settings.lastLayoutPreset.clear();
            ctx.settings.save();
        }
        g_renameIndex = -1;
        g_status = "Layout \"" + realName + "\" excluído.";
        log::info("Layout excluído: " + realName);
    } else {
        g_status = "Não foi possível excluir \"" + realName + "\".";
    }
}

void drawDeleteModal(PanelContext& ctx)
{
    if (!g_deleteName.empty() && !g_deleteOpened) {
        ImGui::OpenPopup("Excluir layout");
        g_deleteOpened = true;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Excluir layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    ImGui::Text("Excluir o layout \"%s\"?", g_deleteName.c_str());
    ImGui::TextColored(theme::kTextDim, "Os painéis continuam como estão; só o layout salvo é apagado.");
    ImGui::Spacing();
    bool close = false;
    if (ImGui::Button("Excluir##layout_excluir_ok", ImVec2(dp(140.0f), 0.0f))) {
        deleteLayoutByName(ctx, g_deleteName);
        close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar##layout_excluir_cancelar", ImVec2(dp(140.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
    if (close) {
        g_deleteName.clear();
        g_deleteOpened = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawManageWindow(PanelContext& ctx)
{
    if (!g_openManage)
        return;
    ImGui::SetNextWindowSize(dp(640.0f, 340.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (ImGui::Begin("Gerenciar layouts", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        PresetStore& st = store();
        const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("##layout_lista", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders)) {
            if (st.count() == 0) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(theme::kTextDim,
                                   "Nenhum layout salvo. Use Exibir > Layout > Salvar layout atual… para guardar o arranjo dos painéis.");
                ImGui::PopTextWrapPos();
            }
            int toDelete = -1;
            int toApply = -1;
            const bool table = st.count() > 0
                               && ImGui::BeginTable("##layout_tabela", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings);
            if (table) {
                ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Atalho", ImGuiTableColumnFlags_WidthFixed, dp(48.0f));
                ImGui::TableSetupColumn("Ações", ImGuiTableColumnFlags_WidthFixed);
            }
            for (int i = 0; table && i < st.count(); ++i) {
                const LayoutPreset& p = st.entries()[static_cast<size_t>(i)].preset;
                const std::string id = std::to_string(i + 1);
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                if (g_renameIndex == i) {
                    if (g_renameFocus) {
                        ImGui::SetKeyboardFocusHere();
                        g_renameFocus = false;
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const bool entered = ImGui::InputText(("##layout_" + id + "_nome").c_str(), g_renameBuf, sizeof(g_renameBuf),
                                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (entered) {
                        std::string error;
                        const std::string oldName = p.name;
                        if (st.rename(i, g_renameBuf, &error)) {
                            if (ctx.settings.lastLayoutPreset == oldName)
                                ctx.settings.lastLayoutPreset = st.entries()[static_cast<size_t>(i)].preset.name;
                            g_status.clear();
                            g_renameIndex = -1;
                        } else {
                            g_status = error;
                        }
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        g_renameIndex = -1;
                    }
                } else {
                    const bool active = ctx.settings.lastLayoutPreset == p.name;
                    ImGui::TextColored(active ? theme::kText : theme::kTextDim, "%s", p.name.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                control::ActionArgs presetArgs;
                presetArgs.index = i;
                ImGui::TextColored(theme::kTextDim, "%s", actionShortcut("ui.layoutPreset", presetArgs).c_str());
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(("Aplicar##layout_" + id + "_aplicar").c_str()))
                    toApply = i;
                ImGui::SameLine();
                if (ImGui::Button(("Renomear##layout_" + id + "_renomear").c_str())) {
                    g_renameIndex = i;
                    g_renameFocus = true;
                    std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", p.name.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(("Exportar…##layout_" + id + "_exportar").c_str()))
                    g_exportRequest = i;
                ImGui::SameLine();
                if (ImGui::Button(("Excluir##layout_" + id + "_excluir").c_str()))
                    toDelete = i;
                ImGui::PopID();
            }
            if (table)
                ImGui::EndTable();
            if (toApply >= 0)
                requestApply(st.entries()[static_cast<size_t>(toApply)].preset);
            if (toDelete >= 0) {
                const std::string name = st.entries()[static_cast<size_t>(toDelete)].preset.name;
                if (st.remove(toDelete)) {
                    if (ctx.settings.lastLayoutPreset == name) {
                        ctx.settings.lastLayoutPreset.clear();
                        ctx.settings.save();
                    }
                    g_renameIndex = -1;
                    g_status = "Layout \"" + name + "\" excluído.";
                } else {
                    g_status = "Não foi possível excluir \"" + name + "\".";
                }
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Importar…##layout_importar"))
            g_importRequest = true;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::kTextDim, "%s", g_status.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - dp(100.0f) - ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Fechar##layout_fechar", ImVec2(dp(100.0f), 0.0f)))
            open = false;
    }
    ImGui::End();
    if (!open) {
        g_openManage = false;
        g_renameIndex = -1;
    }
}

}

void drawLayoutPresetsMenu(PanelContext& ctx)
{
    PresetStore& st = store();
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_SAVE "  Salvar layout atual como…##layout_salvar"))
        g_openSave = true;
    if (st.count() > 0)
        ImGui::Separator();
    for (int i = 0; i < st.count(); ++i) {
        const LayoutPreset& p = st.entries()[static_cast<size_t>(i)].preset;
        control::ActionArgs presetArgs;
        presetArgs.index = i;
        const std::string shortcut = actionShortcut("ui.layoutPreset", presetArgs);
        const std::string label = p.name + "##layout_preset_" + std::to_string(i + 1);
        if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), ctx.settings.lastLayoutPreset == p.name))
            requestApply(p);
    }
    ImGui::Separator();
    if (ImGui::BeginMenu(ICON_TRASH "  Excluir layout##layout_excluir", st.count() > 0)) {
        for (int i = 0; i < st.count(); ++i) {
            const std::string& name = st.entries()[static_cast<size_t>(i)].preset.name;
            if (ImGui::MenuItem((name + "##layout_excluir_" + std::to_string(i + 1)).c_str()))
                g_deleteName = name;
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(ICON_SLIDERS "  Gerenciar…##layout_gerenciar"))
        g_openManage = true;
}

bool applyLayoutPresetIndex(int index)
{
    const PresetStore& st = store();
    if (index < 0 || index >= st.count())
        return false;
    requestApply(st.entries()[static_cast<size_t>(index)].preset);
    return true;
}

void applyPendingLayout(PanelContext& ctx)
{
    if (!g_restoreChecked) {
        g_restoreChecked = true;
        if (!ctx.settings.lastLayoutPreset.empty()) {
            const int index = store().find(ctx.settings.lastLayoutPreset);
            if (index < 0)
                ctx.settings.lastLayoutPreset.clear();
            else if (!ImGui::DockBuilderGetNode(layout::kCompDockRoot))
                g_pending = store().entries()[static_cast<size_t>(index)].preset;
            else
                applyPreset(ctx, store().entries()[static_cast<size_t>(index)].preset, false);
        }
    }
    if (!g_pending)
        return;
    const LayoutPreset p = *g_pending;
    g_pending.reset();
    applyPreset(ctx, p);
}

void drawLayoutWindows(PanelContext& ctx)
{
    drawSaveModal(ctx);
    drawDeleteModal(ctx);
    drawManageWindow(ctx);
}

void runLayoutDialogs(void* owner)
{
    if (g_exportRequest >= 0) {
        const int index = g_exportRequest;
        g_exportRequest = -1;
        PresetStore& st = store();
        if (index < st.count()) {
            const std::string name = st.entries()[static_cast<size_t>(index)].preset.name;
            std::wstring stemText = std::filesystem::path(std::u8string(name.begin(), name.end())).wstring();
            for (wchar_t& c : stemText)
                if (std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos)
                    c = L'_';
            if (const auto file = dialogs::saveFile(owner, L"Exportar layout", L"Layout do BSL Stage (*.bsllayout)",
                                                    L"*.bsllayout", stemText, L"bsllayout")) {
                std::string error;
                g_status = st.exportTo(index, *file, &error) ? "Exportado: " + paths::toUtf8(file->filename())
                                                              : "Não foi possível exportar: " + error;
            }
        }
    }
    if (g_importRequest) {
        g_importRequest = false;
        if (const auto file = dialogs::openFile(owner, L"Layout do BSL Stage", L"*.bsllayout;*.json")) {
            std::string error, name;
            g_status = store().importFrom(*file, &error, &name) ? "Importado: " + name
                                                                 : "Não foi possível importar: " + error;
        }
    }
}

void clearActiveLayoutPreset(PanelContext& ctx)
{
    if (!ctx.settings.lastLayoutPreset.empty()) {
        ctx.settings.lastLayoutPreset.clear();
        ctx.settings.save();
    }
}

LayoutUiInfo layoutUiInfo()
{
    LayoutUiInfo info;
    for (const PresetStore::Entry& e : store().entries())
        info.presets.push_back(e.preset.name);
    return info;
}

}
