#include "ui/panels/MappingPanels.h"

#include "app/Log.h"
#include "app/Qa.h"
#include "output/MapEdit.h"
#include "output/OutputRenderer.h"
#include "render/Compositor.h"
#include "render/ZoneMasks.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ndi/NdiRuntime.h"
#include "ui/panels/MapView.h"
#include "ui/panels/VideoCompat.h"
#include "ui/panels/ZoneRouting.h"

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
    case OutputKind::Ndi: return "NDI";
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
        if (ImGui::Selectable("NDI", screen.kind == OutputKind::Ndi))
            screen.kind = OutputKind::Ndi;
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

    if (screen.kind == OutputKind::Ndi) {
        if (!ndi::Runtime::instance().available()) {
            drawNdiRuntimeNotice();
        } else {
            dim("Nome do sender NDI");
            ImGui::PushID("ndiNome");
            inputText("##ndi", screen.ndiName);
            ImGui::PopID();
            dim("Formato");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (beginCombo("##ndiFormato", screen.ndiBgra ? "BGRA (cheio, com alpha)" : "UYVY (leve)")) {
                if (ImGui::Selectable("UYVY (leve)", !screen.ndiBgra))
                    screen.ndiBgra = false;
                if (ImGui::Selectable("BGRA (cheio, com alpha)", screen.ndiBgra))
                    screen.ndiBgra = true;
                ImGui::EndCombo();
            }
            const int conns = ctx.outputRenderer.ndiConnections(screen.id);
            if (conns >= 0)
                dim((std::to_string(conns) + " receptor(es) conectado(s)").c_str());
        }
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

void sourceProperties(PanelContext& ctx, Slice& slice)
{
    const Composition& comp = ctx.composition;
    ImGui::SeparatorText("Fonte");
    dim("Fonte da imagem");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##sliceFonte", sliceSourceLabel(comp, slice).c_str())) {
        if (ImGui::Selectable("Composição##fonte_comp", slice.source == kSliceFromComposition))
            setSliceSource(comp, slice, -1);
        for (int l = 0; l < comp.layerCount(); ++l) {
            const bool selected = slice.source == kSliceFromLayer && slice.layerId == comp.layers[static_cast<size_t>(l)].id;
            if (ImGui::Selectable((zoneSourceLabel(comp, l) + "##fonte_camada_" + std::to_string(l + 1)).c_str(), selected))
                setSliceSource(comp, slice, l);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Composição: o canvas inteiro, como sempre. Camada: só aquela camada (com os efeitos do clip e "
                          "da camada), sem as outras camadas, para que efeitos de uma não afetem as demais");
    if (slice.source != kSliceFromLayer)
        return;

    ImGui::Checkbox("Incluir efeitos do master##sliceMaster", &slice.includeMaster);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Desligado (padrão): o slice mostra só a camada, sem os efeitos e o fade do master. Ligado: aplica "
                          "também o master a esta camada isolada");
    ImGui::Checkbox("Aplicar transform da camada##sliceTransform", &slice.applyLayerTransform);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Desligado (padrão): o slice recebe a camada sem posição, tamanho, rotação nem recorte, porque ele "
                          "mesmo faz o enquadramento (entrada e quad de saída). Ligado: usa também o Transform da camada");
    const int li = comp.findLayerById(slice.layerId);
    ImGui::PushTextWrapPos(0.0f);
    if (li < 0) {
        ImGui::TextColored(theme::kWarning, "A camada vinculada não existe mais: este slice mostra preto. Escolha outra "
                                            "fonte acima.");
    } else if (comp.layers[static_cast<size_t>(li)].bypass) {
        ImGui::TextColored(theme::kWarning, "A camada está em bypass: este slice mostra preto.");
    } else if (!comp.layers[static_cast<size_t>(li)].sliceOnly) {
        dim("Esta camada também aparece no canvas principal. Use o ícone de link no cabeçalho dela (modo Composição) para "
            "deixá-la só nos slices vinculados.");
    }
    ImGui::PopTextWrapPos();
}

