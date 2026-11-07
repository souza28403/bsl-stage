#include "ui/panels/TabPanels.h"

#include "app/IsfSetup.h"
#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "ffgl/FfglCatalog.h"
#include "fx/EffectRegistry.h"
#include "fx/IsfLibrary.h"
#include "io/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/EffectChainWidget.h"
#include "ui/panels/PluginWizard.h"
#include "ui/panels/PluginsWindow.h"

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string>
#include <utility>
#include <vector>

namespace bsl::ui {

namespace {

struct PanelState
{
    char filter[128] = {};
    std::uint64_t version = ~0ull;
    std::vector<fx::EffectTypeInfo> types;
    bool openFolders = false;
};
PanelState g_state;
std::string g_onlyOpenSection;

bool openFirst(const char* section)
{
    return g_onlyOpenSection.empty() || g_onlyOpenSection == section;
}

bool effectRow(const fx::EffectTypeInfo& t, bool usable)
{
    ImGui::PushID(t.id.c_str());
    ImGui::BeginDisabled(!usable);
    const bool clicked = ImGui::Selectable("##linha", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, dp(22.0f)));
    qa::note(t.name);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float iconBox = dp(22.0f);
    drawIcon(dl, t.icon && t.icon[0] ? t.icon : ICON_SPARKLES, ImVec2(a.x + dp(4.0f) + iconBox * 0.5f, (a.y + b.y) * 0.5f),
             ImGui::GetColorU32(usable ? theme::kTextDim : theme::kWarning), 1.15f);
    dl->PushClipRect(a, b, true);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(a.x + dp(8.0f) + iconBox, a.y + (b.y - a.y - ImGui::GetFontSize()) * 0.5f),
                ImGui::GetColorU32(usable ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : theme::kWarning), t.name.c_str());
    dl->PopClipRect();
    if (t.ai) {
        const char* tag = "IA";
        const ImVec2 ts = ImGui::CalcTextSize(tag);
        const float pad = dp(5.0f);
        const ImVec2 bmin(b.x - ts.x - pad * 2.0f - dp(8.0f), a.y + (b.y - a.y - ts.y - dp(2.0f)) * 0.5f);
        const ImVec2 bmax(bmin.x + ts.x + pad * 2.0f, bmin.y + ts.y + dp(2.0f));
        dl->AddRectFilled(bmin, bmax, ImGui::GetColorU32(theme::kAccent), dp(3.0f));
        dl->AddText(ImVec2(bmin.x + pad, bmin.y + dp(1.0f)), IM_COL32(255, 255, 255, 255), tag);
    }

    if (usable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(kEffectPayload, t.id.c_str(), t.id.size() + 1);
        ImGui::TextUnformatted(t.name.c_str());
        ImGui::EndDragDropSource();
    }
    ImGui::EndDisabled();
    if (t.section == fx::EffectSection::Isf && !t.sourceFile.empty() && ImGui::BeginPopupContextItem("##isf_ctx")) {
        const std::filesystem::path file{std::u8string(t.sourceFile.begin(), t.sourceFile.end())};
        if (ImGui::MenuItem(ICON_FILE_CODE "  Abrir no editor##isf_ctx_editor"))
            openPluginInEditor(file);
        if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Mostrar na pasta##isf_ctx_pasta"))
            showPluginInFolder(file);
        if (ImGui::MenuItem(ICON_COPY "  Duplicar##isf_ctx_duplicar"))
            duplicateUserPlugin(file);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_UPLOAD "  Exportar .bslplugin…##isf_ctx_exportar", nullptr, false, t.error.empty()))
            openExportPluginWindow(t.id, file);
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        if (!t.error.empty())
            ImGui::SetTooltip(t.section == fx::EffectSection::Ffgl ? "Indisponível:\n%s" : "Não foi possível carregar este shader:\n%s",
                              t.error.c_str());
        else if (!t.description.empty())
            ImGui::SetTooltip("%s%s\n\nArraste para um clip, camada ou master — ou dê duplo clique para adicionar ao "
                              "clip (ou camada) selecionado%s",
                              t.description.c_str(),
                              t.ai ? "\n\nIA: usa inteligência artificial local, que roda neste PC (GPU via DirectML ou CPU). "
                                     "Nenhuma imagem sai do computador. Desligada no perfil Eco (Exibir > IA)."
                                   : "",
                              t.section == fx::EffectSection::Isf ? "\nBotão direito: abrir no editor, duplicar, exportar" : "");
    }
    const bool doubleClicked = usable && clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    return doubleClicked;
}

