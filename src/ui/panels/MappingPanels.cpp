#include "ui/panels/MappingPanels.h"

#include "app/Log.h"
#include "app/Qa.h"
#include "output/OutputRenderer.h"
#include "output/Warp.h"
#include "render/Compositor.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace bsl::ui {

namespace {

float handleRadius() { return dp(7.0f); } // raio de captura das alças, em px da tela

ImU32 color(const ImVec4& c, float alpha = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

Screen* selectedScreen(PanelContext& ctx)
{
    auto& screens = ctx.output.screens;
    const int i = ctx.mapping.screen;
    return i >= 0 && i < static_cast<int>(screens.size()) ? &screens[static_cast<size_t>(i)] : nullptr;
}

Slice* selectedSlice(PanelContext& ctx)
{
    Screen* s = selectedScreen(ctx);
    const int i = ctx.mapping.slice;
    return s && i >= 0 && i < static_cast<int>(s->slices.size()) ? &s->slices[static_cast<size_t>(i)] : nullptr;
}

// Setas: deslocamento em px (Shift = 10). Ignorado enquanto se digita num campo.
bool arrowDelta(int& dx, int& dy)
{
    const ImGuiIO& io = ImGui::GetIO();
    dx = dy = 0;
    if (io.WantTextInput)
        return false;
    const int step = io.KeyShift ? 10 : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dx -= step;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += step;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) dy -= step;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) dy += step;
    return dx != 0 || dy != 0;
}

void ownArrowKeys()
{
    for (ImGuiKey k : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow})
        ImGui::SetItemKeyOwner(k);
}

// Imagem encaixada no espaço livre, mantendo a proporção e deixando uma margem
struct FitRect
{
    ImVec2 min;
    ImVec2 size;
    float scale = 1.0f; // px de tela por px do conteúdo
};

bool fitImage(float aspect, float margin, float captionHeight, FitRect& out, int contentWidth)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 size(avail.x - margin * 2.0f, avail.y - captionHeight - margin * 2.0f);
    if (size.x < dp(8.0f) || size.y < dp(8.0f))
        return false;
    if (size.x / size.y > aspect)
        size.x = size.y * aspect;
    else
        size.y = size.x / aspect;

    ImVec2 cursor = ImGui::GetCursorPos();
    cursor.x += (avail.x - size.x) * 0.5f;
    cursor.y += (avail.y - captionHeight - size.y) * 0.5f;
    ImGui::SetCursorPos(cursor);
    out.min = ImGui::GetCursorScreenPos();
    out.size = size;
    out.scale = size.x / static_cast<float>(std::max(contentWidth, 1));
    return true;
}

void caption(const char* fmt, ...) IM_FMTARGS(1);
void caption(const char* fmt, ...)
{
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x, ImGui::GetWindowHeight()
                                                                - ImGui::GetTextLineHeightWithSpacing()
                                                                - padding() * 0.5f));
    pushMono();
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
    ImGui::PopFont();
}

float distance(ImVec2 a, ImVec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// --------------------------------------------------------------------------------------------
// Entrada: retângulos de entrada sobre o canvas

enum class DragMode { None, Move, Resize };

struct InputDrag
{
    DragMode mode = DragMode::None;
    int handle = -1;
    ImVec2 startMouse;
    RectI startRect;
};
InputDrag g_inDrag;

// Alças: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
ImVec2 handlePos(ImVec2 a, ImVec2 b, int h)
{
    const float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f;
    switch (h) {
    case 0: return {a.x, a.y};
    case 1: return {cx, a.y};
    case 2: return {b.x, a.y};
    case 3: return {b.x, cy};
    case 4: return {b.x, b.y};
    case 5: return {cx, b.y};
    case 6: return {a.x, b.y};
    default: return {a.x, cy};
    }
}

bool handleLeft(int h) { return h == 0 || h == 6 || h == 7; }
bool handleRight(int h) { return h == 2 || h == 3 || h == 4; }
bool handleTop(int h) { return h == 0 || h == 1 || h == 2; }
bool handleBottom(int h) { return h == 4 || h == 5 || h == 6; }

ImGuiMouseCursor handleCursor(int h)
{
    switch (h) {
    case 0: case 4: return ImGuiMouseCursor_ResizeNWSE;
    case 2: case 6: return ImGuiMouseCursor_ResizeNESW;
    case 1: case 5: return ImGuiMouseCursor_ResizeNS;
    default: return ImGuiMouseCursor_ResizeEW;
    }
}

int hitHandle(ImVec2 a, ImVec2 b, ImVec2 mouse)
{
    int best = -1;
    float bestDist = handleRadius() + dp(2.0f);
    for (int h = 0; h < 8; ++h) {
        const float d = distance(handlePos(a, b, h), mouse);
        if (d < bestDist) {
            bestDist = d;
            best = h;
        }
    }
    return best;
}

void clampRectToCanvas(RectI& r, int cw, int ch)
{
    r.w = std::clamp(r.w, 1, cw);
    r.h = std::clamp(r.h, 1, ch);
    r.x = std::clamp(r.x, 0, cw - r.w);
    r.y = std::clamp(r.y, 0, ch - r.h);
}

// --------------------------------------------------------------------------------------------
// Saída: cantos do slice sobre a Screen

enum class QuadDrag { None, Corner, Move };

struct OutputDrag
{
    QuadDrag mode = QuadDrag::None;
    ImVec2 startMouse;
    Point2 startCorners[4];
};
OutputDrag g_outDrag;

void clampCorner(Point2& p, const Screen& s)
{
    p.x = std::clamp(p.x, -0.25f * static_cast<float>(s.width), 1.25f * static_cast<float>(s.width));
    p.y = std::clamp(p.y, -0.25f * static_cast<float>(s.height), 1.25f * static_cast<float>(s.height));
}

// --------------------------------------------------------------------------------------------
// Propriedades

bool inputText(const char* id, std::string& value)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText(id, buf, sizeof(buf))) {
        value = buf;
        return true;
    }
    return false;
}

