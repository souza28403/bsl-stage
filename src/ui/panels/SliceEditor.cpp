#include "ui/panels/SliceEditor.h"

#include "fx/bsl/SliceFeed.h"
#include "fx/bsl/SliceState.h"
#include "net/MiniHttp.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <set>

namespace bsl::ui {

namespace {

using fx::bsllight::FeedSlice;
using fx::bsllight::SliceGroup;
using fx::bsllight::SliceMode;
using fx::bsllight::SliceState;

constexpr ImU32 kGroupColors[fx::bsllight::kSliceGroups] = {
    IM_COL32(0xe8, 0x34, 0x3a, 255), IM_COL32(0xff, 0x8a, 0x1f, 255), IM_COL32(0xff, 0xd2, 0x3f, 255), IM_COL32(0x3d, 0xdc, 0x84, 255),
    IM_COL32(0x27, 0xd3, 0xe6, 255), IM_COL32(0x4a, 0x7d, 0xff, 255), IM_COL32(0xa6, 0x6b, 0xff, 255), IM_COL32(0xff, 0x5f, 0xb7, 255)};

struct EditorUi
{
    std::set<std::string> selected;
    int group = 0;
    bool dragging = false;
    ImVec2 dragStart{};
    bool openedFx = false;
};
EditorUi g_ui;

struct SliceView
{
    std::vector<FeedSlice> slices;
    int compW = 1920, compH = 1080;
};

struct XmlCache
{
    std::string path;
    std::filesystem::file_time_type mtime{};
    std::chrono::steady_clock::time_point checked{};
    SliceView view;
};
XmlCache g_xml;

SliceView currentSlices(const media::ParamSet& params)
{
    SliceView v;
    if (params.s("source") == "xml") {
        std::string path = params.s("xmlFile");
        const auto now = std::chrono::steady_clock::now();
        if (path != g_xml.path || now - g_xml.checked > std::chrono::seconds(2)) {
            g_xml.checked = now;
            const std::filesystem::path fp{std::u8string(path.begin(), path.end())};
            std::error_code ec;
            const auto mtime = std::filesystem::last_write_time(fp, ec);
            if (path != g_xml.path || (!ec && mtime != g_xml.mtime)) {
                g_xml.path = path;
                g_xml.mtime = ec ? std::filesystem::file_time_type{} : mtime;
                g_xml.view = SliceView();
                std::string text;
                if (net::readWholeFile(fp, text, 64u << 20)) {
                    int cw = 0, ch = 0;
                    g_xml.view.slices = fx::bsllight::parseSliceXml(text, &cw, &ch);
                    g_xml.view.compW = cw > 0 ? cw : 1920;
                    g_xml.view.compH = ch > 0 ? ch : 1080;
                }
            }
        }
        return g_xml.view;
    }
    fx::bsllight::SliceFeed& feed = fx::bsllight::SliceFeed::instance();
    v.slices = feed.slices();
    v.compW = std::max(16, feed.canvasWidth());
    v.compH = std::max(16, feed.canvasHeight());
    return v;
}

float labelWidth()
{
    return std::clamp(ImGui::GetContentRegionAvail().x * 0.38f, dp(96.0f), dp(170.0f));
}

void rowLabel(const char* text)
{
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    dimLabel(text);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x + labelWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool numRow(const char* label, const char* id, float& v, float lo, float hi, const char* format, const char* unit, float def,
            bool enabled = true)
{
    ImGui::BeginDisabled(!enabled);
    rowLabel(label);
    const float step = (hi - lo) / 200.0f;
    const bool changed = hotNumber(id, v, lo, hi, step, format, unit, def, -1.0f);
    ImGui::EndDisabled();
    return changed;
}

bool checkRow(const char* label, const char* id, bool& v, bool enabled = true)
{
    ImGui::BeginDisabled(!enabled);
    rowLabel(label);
    const bool changed = ImGui::Checkbox(id, &v);
    ImGui::EndDisabled();
    return changed;
}

bool choiceRow(const char* label, const char* id, int& value, const std::vector<const char*>& names)
{
    rowLabel(label);
    bool changed = false;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - gap * static_cast<float>(names.size() - 1)) / static_cast<float>(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0)
            ImGui::SameLine();
        const bool on = static_cast<int>(i) == value;
        ImGui::PushID(static_cast<int>(i));
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccent);
        }
        const std::string btn = std::string(names[i]) + "##" + id;
        if (ImGui::Button(btn.c_str(), ImVec2(w, 0.0f)) && !on) {
            value = static_cast<int>(i);
            changed = true;
        }
        if (on)
            ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    return changed;
}