void addToSelection(PanelContext& ctx, const std::string& typeId)
{
    Composition& comp = ctx.composition;
    const Selection& sel = ctx.selection;
    if (sel.kind == Selection::Kind::Clip && comp.validCell(sel.layer, sel.column)
        && !comp.clip(sel.layer, sel.column).empty()) {
        comp.clip(sel.layer, sel.column).effects.add(typeId);
        return;
    }
    if (sel.kind != Selection::Kind::None && sel.layer >= 0 && sel.layer < comp.layerCount()) {
        comp.layers[static_cast<size_t>(sel.layer)].effects.add(typeId);
        return;
    }
    log::warn("Selecione um clip ou uma camada para adicionar o efeito.");
}

void drawSection(PanelContext& ctx, const char* sectionId, const std::vector<const fx::EffectTypeInfo*>& items,
                 bool filtering)
{
    std::vector<std::string> cats;
    for (const fx::EffectTypeInfo* t : items)
        if (std::find(cats.begin(), cats.end(), t->category) == cats.end())
            cats.push_back(t->category);
    for (const std::string& cat : cats) {
        if (filtering)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        else
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const std::string label = cat + "##cat_" + sectionId + "_" + cat;
        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (const fx::EffectTypeInfo* t : items)
                if (t->category == cat && effectRow(*t, t->error.empty()))
                    addToSelection(ctx, t->id);
            ImGui::TreePop();
        }
    }
}

void drawFoldersPopup(PanelContext& ctx)
{
    if (g_state.openFolders) {
        ImGui::OpenPopup("Pastas ISF##isf_pastas");
        g_state.openFolders = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(360.0f), 0.0f), ImVec2(dp(640.0f), dp(480.0f)));
    if (!ImGui::BeginPopup("Pastas ISF##isf_pastas"))
        return;

    ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
    ImGui::TextUnformatted("Pastas de shaders ISF");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(dp(440.0f));
    ImGui::TextColored(theme::kTextDim,
                       "O BSL Stage lê os arquivos .fs (e .vs opcional) destas pastas, inclusive subpastas. "
                       "Ao salvar um shader no editor, o efeito recompila sozinho; Recarregar lê as pastas de novo.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    auto openFolder = [](const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };

    dimLabel("Principal");
    pushMono();
    ImGui::TextWrapped("%s", paths::toUtf8(paths::isfDir()).c_str());
    popMono();
    if (ImGui::Button("Abrir no Explorer##isf_abrir_principal"))
        openFolder(paths::isfDir());

    ImGui::Spacing();
    dimLabel("Meus plugins (Novo plugin e Importar gravam aqui)");
    pushMono();
    ImGui::TextWrapped("%s", paths::toUtf8(paths::pluginsDir()).c_str());
    popMono();
    if (ImGui::Button("Abrir no Explorer##isf_abrir_meus"))
        openFolder(paths::pluginsDir());

    ImGui::Spacing();
    dimLabel("Extras");
    int remove = -1;
    for (size_t i = 0; i < ctx.settings.isfFolders.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (iconButton(ICON_X, ("##isf_pasta_remover_" + std::to_string(i + 1)).c_str(), "Tirar esta pasta da lista"))
            remove = static_cast<int>(i);
        ImGui::SameLine();
        pushMono();
        ImGui::TextWrapped("%s", ctx.settings.isfFolders[i].c_str());
        popMono();
        ImGui::PopID();
    }
    if (ctx.settings.isfFolders.empty())
        ImGui::TextColored(theme::kTextDim, "Nenhuma pasta extra.");
    if (remove >= 0) {
        ctx.settings.isfFolders.erase(ctx.settings.isfFolders.begin() + remove);
        ctx.settings.save();
        configureIsfLibrary(ctx.settings);
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_PLUS "  Adicionar pasta…##isf_pasta_adicionar")) {
        if (auto dir = dialogs::pickFolder(GetActiveWindow())) {
            const std::string utf8 = paths::toUtf8(*dir);
            if (std::find(ctx.settings.isfFolders.begin(), ctx.settings.isfFolders.end(), utf8) == ctx.settings.isfFolders.end()
                && ctx.settings.isfFolders.size() < 32) {
                ctx.settings.isfFolders.push_back(utf8);
                ctx.settings.save();
                configureIsfLibrary(ctx.settings);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_REFRESH "  Recarregar##isf_recarregar_popup"))
        fx::IsfLibrary::instance().rescan();
    ImGui::EndPopup();
}

}

void setEffectsPanelOnlyOpen(const std::string& section)
{
    g_onlyOpenSection = section;
}

