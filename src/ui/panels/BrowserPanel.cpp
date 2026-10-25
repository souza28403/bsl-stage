#include "ui/panels/BrowserPanel.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "media/MediaFormats.h"
#include "media/WebSource.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/CaptureTargetChooser.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

constexpr size_t kMaxEntries = 3000;

struct Entry
{
    std::string name; // UTF-8
    std::filesystem::path path;
    bool isDirectory = false;
    media::MediaKind kind = media::MediaKind::Unknown;
};

struct BrowserState
{
    std::filesystem::path folder;
    std::vector<Entry> entries;
    char pathBuffer[1024] = {};
    bool loaded = false;
    bool truncated = false;
    bool error = false;
};

std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

void refresh(BrowserState& state)
{
    state.entries.clear();
    state.truncated = false;
    state.error = false;
    state.loaded = true;

    std::error_code ec;
    for (std::filesystem::directory_iterator it(state.folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        const bool isDir = it->is_directory(ec2);
        media::MediaKind kind = media::MediaKind::Unknown;
        if (!isDir) {
            kind = media::classify(it->path());
            if (kind == media::MediaKind::Unknown)
                continue; // so pastas e arquivos suportados
        }
        if (state.entries.size() >= kMaxEntries) {
            state.truncated = true;
            break;
        }
        Entry e;
        e.name = paths::toUtf8(it->path().filename());
        e.path = it->path();
        e.isDirectory = isDir;
        e.kind = kind;
        state.entries.push_back(std::move(e));
    }
    state.error = static_cast<bool>(ec);

    // Pastas primeiro, depois ordem alfabetica (sem diferenciar maiusculas)
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::sort(state.entries.begin(), state.entries.end(), [&](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return lower(a.name) < lower(b.name);
    });
}

void navigate(BrowserState& state, Settings& settings, const std::filesystem::path& folder)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        log::warn("Pasta não encontrada: " + paths::toUtf8(folder));
        return;
    }
    state.folder = folder;
    settings.lastBrowserFolder = paths::toUtf8(folder);
    const std::string text = paths::toUtf8(folder);
    std::snprintf(state.pathBuffer, sizeof(state.pathBuffer), "%s", text.c_str());
    refresh(state);
}

// Carrega o arquivo na celula selecionada (duplo clique)
void loadIntoSelection(PanelContext& ctx, const std::string& id)
{
    const Selection& sel = ctx.selection;
    if (sel.kind != Selection::Kind::Clip || !ctx.composition.validCell(sel.layer, sel.column)) {
        log::warn("Selecione uma célula da Composição para carregar o arquivo.");
        return;
    }
    Clip clip = Clip::fromId(id);
    if (clip.empty()) {
        log::warn("Formato não suportado: " + id);
        return;
    }
    ctx.composition.setClip(sel.layer, sel.column, std::move(clip));
    afterClipPlaced(ctx.composition, sel.layer, sel.column);
}

// Ícone do tipo de arquivo (no lugar das etiquetas [VID]/[IMG]/...)
const char* kindIcon(const Entry& e)
{
    if (e.isDirectory)
        return ICON_FOLDER;
    switch (e.kind) {
    case media::MediaKind::Video: return ICON_FILM;
    case media::MediaKind::Image: return ICON_IMAGE;
    case media::MediaKind::Document: return ICON_FILE_TEXT;
    case media::MediaKind::Web: return ICON_GLOBE;
    case media::MediaKind::Office: {
        const std::string ext = media::lowerExtension(e.path);
        return ext == "pptx" || ext == "ppt" || ext == "odp" ? ICON_PRESENTATION : ICON_FILE;
    }
    default: return ICON_FILE;
    }
}

const char* generatorIcon(const std::string& id)
{
    if (id == kSolidColorId) return ICON_PALETTE;
    if (id == kTextId) return ICON_TYPE;
    if (id == kScreenCaptureId) return ICON_MONITOR;
    if (id == kCaptureDeviceId) return ICON_VIDEO;
    if (id == kWebUrlId) return ICON_GLOBE;
    return ICON_GRID; // padrão de teste
}