bool colorRow(const char* label, const char* id, float rgb[3])
{
    rowLabel(label);
    return ImGui::ColorEdit3(id, rgb, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB);
}

void sliceCorners(const FeedSlice& s, ImVec2 out[4])
{
    const float a = s.angle * 3.14159265f / 180.0f;
    const float c = std::cos(a), si = std::sin(a);
    const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        const float lx = sx[i] * s.hw, ly = sy[i] * s.hh;
        out[i] = ImVec2(s.cx + lx * c - ly * si, s.cy + lx * si + ly * c);
    }
}

bool pointInQuad(const ImVec2 q[4], ImVec2 p)
{
    bool inside = false;
    for (int i = 0, j = 3; i < 4; j = i++) {
        if (((q[i].y > p.y) != (q[j].y > p.y)) && (p.x < (q[j].x - q[i].x) * (p.y - q[i].y) / (q[j].y - q[i].y) + q[i].x))
            inside = !inside;
    }
    return inside;
}

void drawPreview(const SliceView& view, SliceState& state, bool& changed)
{
    (void)changed;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float aspect = static_cast<float>(view.compH) / static_cast<float>(view.compW);
    const float h = std::clamp(availW * aspect, dp(60.0f), dp(230.0f));
    const float w = std::min(availW, h / std::max(aspect, 0.01f));
    ImGui::InvisibleButton("##slicePreview", ImVec2(w, h));
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ImGui::GetColorU32(theme::kField), rounding());
    dl->AddRect(a, b, ImGui::GetColorU32(theme::kBorder), rounding());
    const float kx = (b.x - a.x) / static_cast<float>(view.compW), ky = (b.y - a.y) / static_cast<float>(view.compH);
    auto toScreen = [&](ImVec2 p) { return ImVec2(a.x + p.x * kx, a.y + p.y * ky); };

    dl->PushClipRect(a, b, true);
    std::vector<std::array<ImVec2, 4>> quads(view.slices.size());
    for (size_t i = 0; i < view.slices.size(); ++i) {
        const FeedSlice& s = view.slices[i];
        ImVec2 q[4];
        sliceCorners(s, q);
        for (int k = 0; k < 4; ++k)
            quads[i][static_cast<size_t>(k)] = q[k];
        ImVec2 sq[4];
        for (int k = 0; k < 4; ++k)
            sq[k] = toScreen(q[k]);
        const auto it = state.assign.find(s.id);
        const bool assigned = it != state.assign.end();
        const ImU32 base = assigned ? kGroupColors[it->second] : IM_COL32(150, 150, 150, 255);
        const bool sel = g_ui.selected.count(s.id) > 0;
        const ImU32 fill = (base & 0x00FFFFFF) | (static_cast<ImU32>(sel ? 120 : (assigned ? 70 : 30)) << 24);
        if (s.angle == 0.0f) {
            float round = 0.0f;
            if (assigned) {
                const SliceGroup& g = state.groups[it->second];
                round = g.masterSize * g.corners[0] * std::min(s.hw * kx, s.hh * ky);
            }
            dl->AddRectFilled(sq[0], sq[2], fill, round);
            dl->AddRect(sq[0], sq[2], base, round, 0, sel ? dp(2.5f) : dp(1.2f));
        } else {
            dl->AddConvexPolyFilled(sq, 4, fill);
            dl->AddPolyline(sq, 4, base, ImDrawFlags_Closed, sel ? dp(2.5f) : dp(1.2f));
        }
        if (sel)
            dl->AddRect(ImVec2(std::min({sq[0].x, sq[1].x, sq[2].x, sq[3].x}) - 1.0f, std::min({sq[0].y, sq[1].y, sq[2].y, sq[3].y}) - 1.0f),
                        ImVec2(std::max({sq[0].x, sq[1].x, sq[2].x, sq[3].x}) + 1.0f, std::max({sq[0].y, sq[1].y, sq[2].y, sq[3].y}) + 1.0f),
                        IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.0f);
    }
    if (view.slices.empty())
        drawCenteredMono(dl, a, b, "nenhum slice", ImGui::GetColorU32(theme::kTextDim));

    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActivated()) {
        g_ui.dragging = true;
        g_ui.dragStart = io.MousePos;
    }
    if (g_ui.dragging && ImGui::IsItemActive()) {
        const ImVec2 d(io.MousePos.x - g_ui.dragStart.x, io.MousePos.y - g_ui.dragStart.y);
        if (std::fabs(d.x) > 4.0f || std::fabs(d.y) > 4.0f)
            dl->AddRect(g_ui.dragStart, io.MousePos, IM_COL32(255, 255, 255, 220), 0.0f, 0, 1.0f);
    }
    if (g_ui.dragging && ImGui::IsItemDeactivated()) {
        g_ui.dragging = false;
        const ImVec2 d(io.MousePos.x - g_ui.dragStart.x, io.MousePos.y - g_ui.dragStart.y);
        std::set<std::string> hit;
        if (std::fabs(d.x) > 4.0f || std::fabs(d.y) > 4.0f) {
            const ImVec2 lo(std::min(g_ui.dragStart.x, io.MousePos.x), std::min(g_ui.dragStart.y, io.MousePos.y));
            const ImVec2 hi(std::max(g_ui.dragStart.x, io.MousePos.x), std::max(g_ui.dragStart.y, io.MousePos.y));
            for (size_t i = 0; i < view.slices.size(); ++i) {
                float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
                for (const ImVec2& p : quads[i]) {
                    const ImVec2 sp = toScreen(p);
                    x0 = std::min(x0, sp.x);
                    y0 = std::min(y0, sp.y);
                    x1 = std::max(x1, sp.x);
                    y1 = std::max(y1, sp.y);
                }
                if (x1 >= lo.x && x0 <= hi.x && y1 >= lo.y && y0 <= hi.y)
                    hit.insert(view.slices[i].id);
            }
        } else {
            const ImVec2 cp((io.MousePos.x - a.x) / kx, (io.MousePos.y - a.y) / ky);
            for (size_t i = view.slices.size(); i-- > 0;) {
                ImVec2 q[4] = {quads[i][0], quads[i][1], quads[i][2], quads[i][3]};
                if (pointInQuad(q, cp)) {
                    hit.insert(view.slices[i].id);
                    break;
                }
            }
        }
        if (io.KeyShift) {
            for (const std::string& id : hit) {
                if (!g_ui.selected.erase(id))
                    g_ui.selected.insert(id);
            }
        } else {
            g_ui.selected = hit;
        }
    }
    dl->PopClipRect();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clique num slice para selecionar (Shift soma) ou arraste um retângulo para pegar vários");
}