void drawEffectsPanel(PanelContext& ctx)
{
    beginPanel(panel::kEffects);

    const float btn = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(dp(80.0f), ImGui::GetContentRegionAvail().x - 5.0f * (btn + spacing)));
    ImGui::InputTextWithHint("##efeitos_busca", "Buscar efeitos…", g_state.filter, sizeof(g_state.filter));
    ImGui::SameLine();
    if (iconButton(ICON_REFRESH, "##isf_recarregar", "Recarregar os shaders ISF das pastas"))
        fx::IsfLibrary::instance().rescan();
    ImGui::SameLine();
    if (iconButton(ICON_FOLDER_COG, "##isf_pastas", "Pastas de shaders ISF…"))
        g_state.openFolders = true;
    ImGui::SameLine();
    if (iconButton(ICON_PLUG, "##plugins_abrir", "Plugins FFGL: pastas, importar, bloqueados…"))
        openPluginsWindow();
    ImGui::SameLine();
    if (iconButton(ICON_PACKAGE_PLUS, "##plugin_novo", "Novo plugin… (crie seu próprio efeito ou gerador)"))
        openNewPluginWizard();
    ImGui::SameLine();
    if (iconButton(ICON_DOWNLOAD, "##plugin_importar", "Importar plugin (.bslplugin)…"))
        openImportPluginWindow();
    drawFoldersPopup(ctx);

    fx::EffectRegistry& reg = fx::EffectRegistry::instance();
    const std::uint64_t version = reg.version();
    if (version != g_state.version) {
        g_state.types = reg.types();
        g_state.version = version;
    }

    const std::string needle = foldSearchText(g_state.filter);
    const bool filtering = !needle.empty();
    auto pick = [&](fx::EffectSection section) {
        std::vector<const fx::EffectTypeInfo*> out;
        for (const fx::EffectTypeInfo& t : g_state.types) {
            if (t.section != section || t.generator)
                continue;
            if (filtering && foldSearchText(t.name).find(needle) == std::string::npos
                && foldSearchText(t.category).find(needle) == std::string::npos)
                continue;
            out.push_back(&t);
        }
        return out;
    };

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Header, theme::kPanelRaised);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::kPanelHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::kPanelActive);
    if (ImGui::BeginChild("##efeitos_lista", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        const auto bslItems = pick(fx::EffectSection::Bsl);
        ImGui::SetNextItemOpen(openFirst("bsl"), ImGuiCond_Once);
        if (brandHeader("BSL Light##secao_bsl")) {
            for (const fx::EffectTypeInfo* t : bslItems)
                if (effectRow(*t, t->error.empty()))
                    addToSelection(ctx, t->id);
            if (bslItems.empty())
                ImGui::TextColored(theme::kTextDim, "Nenhum efeito BSL com esse nome.");
        }

        const auto native = pick(fx::EffectSection::Native);
        ImGui::SetNextItemOpen(openFirst("nativos"), ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Nativos##secao_nativos")) {
            drawSection(ctx, "nativos", native, filtering);
            if (native.empty())
                ImGui::TextColored(theme::kTextDim, "Nenhum efeito com esse nome.");
        }

        const auto isf = pick(fx::EffectSection::Isf);
        fx::IsfLibrary& lib = fx::IsfLibrary::instance();
        ImGui::SetNextItemOpen(openFirst("isf"), ImGuiCond_Once);
        if (ImGui::CollapsingHeader("ISF##secao_isf")) {
            drawSection(ctx, "isf", isf, filtering);
            if (lib.scanning())
                ImGui::TextColored(theme::kTextDim, "Procurando shaders…");
            else if (isf.empty())
                ImGui::TextColored(theme::kTextDim, filtering ? "Nenhum shader ISF com esse nome."
                                                              : "Nenhum shader ISF de efeito nas pastas.");
        }

        const auto ffglItems = pick(fx::EffectSection::Ffgl);
        ffgl::Catalog& plugins = ffgl::Catalog::instance();
        ImGui::SetNextItemOpen(openFirst("ffgl"), ImGuiCond_Once);
        if (ImGui::CollapsingHeader("FFGL##secao_ffgl")) {
            drawSection(ctx, "ffgl", ffglItems, filtering);
            if (plugins.scanning())
                ImGui::TextColored(theme::kTextDim, "%s", plugins.scanStatus().c_str());
            else if (ffglItems.empty())
                ImGui::TextColored(theme::kTextDim, filtering ? "Nenhum plugin FFGL com esse nome."
                                                              : "Nenhum plugin FFGL de efeito encontrado.");
            const size_t blocked = plugins.blocked().size();
            if (blocked > 0) {
                ImGui::TextColored(theme::kWarning, "%zu plugin(s) bloqueado(s)", blocked);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Plugins que travaram ou caíram são deixados de fora para o show continuar.\n"
                                      "Clique em Plugins… para ver o motivo e tentar de novo");
            }
            if (plugins.webBrowserFound()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(theme::kTextDim, "O plugin WebBrowser foi ignorado. Use a fonte Web nativa (Fontes > Web).");
                ImGui::PopTextWrapPos();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    endPanel();
}

}