// Números arrastáveis (hot text) que ocupam a largura da célula: arrastar muda o valor, clicar abre o campo de
// digitação e o limite vale ao confirmar (Enter, Tab ou clicar fora).
bool inputInt(const char* id, int& value, int lo, int hi, float step = 2.0f)
{
    return hotInt(id, value, lo, hi, step, "%d", nullptr, INT_MIN, -1.0f);
}

bool inputFloat(const char* id, float& value, float lo, float hi, const char* format = "%.1f", float step = 0.5f,
                float def = NAN)
{
    return hotNumber(id, value, lo, hi, step, format, nullptr, def, -1.0f);
}

void dim(const char* text)
{
    ImGui::TextColored(theme::kTextDim, "%s", text);
}

void tableHeader4(const char* a, const char* b, const char* c, const char* d)
{
    ImGui::TableNextRow();
    const char* labels[4] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) {
        ImGui::TableSetColumnIndex(i);
        pushMono();
        dim(labels[i]);
        ImGui::PopFont();
    }
}

// Resolução nova: os cantos dos slices acompanham (um slice que preenchia a tela continua preenchendo)
void setScreenResolution(Screen& s, int w, int h)
{
    w = std::clamp(w, 16, 8192);
    h = std::clamp(h, 16, 8192);
    if (w == s.width && h == s.height)
        return;
    const float kx = static_cast<float>(w) / static_cast<float>(s.width);
    const float ky = static_cast<float>(h) / static_cast<float>(s.height);
    for (Slice& sl : s.slices)
        for (Point2& c : sl.corners) {
            c.x *= kx;
            c.y *= ky;
        }
    s.width = w;
    s.height = h;
}

std::string destinationLabel(const Screen& s, const std::vector<MonitorInfo>& monitors)
{
    switch (s.kind) {
    case OutputKind::Monitor: {
        for (const MonitorInfo& m : monitors)
            if (m.index == s.monitorIndex && m.name == s.monitorName)
                return "Monitor " + std::to_string(m.index + 1) + ": " + m.name;
        return "Monitor ausente: " + s.monitorName;
    }
    case OutputKind::Window: return "Janela";
    case OutputKind::Spout: return "Spout";
    default: return "Desativado";
    }
}

