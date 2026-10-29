#include "ui/panels/PluginsWindow.h"

#include "app/FfglSetup.h"
#include "app/Paths.h"
#include "ffgl/FfglCatalog.h"
#include "ffgl/FfglImport.h"
#include "io/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

struct WindowState
{
    bool open = false;
    ffgl::ImportJob job;
    std::string message;    // resultado da última importação
    bool messageError = false;
};
WindowState g_window;

void openInExplorer(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Uma pasta fixa (somente leitura na lista): rótulo, caminho e "Abrir no Explorer"
void fixedFolder(const char* label, const std::filesystem::path& dir, const char* id)
{
    dimLabel(label);
    pushMono();
    ImGui::TextWrapped("%s", paths::toUtf8(dir).c_str());
    popMono();
    if (ImGui::Button((std::string("Abrir no Explorer##") + id).c_str()))
        openInExplorer(dir);
}

} // namespace

void openPluginsWindow()
{
    g_window.open = true;
}

void drawPluginsWindow(PanelContext& ctx)
{
    if (!g_window.open)
        return;
    Settings& st = ctx.settings;
    ffgl::Catalog& catalog = ffgl::Catalog::instance();

    // Resultado de uma importação que acabou
    ffgl::ImportResult imported;
    if (g_window.job.takeResult(imported)) {
        g_window.message = imported.message;
        g_window.messageError = !imported.ok();
        catalog.rescan();
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(580.0f), dp(560.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Plugins FFGL###janela_plugins", &g_window.open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Plugins FFGL 2.x de terceiros (efeitos e fontes). Os seus ficam em "
                       "Documents\\BSL Stage\\Plugins. Cada plugin é testado num processo separado: se um travar ou "
                       "cair, ele fica bloqueado e o show continua.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    // --- Pastas ---
    ImGui::SeparatorText("Pastas");
    fixedFolder("Meus plugins", paths::pluginsDir(), "plugins_abrir_meus");

    ImGui::Spacing();
    dimLabel("Pastas extras");
    int remove = -1;
    for (size_t i = 0; i < st.ffglFolders.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (iconButton(ICON_X, ("##plugins_pasta_remover_" + std::to_string(i + 1)).c_str(), "Tirar esta pasta da lista"))
            remove = static_cast<int>(i);
        ImGui::SameLine();
        pushMono();
        ImGui::TextWrapped("%s", st.ffglFolders[i].c_str());
        popMono();
        ImGui::PopID();
    }
    if (st.ffglFolders.empty())
        ImGui::TextColored(theme::kTextDim, "Nenhuma pasta extra.");
    if (remove >= 0) {
        st.ffglFolders.erase(st.ffglFolders.begin() + remove);
        st.save();
        configureFfglCatalog(st);
    }
    if (ImGui::Button(ICON_PLUS "  Adicionar pasta…##plugins_pasta_adicionar")) {
        if (auto dir = dialogs::pickFolder(GetActiveWindow())) {
            const std::string utf8 = paths::toUtf8(*dir);
            if (std::find(st.ffglFolders.begin(), st.ffglFolders.end(), utf8) == st.ffglFolders.end()
                && st.ffglFolders.size() < 32) {
                st.ffglFolders.push_back(utf8);
                st.save();
                configureFfglCatalog(st);
            }
        }
    }

    ImGui::Spacing();
    bool useResolume = st.ffglUseResolume;
    if (ImGui::Checkbox("Usar plugins do Resolume (Extra Effects)##plugins_resolume", &useResolume)) {
        st.ffglUseResolume = useResolume;
        st.save();
        configureFfglCatalog(st);
    }
    {
        const std::vector<std::filesystem::path> dirs = paths::resolumeEffectDirs();
        ImGui::PushTextWrapPos(0.0f);
        if (dirs.empty()) {
            ImGui::TextColored(theme::kTextDim, "Resolume não encontrado neste computador (opcional).");
        } else {
            for (const std::filesystem::path& dir : dirs) {
                pushMono();
                ImGui::TextColored(theme::kTextDim, "%s", paths::toUtf8(dir).c_str());
                popMono();
            }
        }
        ImGui::PopTextWrapPos();
    }

    // --- Importar / reescanear ---
    ImGui::Spacing();
    ImGui::SeparatorText("Importar e reescanear");
    const bool importing = g_window.job.running();
    ImGui::BeginDisabled(importing);
    if (ImGui::Button(ICON_PACKAGE_PLUS "  Importar plugins…##plugins_importar"))
        ImGui::OpenPopup("##plugins_importar_menu");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copia uma DLL (ou uma pasta de plugins) para Documents\\BSL Stage\\Plugins, junto com as "
                          "subpastas de que ela depende. A origem não é alterada.");
    if (ImGui::BeginPopup("##plugins_importar_menu")) {
        std::filesystem::path chosen;
        if (ImGui::MenuItem("Arquivo DLL…##plugins_importar_dll")) {
            if (auto file = dialogs::openFile(GetActiveWindow(), L"Plugins FFGL (*.dll)", L"*.dll"))
                chosen = *file;
        }
        if (ImGui::MenuItem("Pasta…##plugins_importar_pasta")) {
            if (auto dir = dialogs::pickFolder(GetActiveWindow()))
                chosen = *dir;
        }
        if (!chosen.empty()) {
            g_window.message.clear();
            g_window.job.start(chosen, paths::pluginsDir());
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_REFRESH "  Reescanear##plugins_reescanear"))
        catalog.rescan();
    if (importing)
        ImGui::TextColored(theme::kTextDim, "Copiando plugins…");
    else if (!g_window.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(g_window.messageError ? theme::kWarning : theme::kSuccess, "%s", g_window.message.c_str());
        ImGui::PopTextWrapPos();
    }

    // --- Estado ---
    ImGui::Spacing();
    ImGui::SeparatorText("Plugins encontrados");
    const std::vector<ffgl::EntryPtr> entries = catalog.entries();
    int effects = 0, sources = 0, mixers = 0;
    for (const ffgl::EntryPtr& e : entries) {
        switch (e->info.type) {
        case ffgl::kTypeEffect: ++effects; break;
        case ffgl::kTypeSource: ++sources; break;
        default: ++mixers; break;
        }
    }
    if (catalog.scanning())
        ImGui::TextColored(theme::kTextDim, "%s", catalog.scanStatus().c_str());
    ImGui::Text("%d efeito(s), %d fonte(s), %d mixer(s)", effects, sources, mixers);
    if (catalog.ignoredCount() > 0)
        ImGui::TextColored(theme::kTextDim, "%d DLL(s) ignorada(s): não são plugins FFGL x64 (nada foi carregado).",
                           catalog.ignoredCount());
    if (catalog.webBrowserFound()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kTextDim, "O plugin WebBrowser foi ignorado. Use a fonte Web nativa (Fontes > Web).");
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Bloqueados");
    const std::vector<ffgl::BlockedPlugin> blocked = catalog.blocked();
    if (blocked.empty())
        ImGui::TextColored(theme::kTextDim, "Nenhum plugin bloqueado.");
    for (size_t i = 0; i < blocked.size(); ++i) {
        const ffgl::BlockedPlugin& b = blocked[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, b.session ? theme::kError : theme::kWarning);
        ImGui::TextUnformatted(b.fileName.c_str());
        ImGui::PopStyleColor();
        if (!b.origin.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(theme::kTextDim, "(%s)", b.origin.c_str());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", b.path.c_str());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kTextDim, "%s%s", b.session ? "Falhou durante o uso: " : "", b.reason.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button(("Tentar de novo##plugins_tentar_" + std::to_string(i + 1)).c_str()))
            catalog.retry(b.path);
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace bsl::ui