void drawSliceList(const SliceView& view, SliceState& state, bool& changed)
{
    int used = 0;
    for (const FeedSlice& s : view.slices)
        if (state.assign.count(s.id))
            ++used;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::kTextDim, "%zu slices  ·  %d em uso", view.slices.size(), used);
    ImGui::SameLine();
    if (ImGui::SmallButton("Todos##sl_todos")) {
        g_ui.selected.clear();
        for (const FeedSlice& s : view.slices)
            g_ui.selected.insert(s.id);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Nenhum##sl_nenhum"))
        g_ui.selected.clear();

    for (auto it = g_ui.selected.begin(); it != g_ui.selected.end();) {
        const bool exists = std::any_of(view.slices.begin(), view.slices.end(), [&](const FeedSlice& s) { return s.id == *it; });
        it = exists ? std::next(it) : g_ui.selected.erase(it);
    }

    std::set<std::string> screenNames;
    for (const FeedSlice& s : view.slices)
        screenNames.insert(s.screen);
    const size_t rows = std::max<size_t>(view.slices.size() + screenNames.size(), 1);
    const float rowH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 0.5f;
    const float listH = std::min(dp(170.0f), rowH * static_cast<float>(rows) + ImGui::GetStyle().WindowPadding.y * 2.0f);
    if (ImGui::BeginChild("##sliceList", ImVec2(0.0f, listH), ImGuiChildFlags_Borders)) {
        std::string lastScreen = "\x01";
        for (size_t i = 0; i < view.slices.size(); ++i) {
            const FeedSlice& s = view.slices[i];
            if (s.screen != lastScreen) {
                lastScreen = s.screen;
                ImGui::TextColored(theme::kTextDim, "%s", s.screen.c_str());
            }
            ImGui::PushID(static_cast<int>(i));
            const bool sel = g_ui.selected.count(s.id) > 0;
            const std::string label = "   " + (s.sliceName.empty() ? s.name : s.sliceName) + "##row";
            if (ImGui::Selectable(label.c_str(), sel)) {
                if (!g_ui.selected.erase(s.id))
                    g_ui.selected.insert(s.id);
            }
            const auto it = state.assign.find(s.id);
            const char* badge = it != state.assign.end() ? state.groups[it->second].name.c_str() : "sem grupo";
            const float tw = ImGui::CalcTextSize(badge).x;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - tw - dp(8.0f));
            if (it != state.assign.end())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGroupColors[it->second]), "%d %s", it->second + 1, badge);
            else
                ImGui::TextColored(theme::kTextDim, "%s", badge);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::AlignTextToFramePadding();
    if (g_ui.selected.empty())
        ImGui::TextColored(theme::kTextDim, "Selecione slices para enviar a um grupo");
    else
        ImGui::Text("%zu selecionado(s): enviar para o grupo", g_ui.selected.size());
    ImGui::BeginDisabled(g_ui.selected.empty());
    for (int g = 0; g < fx::bsllight::kSliceGroups; ++g) {
        if (g > 0)
            ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4((kGroupColors[g] & 0x00FFFFFF) | 0x66000000));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4((kGroupColors[g] & 0x00FFFFFF) | 0xAA000000));
        const std::string id = std::to_string(g + 1) + "##enviar_grupo_" + std::to_string(g + 1);
        if (ImGui::Button(id.c_str(), ImVec2(dp(30.0f), 0.0f))) {
            for (const std::string& sid : g_ui.selected)
                state.assign[sid] = g;
            g_ui.group = g;
            changed = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Enviar para %s", state.groups[g].name.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Tirar do grupo##tirar_grupo")) {
        for (const std::string& sid : g_ui.selected)
            state.assign.erase(sid);
        changed = true;
    }
    ImGui::EndDisabled();
}