void zoneProperties(PanelContext& ctx, Slice& slice)
{
    ImGui::SeparatorText("Profundidade (Z-Slice)");
    dim("Zona de profundidade");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##zFilter", zoneFilterName(slice.zFilter))) {
        for (int z = 0; z < kZoneFilterCount; ++z)
            if (ImGui::Selectable((std::string(zoneFilterName(z)) + "##zona_" + std::to_string(z)).c_str(), slice.zFilter == z))
                slice.zFilter = z;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Todas: o slice mostra a imagem inteira. Frente, Meio, Fundo, Zona 4: só os pixels dessa zona de "
                          "profundidade, calculada pelo efeito Z-Slice da camada ou do master");
    if (slice.zFilter != kZoneAll) {
        dim("Fonte das zonas");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (beginCombo("##zSource", zoneSourceLabel(ctx.composition, slice.zSource).c_str())) {
            if (ImGui::Selectable("Master##zfonte_master", slice.zSource == kZoneSourceMaster))
                slice.zSource = kZoneSourceMaster;
            for (int l = 0; l < ctx.composition.layerCount(); ++l)
                if (ImGui::Selectable((zoneSourceLabel(ctx.composition, l) + "##zfonte_" + std::to_string(l)).c_str(), slice.zSource == l))
                    slice.zSource = l;
            ImGui::EndCombo();
        }
        if (!ZoneMasks::instance().available(slice.zSource)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(theme::kWarning, "Sem Z-Slice ativo em %s: este slice mostra tudo. Adicione o efeito Z-Slice "
                                                "na cadeia dessa fonte (e use a IA fora do perfil Eco).",
                               zoneSourceLabel(ctx.composition, slice.zSource).c_str());
            ImGui::PopTextWrapPos();
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
    sourceProperties(ctx, slice);
    zoneProperties(ctx, slice);
    ImGui::SeparatorText("Posição");

    ImGui::Spacing();
    dim("Entrada no canvas (px)");
    if (ImGui::BeginTable("##entrada", 4, ImGuiTableFlags_SizingStretchSame)) {
        tableHeader4("X", "Y", "L", "A");
        ImGui::TableNextRow();
        RectI& r = slice.input;
        ImGui::TableSetColumnIndex(0);
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
    if (!ctx.mapping.pointsMode) {
        dim("Saída: posição, tamanho e rotação (px, graus)");
        mapedit::Xform xf;
        bool exact = true;
        {
            Point2 c[4] = {slice.corners[0], slice.corners[1], slice.corners[2], slice.corners[3]};
            exact = mapedit::fromCorners(c, xf);
        }
        float px = xf.x(), py = xf.y(), pw = xf.w, ph = xf.h;
        float deg = mapedit::radToDeg(xf.rot);
        bool changed = false;
        if (ImGui::BeginTable("##saidaxf", 4, ImGuiTableFlags_SizingStretchSame)) {
            tableHeader4("X", "Y", "L", "A");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            changed |= inputFloat("##ox", px, -1.0e5f, 1.0e5f, "%.1f", 1.0f);
            ImGui::TableSetColumnIndex(1);
            changed |= inputFloat("##oy", py, -1.0e5f, 1.0e5f, "%.1f", 1.0f);
            ImGui::TableSetColumnIndex(2);
            changed |= inputFloat("##ow", pw, 1.0f, 1.0e5f, "%.1f", 1.0f);
            ImGui::TableSetColumnIndex(3);
            changed |= inputFloat("##oh", ph, 1.0f, 1.0e5f, "%.1f", 1.0f);
            ImGui::EndTable();
        }
        dim("Rotação");
        changed |= inputFloat("##orot", deg, -360.0f, 360.0f, "%.1f", 0.5f, 0.0f);
        if (changed) {
            xf.w = pw;
            xf.h = ph;
            xf.setPosition(px, py);
            xf.rot = mapedit::degToRad(deg);
            Point2 out[4];
            mapedit::toCorners(xf, out);
            for (int i = 0; i < 4; ++i)
                slice.corners[i] = out[i];
        }
        if (!exact)
            ImGui::TextColored(theme::kWarning, "Quad deformado: mostrando a caixa dos cantos");
    } else {
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

struct RenameState
{
    int screen = -1;
    int slice = -2;
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

}

bool sliceLayerMissing(const Composition& comp, const Slice& slice)
{
    return slice.source == kSliceFromLayer && comp.findLayerById(slice.layerId) < 0;
}

std::string sliceSourceLabel(const Composition& comp, const Slice& slice)
{
    if (slice.source != kSliceFromLayer)
        return "Composição";
    const int li = comp.findLayerById(slice.layerId);
    return li < 0 ? "Camada removida" : zoneSourceLabel(comp, li);
}

std::string sliceLayerName(const Composition& comp, const Slice& slice)
{
    if (slice.source != kSliceFromLayer)
        return {};
    const int li = comp.findLayerById(slice.layerId);
    if (li < 0)
        return "Camada removida";
    const std::string& name = comp.layers[static_cast<size_t>(li)].name;
    return name.empty() ? "Camada " + std::to_string(li + 1) : name;
}

void setSliceSource(const Composition& comp, Slice& slice, int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= comp.layerCount()) {
        slice.source = kSliceFromComposition;
        slice.layerId = 0;
        slice.includeMaster = false;
        slice.applyLayerTransform = false;
        return;
    }
    slice.source = kSliceFromLayer;
    slice.layerId = comp.layers[static_cast<size_t>(layerIndex)].id;
}

void validateMappingSelection(PanelContext& ctx)
{
    MappingSelection& m = ctx.mapping;
    const int screens = static_cast<int>(ctx.output.screens.size());
    if (screens == 0) {
        m.screen = m.slice = m.corner = -1;
        m.multi.clear();
        m.multiScreen = -1;
        return;
    }
    m.screen = std::clamp(m.screen, 0, screens - 1);
    const int slices = static_cast<int>(ctx.output.screens[static_cast<size_t>(m.screen)].slices.size());
    m.slice = slices == 0 ? -1 : std::clamp(m.slice, -1, slices - 1);
    if (m.slice < 0)
        m.corner = -1;
    normalizeSelection(m, slices);
}

void drawMappingListPanel(PanelContext& ctx)
{
    beginPanel(panel::kMapList);

    OutputSetup& out = ctx.output;
    MappingSelection& sel = ctx.mapping;
    const int cw = ctx.composition.width, ch = ctx.composition.height;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 iconSize((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) * 0.25f, ImGui::GetFrameHeight());
    if (iconButton(ICON_PLUS ICON_MONITOR, "##map_add_tela", "Nova tela", iconSize)) {
        sel.screen = out.addScreen(cw, ch);
        selectOnly(sel, 0);
    }
    ImGui::BeginDisabled(!selectedScreen(ctx));
    ImGui::SameLine();
    if (iconButton(ICON_SQUARE_PLUS, "##map_add_slice", "Novo slice na tela selecionada", iconSize)) {
        selectOnly(sel, out.addSlice(sel.screen, cw, ch));
    }
    ImGui::SameLine();
    if (iconButton(ICON_COPY, "##map_duplicar",
                   "Duplicar os slices selecionados (Ctrl+D) ou a tela, se nenhum slice estiver selecionado", iconSize)) {
        if (sel.slice >= 0)
            mapDuplicateSelection(ctx);
        else
            sel.screen = out.duplicateScreen(sel.screen);
        sel.corner = -1;
    }
    ImGui::SameLine();
    if (iconButton(ICON_TRASH, "##map_remover",
                   "Remover os slices selecionados (Delete) ou a tela, se nenhum slice estiver selecionado", iconSize)) {
        if (sel.slice >= 0)
            mapRemoveSelection(ctx);
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
                if (sel.screen == i && isSliceSelected(sel, j))
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
                        const ImGuiIO& io = ImGui::GetIO();
                        if (io.KeyCtrl && sel.screen == i) {
                            toggleSliceSelection(sel, j);
                        } else {
                            sel.screen = i;
                            selectOnly(sel, j);
                        }
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                        beginRename(i, j, slice.name);
                    float tagRight = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - dp(2.0f);
                    if (slice.source == kSliceFromLayer) {
                        const bool gone = sliceLayerMissing(ctx.composition, slice);
                        const std::string layerTag = std::string(ICON_LINK " ") + sliceLayerName(ctx.composition, slice);
                        const float tw = ImGui::CalcTextSize(layerTag.c_str()).x;
                        ImGui::SameLine(tagRight - tw);
                        ImGui::TextColored(gone ? theme::kWarning : theme::kAccent, "%s", layerTag.c_str());
                        if (ImGui::IsItemHovered()) {
                            if (gone)
                                ImGui::SetTooltip("A camada vinculada não existe mais: este slice mostra preto");
                            else
                                ImGui::SetTooltip("Fonte: só a camada \"%s\"%s", sliceLayerName(ctx.composition, slice).c_str(),
                                                  slice.includeMaster ? ", com os efeitos do master" : "");
                        }
                        tagRight -= tw + dp(8.0f);
                    }
                    if (slice.zFilter != kZoneAll) {
                        const bool live = ZoneMasks::instance().available(slice.zSource);
                        const char* tag = zoneFilterName(slice.zFilter);
                        const float tw = ImGui::CalcTextSize(tag).x;
                        ImGui::SameLine(tagRight - tw);
                        ImGui::TextColored(live ? theme::kAccent : theme::kWarning, "%s", tag);
                        if (!live && ImGui::IsItemHovered())
                            ImGui::SetTooltip("Sem Z-Slice ativo em %s: este slice mostra tudo",
                                              zoneSourceLabel(ctx.composition, slice.zSource).c_str());
                    }
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
    drawMapView(ctx, MapViewKind::Input);
}

void drawMappingOutputPanel(PanelContext& ctx)
{
    drawMapView(ctx, MapViewKind::Output);
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

}