// Linha de lista com ícone à esquerda. O Selectable não tem texto (o ImGui não recuaria o nome); ícone e nome são
// desenhados por cima e 'qaName' vira o rótulo do item para o teste de interface.
bool iconRow(const char* qaName, const char* icon, const char* label, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None)
{
    const bool clicked = ImGui::Selectable("##linha", false, flags, ImVec2(0.0f, dp(22.0f)));
    qa::note(qaName);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float iconBox = dp(22.0f);
    drawIcon(dl, icon, ImVec2(a.x + dp(4.0f) + iconBox * 0.5f, (a.y + b.y) * 0.5f), ImGui::GetColorU32(theme::kTextDim), 1.15f);
    dl->PushClipRect(a, b, true);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(a.x + dp(8.0f) + iconBox, a.y + (b.y - a.y - ImGui::GetFontSize()) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), label);
    dl->PopClipRect();
    return clicked;
}

void dragSource(const std::string& id, const std::string& label)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("BSL_MEDIA_PATH", id.c_str(), id.size() + 1);
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
}

} // namespace

void drawBrowserPanel(PanelContext& ctx)
{
    static BrowserState state;
    if (!state.loaded) {
        std::filesystem::path start = fromUtf8(ctx.settings.lastBrowserFolder);
        std::error_code ec;
        if (start.empty() || !std::filesystem::is_directory(start, ec))
            start = paths::desktopDir();
        state.folder = start;
        const std::string text = paths::toUtf8(start);
        std::snprintf(state.pathBuffer, sizeof(state.pathBuffer), "%s", text.c_str());
        refresh(state);
    }

    beginPanel(panel::kFiles);

    // Barra de caminho editavel + pasta acima
    std::filesystem::path goTo;
    if (iconButton(ICON_CORNER_LEFT_UP, "##acima", "Pasta acima")) {
        const std::filesystem::path parent = state.folder.parent_path();
        if (!parent.empty() && parent != state.folder)
            goTo = parent;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##caminho", state.pathBuffer, sizeof(state.pathBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        goTo = fromUtf8(state.pathBuffer);

    if (ImGui::BeginChild("##lista", ImVec2(0, 0), ImGuiChildFlags_None)) {
        for (size_t i = 0; i < state.entries.size(); ++i) {
            const Entry& e = state.entries[i];
            ImGui::PushID(static_cast<int>(i));
            if (e.isDirectory) {
                const std::string qaName = e.name + "/"; // o uiqa acha a pasta por este nome
                if (iconRow(qaName.c_str(), kindIcon(e), e.name.c_str()))
                    goTo = e.path;
            } else {
                iconRow(e.name.c_str(), kindIcon(e), e.name.c_str(), ImGuiSelectableFlags_AllowDoubleClick);
                const std::string id = paths::toUtf8(e.path);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    loadIntoSelection(ctx, id);
                dragSource(id, e.name);
            }
            ImGui::PopID();
        }
        if (state.entries.empty())
            ImGui::TextColored(theme::kTextDim, state.error ? "Não foi possível ler a pasta" : "Nenhuma mídia aqui");
        if (state.truncated)
            ImGui::TextColored(theme::kTextDim, "... lista cortada em %zu itens", kMaxEntries);
    }
    ImGui::EndChild();

    endPanel();

    if (!goTo.empty())
        navigate(state, ctx.settings, goTo);
}

void drawGeneratorsPanel(PanelContext& ctx)
{
    beginPanel(panel::kGenerators);

    for (const GeneratorInfo& g : allGenerators()) {
        const bool unavailable = std::string(g.id) == kWebUrlId && !media::WebSource::available();
        ImGui::PushID(g.id);
        ImGui::BeginDisabled(unavailable);
        iconRow(g.name, generatorIcon(g.id), g.name, ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            loadIntoSelection(ctx, g.id);
        if (!unavailable)
            dragSource(g.id, g.name);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (unavailable)
                ImGui::SetTooltip("Web desabilitada: o navegador (CEF) não foi encontrado nesta instalação.\n"
                                  "Compile com BSL_CEF_DIR apontando para a distribuição do CEF.");
            else
                ImGui::SetTooltip("Arraste para uma célula ou dê duplo clique para carregar na célula selecionada%s",
                                  g.live ? "\nFonte ao vivo: só uma instância (a Prévia mostra a que está tocando)" : "");
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim, "PDF, Word, PowerPoint e Excel entram pelo painel Arquivos (ou arraste do Explorer). "
                                        "Páginas .html soltas na grade abrem como Web.");
    ImGui::PopTextWrapPos();

    endPanel();
}

} // namespace bsl::ui