void screenProperties(PanelContext& ctx, Screen& screen)
{
    ImGui::SeparatorText("Tela");

    dim("Nome");
    ImGui::PushID("telaNome");
    inputText("##nome", screen.name);
    ImGui::PopID();
    ImGui::Checkbox("Ativa", &screen.enabled);

    dim("Destino");
    const std::vector<MonitorInfo>& monitors = ctx.outputRenderer.monitors();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##destino", destinationLabel(screen, monitors).c_str())) {
        if (ImGui::Selectable("Desativado", screen.kind == OutputKind::Disabled))
            screen.kind = OutputKind::Disabled;
        for (const MonitorInfo& m : monitors) {
            char label[256];
            std::snprintf(label, sizeof(label), "Monitor %d: %s  (%dx%d)%s", m.index + 1, m.name.c_str(), m.width,
                          m.height, m.primary ? "  principal" : "");
            const bool selected = screen.kind == OutputKind::Monitor && screen.monitorIndex == m.index
                                  && screen.monitorName == m.name;
            if (ImGui::Selectable(label, selected)) {
                screen.kind = OutputKind::Monitor;
                screen.monitorIndex = m.index;
                screen.monitorName = m.name;
                setScreenResolution(screen, m.width, m.height);
            }
        }
        if (ImGui::Selectable("Janela (teste)", screen.kind == OutputKind::Window))
            screen.kind = OutputKind::Window;
        if (ImGui::Selectable("Spout", screen.kind == OutputKind::Spout))
            screen.kind = OutputKind::Spout;
        ImGui::EndCombo();
    }

    switch (ctx.outputRenderer.status(screen.id)) {
    case ScreenStatus::Active: ImGui::TextColored(theme::kSuccess, "Saindo"); break;
    case ScreenStatus::MonitorMissing: ImGui::TextColored(theme::kWarning, "Monitor não encontrado: sem saída"); break;
    case ScreenStatus::Error: ImGui::TextColored(theme::kWarning, "Não foi possível abrir a saída"); break;
    default: dim(screen.enabled ? "Sem destino" : "Tela desativada"); break;
    }

    if (screen.kind == OutputKind::Spout) {
        dim("Nome do sender Spout");
        ImGui::PushID("spoutNome");
        inputText("##spout", screen.spoutName);
        ImGui::PopID();
    }

    dim("Resolução");
    if (screen.kind == OutputKind::Monitor) {
        pushMono();
        ImGui::Text("%d x %d  (do monitor)", screen.width, screen.height);
        ImGui::PopFont();
    } else {
        int w = screen.width, h = screen.height;
        if (ImGui::BeginTable("##res", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool cw = inputInt("##resW", w, 16, 8192, 4.0f);
            ImGui::TableSetColumnIndex(1);
            const bool ch = inputInt("##resH", h, 16, 8192, 4.0f);
            if (cw || ch)
                setScreenResolution(screen, w, h);
            ImGui::EndTable();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (beginCombo("##resPreset", "Predefinições")) {
            struct Preset { const char* label; int w, h; };
            for (const Preset& p : {Preset{"1920 x 1080", 1920, 1080}, Preset{"1280 x 720", 1280, 720},
                                     Preset{"3840 x 2160", 3840, 2160}, Preset{"1080 x 1920", 1080, 1920},
                                     Preset{"Tamanho do canvas", ctx.composition.width, ctx.composition.height}})
                if (ImGui::Selectable(p.label))
                    setScreenResolution(screen, p.w, p.h);
            ImGui::EndCombo();
        }
    }
}

void sliceProperties(PanelContext& ctx, Screen& screen, Slice& slice)
{
    (void)screen;
    const int cw = ctx.composition.width, ch = ctx.composition.height;

    ImGui::SeparatorText("Slice");
    dim("Nome");
    ImGui::PushID("sliceNome");
    inputText("##nome", slice.name);
    ImGui::PopID();
    ImGui::Checkbox("Ativo", &slice.enabled);

    ImGui::Spacing();
    dim("Entrada no canvas (px)");
    if (ImGui::BeginTable("##entrada", 4, ImGuiTableFlags_SizingStretchSame)) {
        tableHeader4("X", "Y", "L", "A");
        ImGui::TableNextRow();
        RectI& r = slice.input;
        ImGui::TableSetColumnIndex(0);
        // O retangulo precisa continuar dentro do canvas: X/Y ate (canvas - tamanho), L/A ate (canvas - posicao)
        inputInt("##x", r.x, 0, std::max(0, cw - r.w));
        ImGui::TableSetColumnIndex(1);
        inputInt("##y", r.y, 0, std::max(0, ch - r.h));
        ImGui::TableSetColumnIndex(2);
        inputInt("##w", r.w, 1, std::max(1, cw - r.x));
        ImGui::TableSetColumnIndex(3);
        inputInt("##h", r.h, 1, std::max(1, ch - r.y));
        ImGui::EndTable();
    }

    ImGui::Spacing();
    dim("Saída: cantos na tela (px)");
    if (ImGui::BeginTable("##cantos", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, dp(92.0f));
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthStretch);
        static const char* const names[4] = {"Sup. esq.", "Sup. dir.", "Inf. dir.", "Inf. esq."};
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            if (ctx.mapping.corner == i)
                ImGui::TextColored(theme::kAccent, "%s", names[i]);
            else
                dim(names[i]);
            ImGui::TableSetColumnIndex(1);
            if (inputFloat("##cx", slice.corners[i].x, -1.0e5f, 1.0e5f, "%.1f", 1.0f))
                ctx.mapping.corner = i;
            ImGui::TableSetColumnIndex(2);
            if (inputFloat("##cy", slice.corners[i].y, -1.0e5f, 1.0e5f, "%.1f", 1.0f))
                ctx.mapping.corner = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Ajustar saída ao retângulo de entrada", ImVec2(-FLT_MIN, 0.0f)))
        fitOutputToInput(slice);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Quad de saída = retângulo de entrada, 1:1 em pixels (mesma posição e tamanho)");
    if (ImGui::Button("Resetar warp", ImVec2(-FLT_MIN, 0.0f)))
        resetWarp(slice);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tira a perspectiva: o quad vira o retângulo que envolve os 4 cantos");

    ImGui::Spacing();
    ImGui::Checkbox("Espelhar H", &slice.flipH);
    ImGui::SameLine(0.0f, dp(20.0f));
    ImGui::Checkbox("Espelhar V", &slice.flipV);

    ImGui::SeparatorText("Imagem");
    dim("Opacidade");
    hotPercent("##opacidade", slice.opacity, 1.0f, -1.0f);

    dim("Ganho R / G / B");
    if (ImGui::BeginTable("##ganho", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(i);
            inputFloat("##g", slice.gain[i], 0.0f, 4.0f, "%.2f", 0.01f, 1.0f);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    dim("Brilho");
    inputFloat("##brilho", slice.brightness, -1.0f, 1.0f, "%.2f", 0.01f, 0.0f);

    ImGui::SeparatorText("Soft edge");
    if (ImGui::BeginTable("##edge", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("lado", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("g", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        pushMono();
        dim("largura px");
        ImGui::TableSetColumnIndex(2);
        dim("gamma");
        ImGui::PopFont();
        static const char* const sides[4] = {"Esquerda", "Topo", "Direita", "Base"};
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            dim(sides[i]);
            ImGui::TableSetColumnIndex(1);
            inputFloat("##w", slice.edges[i].width, 0.0f, 4096.0f, "%.1f", 1.0f, 0.0f);
            ImGui::TableSetColumnIndex(2);
            inputFloat("##g", slice.edges[i].gamma, 0.1f, 5.0f, "%.2f", 0.01f, 1.0f);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// --------------------------------------------------------------------------------------------
// Lista

struct RenameState
{
    int screen = -1;
    int slice = -2; // -1 = renomeando a Screen; >= 0 = um slice dela; -2 = ninguém
    bool focus = false;
    char buffer[128] = {};
};
RenameState g_rename;

void beginRename(int screen, int slice, const std::string& current)
{
    g_rename.screen = screen;
    g_rename.slice = slice;
    g_rename.focus = true;
    std::snprintf(g_rename.buffer, sizeof(g_rename.buffer), "%s", current.c_str());
}

bool renaming(int screen, int slice)
{
    return g_rename.screen == screen && g_rename.slice == slice;
}

// Campo de texto no lugar do nome. true quando terminou (Enter ou perdeu o foco); 'apply' diz se vale.
bool renameField(std::string& name, bool& apply)
{
    if (g_rename.focus) {
        ImGui::SetKeyboardFocusHere();
        g_rename.focus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool entered = ImGui::InputText("##renomear", g_rename.buffer, sizeof(g_rename.buffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (entered || ImGui::IsItemDeactivated() || cancelled) {
        apply = !cancelled && g_rename.buffer[0] != '\0';
        if (apply)
            name = g_rename.buffer;
        g_rename = {};
        return true;
    }
    return false;
}

} // namespace

void validateMappingSelection(PanelContext& ctx)
{
    MappingSelection& m = ctx.mapping;
    const int screens = static_cast<int>(ctx.output.screens.size());
    if (screens == 0) {
        m = {-1, -1, -1};
        return;
    }
    m.screen = std::clamp(m.screen, 0, screens - 1);
    const int slices = static_cast<int>(ctx.output.screens[static_cast<size_t>(m.screen)].slices.size());
    m.slice = slices == 0 ? -1 : std::clamp(m.slice, -1, slices - 1);
    if (m.slice < 0)
        m.corner = -1;
}

void drawMappingListPanel(PanelContext& ctx)
{
    beginPanel(panel::kMapList);

    OutputSetup& out = ctx.output;
    MappingSelection& sel = ctx.mapping;
    const int cw = ctx.composition.width, ch = ctx.composition.height;

    // Quatro botões de ícone na largura da lista: + Tela, + Slice, Duplicar, Remover
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 iconSize((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) * 0.25f, ImGui::GetFrameHeight());
    if (iconButton(ICON_PLUS ICON_MONITOR, "##map_add_tela", "Nova tela", iconSize)) {
        sel.screen = out.addScreen(cw, ch);
        sel.slice = 0;
        sel.corner = -1;
    }
    ImGui::BeginDisabled(!selectedScreen(ctx));
    ImGui::SameLine();
    if (iconButton(ICON_SQUARE_PLUS, "##map_add_slice", "Novo slice na tela selecionada", iconSize)) {
        sel.slice = out.addSlice(sel.screen, cw, ch);
        sel.corner = -1;
    }
    ImGui::SameLine();
    if (iconButton(ICON_COPY, "##map_duplicar", "Duplicar o slice selecionado (ou a tela, se nenhum slice estiver selecionado)",
                   iconSize)) {
        if (sel.slice >= 0)
            sel.slice = out.duplicateSlice(sel.screen, sel.slice);
        else
            sel.screen = out.duplicateScreen(sel.screen);
        sel.corner = -1;
    }
    ImGui::SameLine();
    if (iconButton(ICON_TRASH, "##map_remover", "Remover o slice selecionado (ou a tela, se nenhum slice estiver selecionado)",
                   iconSize)) {
        if (sel.slice >= 0)
            out.removeSlice(sel.screen, sel.slice);
        else
            out.removeScreen(sel.screen);
        sel.corner = -1;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(out.screens.size()); ++i) {
        Screen& screen = out.screens[static_cast<size_t>(i)];
        ImGui::PushID(screen.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen
                                   | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (sel.screen == i && sel.slice < 0)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = false;
        if (renaming(i, -1)) {
            open = ImGui::TreeNodeEx("##tela", flags | ImGuiTreeNodeFlags_AllowOverlap, "%s", "");
            ImGui::SameLine();
            bool apply = false;
            renameField(screen.name, apply);
        } else {
            if (!screen.enabled)
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
            open = ImGui::TreeNodeEx("##tela", flags, "%s", screen.name.c_str());
            if (!screen.enabled)
                ImGui::PopStyleColor();
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                sel.screen = i;
                sel.slice = -1;
                sel.corner = -1;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                beginRename(i, -1, screen.name);

            // destino à direita
            const ScreenStatus st = ctx.outputRenderer.status(screen.id);
            const char* tag = outputKindName(screen.kind);
            const float w = ImGui::CalcTextSize(tag).x;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - w - dp(2.0f));
            ImGui::TextColored(st == ScreenStatus::Active ? theme::kSuccess
                               : (st == ScreenStatus::MonitorMissing || st == ScreenStatus::Error) ? theme::kWarning
                                                                                                    : theme::kTextDim,
                               "%s", tag);
        }

        if (open) {
            for (int j = 0; j < static_cast<int>(screen.slices.size()); ++j) {
                Slice& slice = screen.slices[static_cast<size_t>(j)];
                ImGui::PushID(j);
                ImGuiTreeNodeFlags lf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                        | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (sel.screen == i && sel.slice == j)
                    lf |= ImGuiTreeNodeFlags_Selected;

                if (renaming(i, j)) {
                    ImGui::TreeNodeEx("##slice", lf | ImGuiTreeNodeFlags_AllowOverlap, "%s", "");
                    ImGui::SameLine();
                    bool apply = false;
                    renameField(slice.name, apply);
                } else {
                    if (!slice.enabled)
                        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
                    ImGui::TreeNodeEx("##slice", lf, "%s", slice.name.c_str());
                    if (!slice.enabled)
                        ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked()) {
                        sel.screen = i;
                        sel.slice = j;
                        sel.corner = -1;
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                        beginRename(i, j, slice.name);
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (out.screens.empty())
        ImGui::TextColored(theme::kTextDim, "Nenhuma tela. Use o botão de nova tela acima.");

    endPanel();
}

void drawMappingInputPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kMapInput);

    const Compositor& canvas = ctx.canvas;
    const int cw = canvas.width(), ch = canvas.height();
    Screen* screen = selectedScreen(ctx);
    const float lineH = ImGui::GetTextLineHeightWithSpacing();

    FitRect fit;
    if (screen && fitImage(static_cast<float>(cw) / static_cast<float>(ch), dp(14.0f), lineH, fit, cw)) {
        // A area clicavel passa um pouco da imagem: as alcas de um slice que cobre o canvas inteiro ficam nas bordas
        // e a metade de fora delas tambem precisa pegar.
        ImGui::SetCursorScreenPos(ImVec2(fit.min.x - dp(10.0f), fit.min.y - dp(10.0f)));
        ImGui::InvisibleButton("##entrada", ImVec2(fit.size.x + dp(20.0f), fit.size.y + dp(20.0f)));
        ownArrowKeys();
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 a = fit.min;
        const ImVec2 b(a.x + fit.size.x, a.y + fit.size.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Canvas ao vivo
        const float s = canvas.uvMax();
        const float hx = 0.5f / static_cast<float>(cw), hy = 0.5f / static_cast<float>(ch);
        dl->AddImage(static_cast<ImTextureID>(canvas.texture()), a, b, ImVec2(hx, s - hy), ImVec2(s - hx, hy));
        dl->AddRect(a, b, color(theme::kBorder));
        qa::noteRect("entrada:imagem", a.x, a.y, b.x, b.y);

        auto rectMin = [&](const RectI& r) { return ImVec2(a.x + static_cast<float>(r.x) * fit.scale, a.y + static_cast<float>(r.y) * fit.scale); };
        auto rectMax = [&](const RectI& r) { return ImVec2(a.x + static_cast<float>(r.x + r.w) * fit.scale, a.y + static_cast<float>(r.y + r.h) * fit.scale); };

        MappingSelection& sel = ctx.mapping;
        Slice* selected = selectedSlice(ctx);
        const ImVec2 mouse = ImGui::GetMousePos();

        // --- Interação ---
        if (ImGui::IsItemActivated()) {
            g_inDrag = {};
            bool grabbed = false;
            if (selected) {
                const int h = hitHandle(rectMin(selected->input), rectMax(selected->input), mouse);
                if (h >= 0) {
                    g_inDrag = {DragMode::Resize, h, mouse, selected->input};
                    grabbed = true;
                }
            }
            if (!grabbed) {
                int hit = -1;
                auto inside = [&](int idx) {
                    const RectI& r = screen->slices[static_cast<size_t>(idx)].input;
                    const ImVec2 lo = rectMin(r), hi = rectMax(r);
                    return mouse.x >= lo.x && mouse.x <= hi.x && mouse.y >= lo.y && mouse.y <= hi.y;
                };
                if (selected && sel.slice >= 0 && inside(sel.slice))
                    hit = sel.slice;
                for (int j = static_cast<int>(screen->slices.size()) - 1; hit < 0 && j >= 0; --j)
                    if (inside(j))
                        hit = j;
                if (hit >= 0) {
                    sel.slice = hit;
                    sel.corner = -1;
                    selected = &screen->slices[static_cast<size_t>(hit)];
                    g_inDrag = {DragMode::Move, -1, mouse, selected->input};
                }
            }
        }
        if (active && g_inDrag.mode != DragMode::None && selected) {
            const int dx = static_cast<int>(std::lround((mouse.x - g_inDrag.startMouse.x) / fit.scale));
            const int dy = static_cast<int>(std::lround((mouse.y - g_inDrag.startMouse.y) / fit.scale));
            const RectI& r0 = g_inDrag.startRect;
            RectI r = r0;
            if (g_inDrag.mode == DragMode::Move) {
                r.x = std::clamp(r0.x + dx, 0, cw - r0.w);
                r.y = std::clamp(r0.y + dy, 0, ch - r0.h);
            } else {
                int x0 = r0.x, x1 = r0.x + r0.w, y0 = r0.y, y1 = r0.y + r0.h;
                if (handleLeft(g_inDrag.handle)) x0 = std::clamp(r0.x + dx, 0, x1 - 1);
                if (handleRight(g_inDrag.handle)) x1 = std::clamp(r0.x + r0.w + dx, x0 + 1, cw);
                if (handleTop(g_inDrag.handle)) y0 = std::clamp(r0.y + dy, 0, y1 - 1);
                if (handleBottom(g_inDrag.handle)) y1 = std::clamp(r0.y + r0.h + dy, y0 + 1, ch);
                r = {x0, y0, x1 - x0, y1 - y0};
            }
            selected->input = r;
        }
        if (!active)
            g_inDrag.mode = DragMode::None;

        // Cursor de redimensionar nas alças
        if (selected && (hovered || active)) {
            const int h = active && g_inDrag.mode == DragMode::Resize ? g_inDrag.handle
                          : hitHandle(rectMin(selected->input), rectMax(selected->input), mouse);
            if (h >= 0)
                ImGui::SetMouseCursor(handleCursor(h));
        }

        // Setas movem o retângulo (1 px; Shift = 10 px)
        if (selected && (hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
            int dx = 0, dy = 0;
            if (arrowDelta(dx, dy)) {
                selected->input.x += dx;
                selected->input.y += dy;
                clampRectToCanvas(selected->input, cw, ch);
            }
        }

        // --- Desenho: cinza para os demais, vermelho para o selecionado ---
        ImFont* body = fonts().body;
        const float line1 = std::max(1.0f, dp(1.5f));
        const float line2 = std::max(1.0f, dp(2.0f));
        for (int j = 0; j < static_cast<int>(screen->slices.size()); ++j) {
            const Slice& sl = screen->slices[static_cast<size_t>(j)];
            if (j == sel.slice)
                continue;
            const ImVec2 lo = rectMin(sl.input), hi = rectMax(sl.input);
            dl->AddRectFilled(lo, hi, color(theme::kText, 0.05f));
            dl->AddRect(lo, hi, color(theme::kTextDim, sl.enabled ? 0.9f : 0.4f), 0.0f, 0, line1);
            dl->PushClipRect(lo, hi, true);
            dl->AddText(body, fontPx(body), ImVec2(lo.x + dp(6.0f), lo.y + dp(4.0f)), color(theme::kText, 0.8f),
                        sl.name.c_str());
            dl->PopClipRect();
        }
        if (selected) {
            const ImVec2 lo = rectMin(selected->input), hi = rectMax(selected->input);
            dl->AddRectFilled(lo, hi, color(theme::kAccent, 0.14f));
            dl->AddRect(lo, hi, color(theme::kAccent), 0.0f, 0, line2);
            dl->PushClipRect(lo, hi, true);
            dl->AddText(body, fontPx(body), ImVec2(lo.x + dp(6.0f), lo.y + dp(4.0f)), color(theme::kText), selected->name.c_str());
            dl->PopClipRect();
            for (int h = 0; h < 8; ++h) {
                const ImVec2 p = handlePos(lo, hi, h);
                const float hs = dp(4.0f);
                dl->AddRectFilled(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), color(theme::kText));
                dl->AddRect(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), color(theme::kAccent), 0.0f, 0, line1);
            }
        }

        if (selected)
            caption("%s  |  entrada x=%d y=%d  %dx%d  |  canvas %dx%d", selected->name.c_str(), selected->input.x,
                    selected->input.y, selected->input.w, selected->input.h, cw, ch);
        else
            caption("canvas %dx%d", cw, ch);
    } else if (!screen) {
        ImGui::TextColored(theme::kTextDim, "Crie uma tela na Lista.");
    }

    endPanel();
    ImGui::PopStyleVar();
}

void drawMappingOutputPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kMapOutput);

    Screen* screen = selectedScreen(ctx);
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    FitRect fit;
    if (screen && fitImage(static_cast<float>(screen->width) / static_cast<float>(screen->height), dp(16.0f), lineH, fit,
                           screen->width)) {
        ImGui::SetCursorScreenPos(ImVec2(fit.min.x - dp(16.0f), fit.min.y - dp(16.0f)));
        ImGui::InvisibleButton("##saida", ImVec2(fit.size.x + dp(32.0f), fit.size.y + dp(32.0f)));
        ownArrowKeys();
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 a = fit.min;
        const ImVec2 b(a.x + fit.size.x, a.y + fit.size.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ScreenView view;
        if (ctx.outputRenderer.screenView(screen->id, view))
            dl->AddImage(static_cast<ImTextureID>(view.texture), a, b, ImVec2(0, 1), ImVec2(1, 0));
        else
            dl->AddRectFilled(a, b, color(theme::kBackground));
        dl->AddRect(a, b, color(theme::kBorder));
        qa::noteRect("saida:imagem", a.x, a.y, b.x, b.y);

        auto toScreen = [&](Point2 p) { return ImVec2(a.x + p.x * fit.scale, a.y + p.y * fit.scale); };
        auto toPixels = [&](ImVec2 m) { return Point2{(m.x - a.x) / fit.scale, (m.y - a.y) / fit.scale}; };

        MappingSelection& sel = ctx.mapping;
        Slice* selected = selectedSlice(ctx);
        const ImVec2 mouse = ImGui::GetMousePos();

        auto hitCorner = [&](const Slice& sl) {
            int best = -1;
            float bestDist = handleRadius() + dp(4.0f);
            for (int c = 0; c < 4; ++c) {
                const float d = distance(toScreen(sl.corners[c]), mouse);
                if (d < bestDist) {
                    bestDist = d;
                    best = c;
                }
            }
            return best;
        };

        if (ImGui::IsItemActivated()) {
            g_outDrag = {};
            bool grabbed = false;
            if (selected) {
                const int c = hitCorner(*selected);
                if (c >= 0) {
                    sel.corner = c;
                    g_outDrag.mode = QuadDrag::Corner;
                    grabbed = true;
                } else if (pointInQuad(selected->corners, toPixels(mouse))) {
                    g_outDrag.mode = QuadDrag::Move;
                    grabbed = true;
                }
            }
            if (!grabbed) {
                for (int j = static_cast<int>(screen->slices.size()) - 1; j >= 0; --j) {
                    Slice& sl = screen->slices[static_cast<size_t>(j)];
                    if (pointInQuad(sl.corners, toPixels(mouse))) {
                        sel.slice = j;
                        sel.corner = -1;
                        selected = &sl;
                        g_outDrag.mode = QuadDrag::Move;
                        break;
                    }
                }
            }
            if (selected) {
                g_outDrag.startMouse = mouse;
                std::copy(selected->corners, selected->corners + 4, g_outDrag.startCorners);
            }
        }
        if (active && selected && g_outDrag.mode != QuadDrag::None) {
            if (g_outDrag.mode == QuadDrag::Corner && sel.corner >= 0) {
                Point2 p = toPixels(mouse);
                p.x = std::round(p.x);
                p.y = std::round(p.y);
                clampCorner(p, *screen);
                selected->corners[sel.corner] = p;
            } else if (g_outDrag.mode == QuadDrag::Move) {
                const float dx = std::round((mouse.x - g_outDrag.startMouse.x) / fit.scale);
                const float dy = std::round((mouse.y - g_outDrag.startMouse.y) / fit.scale);
                for (int c = 0; c < 4; ++c) {
                    Point2 p{g_outDrag.startCorners[c].x + dx, g_outDrag.startCorners[c].y + dy};
                    clampCorner(p, *screen);
                    selected->corners[c] = p;
                }
            }
        }
        if (!active)
            g_outDrag.mode = QuadDrag::None;

        if (selected && (hovered || active) && hitCorner(*selected) >= 0)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Setas: o canto selecionado, ou o quad inteiro se nenhum canto estiver selecionado
        if (selected && (hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
            int dx = 0, dy = 0;
            if (arrowDelta(dx, dy)) {
                for (int c = 0; c < 4; ++c) {
                    if (sel.corner >= 0 && c != sel.corner)
                        continue;
                    selected->corners[c].x += static_cast<float>(dx);
                    selected->corners[c].y += static_cast<float>(dy);
                    clampCorner(selected->corners[c], *screen);
                }
            }
        }

        // Quads: cinza nos demais, vermelho no selecionado
        for (int j = 0; j < static_cast<int>(screen->slices.size()); ++j) {
            if (j == sel.slice)
                continue;
            const Slice& sl = screen->slices[static_cast<size_t>(j)];
            ImVec2 pts[4];
            for (int c = 0; c < 4; ++c)
                pts[c] = toScreen(sl.corners[c]);
            dl->AddPolyline(pts, 4, color(theme::kTextDim, sl.enabled ? 0.9f : 0.4f), ImDrawFlags_Closed,
                            std::max(1.0f, dp(1.5f)));
            ImFont* body = fonts().body;
            dl->AddText(body, fontPx(body), ImVec2(pts[0].x + dp(6.0f), pts[0].y + dp(4.0f)), color(theme::kText, 0.8f),
                        sl.name.c_str());
        }
        if (selected) {
            ImVec2 pts[4];
            for (int c = 0; c < 4; ++c)
                pts[c] = toScreen(selected->corners[c]);
            dl->AddPolyline(pts, 4, color(theme::kAccent), ImDrawFlags_Closed, std::max(1.0f, dp(2.0f)));
            for (int c = 0; c < 4; ++c) {
                const bool on = sel.corner == c;
                dl->AddCircleFilled(pts[c], dp(6.0f), on ? color(theme::kAccent) : color(theme::kText));
                dl->AddCircle(pts[c], dp(6.0f), color(theme::kAccent), 0, std::max(1.0f, dp(1.5f)));
            }
        }

        const char* tag = screen->kind == OutputKind::Monitor ? screen->monitorName.c_str() : outputKindName(screen->kind);
        caption("%s  |  %dx%d  |  %s", screen->name.c_str(), screen->width, screen->height, tag);
    } else if (!screen) {
        ImGui::TextColored(theme::kTextDim, "Crie uma tela na Lista.");
    }

    endPanel();
    ImGui::PopStyleVar();
}

void drawMappingPropertiesPanel(PanelContext& ctx)
{
    beginPanel(panel::kMapProps);

    Screen* screen = selectedScreen(ctx);
    if (!screen) {
        ImGui::TextColored(theme::kTextDim, "Nada selecionado");
        endPanel();
        return;
    }

    ImGui::PushID(screen->id);
    screenProperties(ctx, *screen);
    if (Slice* slice = selectedSlice(ctx)) {
        ImGui::PushID(ctx.mapping.slice);
        sliceProperties(ctx, *screen, *slice);
        ImGui::PopID();
    } else {
        ImGui::Spacing();
        ImGui::TextColored(theme::kTextDim, "Selecione um slice na Lista para editar.");
    }
    ImGui::PopID();

    endPanel();
}

} // namespace bsl::ui