void copyStyle(SliceState& s, int from, int to)
{
    if (from == to)
        return;
    const std::string name = s.groups[to].name;
    s.groups[to] = s.groups[from];
    s.groups[to].name = name;
}

bool groupCombo(const char* label, const char* id, int current, int& picked)
{
    rowLabel(label);
    bool changed = false;
    if (beginCombo(id, "Escolher…")) {
        for (int i = 0; i < fx::bsllight::kSliceGroups; ++i) {
            if (i == current)
                continue;
            const std::string item = "Grupo " + std::to_string(i + 1);
            if (ImGui::Selectable(item.c_str())) {
                picked = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawGroupEditor(SliceGroup& g, int gi, SliceState& state, bool& changed)
{
    char buf[48];
    ImGui::PushID(gi);
    checkRow("Ativo", "##g_ativo", g.enabled) && (changed = true);

    rowLabel("Nome");
    std::snprintf(buf, sizeof(buf), "%s", g.name.c_str());
    if (ImGui::InputText("##g_nome", buf, 33)) {
        g.name = buf;
        changed = true;
    }
    int fill = g.fillVideo ? 0 : 1;
    if (choiceRow("Preencher", "g_fill", fill, {"Vídeo", "Só borda"})) {
        g.fillVideo = fill == 0;
        changed = true;
    }

    ImGui::SeparatorText("Formato");
    if (numRow("Arredondamento geral", "##g_master", g.masterSize, 0.0f, 1.0f, "%.2f", nullptr, 0.25f))
        changed = true;
    bool equal = g.equalCorners;
    if (checkRow("Cantos iguais", "##g_eq", equal)) {
        g.equalCorners = equal;
        if (equal)
            g.corners[1] = g.corners[2] = g.corners[3] = g.corners[0];
        changed = true;
    }
    if (g.equalCorners) {
        float c = g.corners[0];
        if (numRow("Cantos (todos)", "##g_c0", c, 0.0f, 1.0f, "%.2f", nullptr, 1.0f)) {
            g.corners[0] = g.corners[1] = g.corners[2] = g.corners[3] = c;
            changed = true;
        }
    } else {
        static const char* const names[4] = {"Canto TL", "Canto TR", "Canto BR", "Canto BL"};
        for (int k = 0; k < 4; ++k) {
            const std::string id = std::string("##g_c") + std::to_string(k);
            if (numRow(names[k], id.c_str(), g.corners[k], 0.0f, 1.0f, "%.2f", nullptr, 1.0f))
                changed = true;
        }
    }
    if (numRow("Offset (+ encolhe, − cresce)", "##g_off", g.offset, -200.0f, 200.0f, "%.0f", "px", 0.0f))
        changed = true;
    checkRow("Juntar slices", "##g_merge", g.merge) && (changed = true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Une os slices do grupo num formato só, sem linha no meio");
    if (numRow("Suavizar junção", "##g_msm", g.mergeSmooth, 0.0f, 100.0f, "%.0f", "px", 0.0f, g.merge))
        changed = true;
    if (numRow("Borda suave (vinheta)", "##g_soft", g.softEdge, 0.0f, 1.0f, "%.2f", nullptr, 0.0f))
        changed = true;

    ImGui::SeparatorText("Borda");
    if (numRow("Largura", "##g_w", g.strokeWidth, 0.0f, 100.0f, "%.0f", "px", 6.0f))
        changed = true;
    int mode = g.strokeEffectMode ? 1 : 0;
    if (choiceRow("Modo", "g_mode", mode, {"Cor", "Efeito"})) {
        g.strokeEffectMode = mode == 1;
        if (mode == 1)
            g_ui.openedFx = true;
        changed = true;
    }
    if (colorRow(g.strokeEffectMode ? "Cor A" : "Cor", "##g_ca", g.color))
        changed = true;
    if (g.strokeEffectMode && colorRow("Cor B", "##g_cb", g.colorB))
        changed = true;
    if (numRow("Glow", "##g_glow", g.glow, 0.0f, 1.0f, "%.2f", nullptr, 0.0f))
        changed = true;

    ImGui::SeparatorText("Efeito da borda");
    if (!g.strokeEffectMode)
        ImGui::TextColored(theme::kTextDim, "A borda está em modo Cor. Escolha um efeito para usar estes padrões.");
    {
        const int cols = 4;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float w = (ImGui::GetContentRegionAvail().x - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        for (int e = 0; e < fx::bsllight::kSliceEffects; ++e) {
            if (e % cols != 0)
                ImGui::SameLine();
            const bool on = g.strokeEffectMode && g.effect == e;
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccent);
            }
            const std::string label = std::string(fx::bsllight::kSliceEffectLabels[e]) + "##g_fx" + std::to_string(e);
            if (ImGui::Button(label.c_str(), ImVec2(w, 0.0f))) {
                g.effect = e;
                g.strokeEffectMode = true;
                changed = true;
            }
            if (on)
                ImGui::PopStyleColor(2);
        }
    }
    if (numRow("Velocidade", "##g_spd", g.speed, 0.0f, 2.0f, "%.2f", nullptr, 0.5f))
        changed = true;
    if (numRow("Variação", "##g_var", g.variation, 0.0f, 1.0f, "%.2f", nullptr, 0.5f))
        changed = true;
    checkRow("Cores suaves", "##g_smooth", g.smooth) && (changed = true);
    checkRow("Simetria X", "##g_symx", g.symX) && (changed = true);
    checkRow("Simetria Y", "##g_symy", g.symY) && (changed = true);
    checkRow("Borda dupla", "##g_dual", g.dualOn) && (changed = true);
    if (g.dualOn) {
        if (numRow("Largura da 2ª borda", "##g_dw", g.dualWidth, 0.0f, 100.0f, "%.0f", "px", 3.0f))
            changed = true;
        if (numRow("Distância", "##g_dg", g.dualGap, 0.0f, 200.0f, "%.0f", "px", 8.0f))
            changed = true;
    }

    ImGui::SeparatorText("Animação");
    {
        int bm = static_cast<int>(g.blinkMode);
        if (choiceRow("Piscar", "g_blink", bm, {"Off", "Velocidade", "BPM"})) {
            g.blinkMode = static_cast<SliceMode>(bm);
            changed = true;
        }
        if (g.blinkMode == SliceMode::Speed) {
            if (numRow("Taxa", "##g_br", g.blinkRate, 0.1f, 20.0f, "%.1f", "Hz", 2.0f))
                changed = true;
        } else if (g.blinkMode == SliceMode::Bpm) {
            if (numRow("Batidas por piscada", "##g_bb", g.blinkRate, 0.25f, 16.0f, "%.2f", nullptr, 2.0f))
                changed = true;
        }
        int bo = static_cast<int>(g.bounceMode);
        if (choiceRow("Bounce", "g_bounce", bo, {"Off", "Velocidade", "BPM"})) {
            g.bounceMode = static_cast<SliceMode>(bo);
            changed = true;
        }
        if (g.bounceMode == SliceMode::Speed) {
            if (numRow("Velocidade do bounce", "##g_bs", g.bounceSpeed, 0.1f, 8.0f, "%.1f", "Hz", 1.0f))
                changed = true;
        } else if (g.bounceMode == SliceMode::Bpm) {
            if (numRow("Batidas", "##g_bbeats", g.bounceBeats, 0.25f, 16.0f, "%.2f", nullptr, 1.0f))
                changed = true;
        }
        if (g.bounceMode != SliceMode::Off) {
            if (numRow("Mínimo", "##g_bmin", g.bounceMin, 0.0f, 1.0f, "%.2f", nullptr, 0.85f))
                changed = true;
            checkRow("Multi (fase própria por slice)", "##g_bmulti", g.bounceMulti) && (changed = true);
            checkRow("Suave", "##g_bsmooth", g.bounceSmooth) && (changed = true);
        }
        checkRow("Girar cor da borda", "##g_rot", g.rotOn) && (changed = true);
        if (g.rotOn) {
            if (numRow("Velocidade da cor", "##g_rs", g.rotSpeed, 0.0f, 2.0f, "%.2f", nullptr, 0.2f))
                changed = true;
            if (numRow("Compassos por volta", "##g_rb", g.rotBars, 0.0f, 16.0f, "%.0f", nullptr, 0.0f))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = usar a velocidade; acima de 0 = uma volta a cada N compassos (4 batidas)");
        }
    }

    ImGui::SeparatorText("Copiar estilo");
    int picked = -1;
    if (groupCombo("Copiar do grupo…", "##g_copyfrom", gi, picked)) {
        copyStyle(state, picked, gi);
        changed = true;
    }
    if (groupCombo("Copiar para o grupo…", "##g_copyto", gi, picked)) {
        copyStyle(state, gi, picked);
        changed = true;
    }
    ImGui::PopID();
}

}

bool drawSliceEditor(const media::ParamSet& params, const std::string& stateId, media::Json& out)
{
    SliceState state;
    fx::bsllight::sliceStateFromJson(params.raw(stateId), state);
    const SliceView view = currentSlices(params);
    bool changed = false;

    drawPreview(view, state, changed);
    drawSliceList(view, state, changed);

    ImGui::Spacing();
    g_ui.group = std::clamp(g_ui.group, 0, fx::bsllight::kSliceGroups - 1);
    int counts[fx::bsllight::kSliceGroups] = {};
    for (const FeedSlice& s : view.slices) {
        const auto it = state.assign.find(s.id);
        if (it != state.assign.end())
            ++counts[it->second];
    }
    for (int i = 0; i < fx::bsllight::kSliceGroups; ++i) {
        if (i > 0)
            ImGui::SameLine();
        const bool on = g_ui.group == i;
        const ImVec4 col = ImGui::ColorConvertU32ToFloat4(kGroupColors[i]);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? col : ImGui::ColorConvertU32ToFloat4((kGroupColors[i] & 0x00FFFFFF) | 0x40000000));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4((kGroupColors[i] & 0x00FFFFFF) | 0xAA000000));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.05f, 0.05f, 0.05f, 1.0f) : theme::kText);
        const std::string label = std::to_string(i + 1) + (counts[i] > 0 ? "·" + std::to_string(counts[i]) : std::string()) + "##ficha_grupo_" + std::to_string(i + 1);
        if (ImGui::Button(label.c_str(), ImVec2(dp(34.0f), 0.0f)))
            g_ui.group = i;
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", state.groups[i].name.c_str(), state.groups[i].enabled ? "" : " (desligado)");
    }
    ImGui::Spacing();
    drawGroupEditor(state.groups[g_ui.group], g_ui.group, state, changed);

    if (changed)
        out = fx::bsllight::sliceStateToJson(state);
    return changed;
}

}
