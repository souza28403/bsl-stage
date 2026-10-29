#include "ui/panels/MapView.h"

#include "app/Qa.h"
#include "output/MapEdit.h"
#include "output/MapHistory.h"
#include "output/OutputRenderer.h"
#include "output/Snap.h"
#include "output/Warp.h"
#include "render/Compositor.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace bsl::ui {

namespace {

using mapedit::Xform;
using snap::RectF;

using Quad = std::array<Point2, 4>;

constexpr int kGridSteps[4] = {8, 16, 32, 64};

// Estado compartilhado pelas duas vistas: ima, grade, regua e historico
struct Shared
{
    bool snapOn = true; // ligado por padrao
    bool gridOn = false;
    int gridIdx = 1;
    bool ruler = true;
    OutputHistory history{100};
};
Shared g_shared;

enum class DragKind { None, Pan, Move, Resize, Rotate, Corner, Marquee };

struct DragState
{
    DragKind kind = DragKind::None;
    ImVec2 startMouse{};
    Point2 startContent{};
    ImVec2 startOffset{};  // pan
    bool moved = false;
    int handle = -1;       // alca (Redimensionar) ou canto (Corner)
    int cornerSlice = -1;
    std::vector<int> idx;  // slices que andam juntos (Mover)
    std::vector<Quad> start;
    Xform startX;
    int hitSlice = -1;
    bool toggleOnRelease = false;
    bool collapseOnRelease = false;
    std::vector<int> baseSel; // selecao antes do retangulo (Ctrl/Shift soma)
    snap::Result snap;
    float angle = 0.0f;       // rotacao em curso (rad), para o rotulo
};

struct ViewState
{
    float scale = 1.0f;
    ImVec2 offset{0.0f, 0.0f}; // origem do conteudo em relacao ao canto da vista
    bool autoFit = true;
    bool recenter = false;     // pedido de "100%": centraliza o conteudo na proxima medida da vista
    int lastKey = -1;          // tela (Saida) ou -2 (Entrada) em que o ajuste foi feito
    int lastW = 0;
    int lastH = 0;
    DragState drag;
    bool openDelete = false;
    int deleteCount = 0;
};
ViewState g_views[2];

ViewState& viewOf(MapViewKind k) { return g_views[k == MapViewKind::Input ? 0 : 1]; }

ImU32 color(const ImVec4& c, float alpha = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

float handleRadius() { return dp(7.0f); }

float dist(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

float clampRange(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

Screen* currentScreen(PanelContext& ctx)
{
    auto& screens = ctx.output.screens;
    const int i = ctx.mapping.screen;
    return i >= 0 && i < static_cast<int>(screens.size()) ? &screens[static_cast<size_t>(i)] : nullptr;
}

struct Content
{
    int w = 1;
    int h = 1;
};

// --------------------------------------------------------------------------------------------------------------
// Geometria: a Entrada e a Saida editam "quads" (a Entrada e um retangulo com 4 cantos)

Quad quadOf(MapViewKind k, const Slice& s)
{
    Quad q;
    if (k == MapViewKind::Input) {
        const float x = static_cast<float>(s.input.x), y = static_cast<float>(s.input.y);
        const float w = static_cast<float>(s.input.w), h = static_cast<float>(s.input.h);
        q = {Point2{x, y}, Point2{x + w, y}, Point2{x + w, y + h}, Point2{x, y + h}};
    } else {
        for (int i = 0; i < 4; ++i)
            q[static_cast<size_t>(i)] = s.corners[i];
    }
    return q;
}

RectF boundsOfQuad(const Quad& q)
{
    return snap::boundsOf(q.data());
}

void setQuad(MapViewKind k, Slice& s, const Quad& q, Content c)
{
    if (k == MapViewKind::Output) {
        for (int i = 0; i < 4; ++i)
            s.corners[i] = q[static_cast<size_t>(i)];
        return;
    }
    const RectF b = boundsOfQuad(q);
    int w = std::clamp(static_cast<int>(std::lround(b.width())), 1, c.w);
    int h = std::clamp(static_cast<int>(std::lround(b.height())), 1, c.h);
    int x = std::clamp(static_cast<int>(std::lround(b.x0)), 0, c.w - w);
    int y = std::clamp(static_cast<int>(std::lround(b.y0)), 0, c.h - h);
    s.input = {x, y, w, h};
}

// Limites do conteudo: a Entrada fica dentro do canvas; a Saida pode passar um pouco da tela (como sempre foi)
RectF limitOf(MapViewKind k, Content c)
{
    const float w = static_cast<float>(c.w), h = static_cast<float>(c.h);
    if (k == MapViewKind::Input)
        return {0.0f, 0.0f, w, h};
    return {-0.25f * w, -0.25f * h, 1.25f * w, 1.25f * h};
}

std::vector<int> selectedIndices(const MappingSelection& sel, const Screen& screen)
{
    std::vector<int> out;
    for (const int i : sel.multi)
        if (i >= 0 && i < static_cast<int>(screen.slices.size()))
            out.push_back(i);
    return out;
}

snap::Options snapOptions(const ViewState& v)
{
    snap::Options o;
    o.enabled = g_shared.snapOn;
    o.altHeld = ImGui::GetIO().KeyAlt;
    o.thresholdPx = dp(8.0f);
    o.zoom = v.scale;
    o.gridOn = g_shared.gridOn;
    o.gridStep = static_cast<float>(kGridSteps[g_shared.gridIdx]);
    return o;
}

// Alvos do ima: os OUTROS slices (retangulos de entrada na Entrada, quads de saida na Saida) e as bordas/centro do
// canvas ou da tela
snap::Targets buildTargets(MapViewKind k, const Screen& screen, Content c, const std::vector<int>& exclude)
{
    snap::Targets t;
    for (int j = 0; j < static_cast<int>(screen.slices.size()); ++j) {
        if (std::find(exclude.begin(), exclude.end(), j) != exclude.end())
            continue;
        const Quad q = quadOf(k, screen.slices[static_cast<size_t>(j)]);
        snap::addQuad(t, q.data());
    }
    snap::addBounds(t, {0.0f, 0.0f, static_cast<float>(c.w), static_cast<float>(c.h)});
    return t;
}

int hitSlice(MapViewKind k, const Screen& screen, const MappingSelection& sel, Point2 p)
{
    auto inside = [&](int j) {
        const Quad q = quadOf(k, screen.slices[static_cast<size_t>(j)]);
        return pointInQuad(q.data(), p);
    };
    const int n = static_cast<int>(screen.slices.size());
    for (int j = n - 1; j >= 0; --j) // o selecionado ganha (da para arrastar um slice coberto por outro)
        if (isSliceSelected(sel, j) && inside(j))
            return j;
    for (int j = n - 1; j >= 0; --j)
        if (inside(j))
            return j;
    return -1;
}

// Retangulo "transformavel" de um slice: o retangulo exato, ou a caixa dos cantos se o quad estiver deformado
Xform xformOf(MapViewKind k, const Slice& s, bool* exact = nullptr)
{
    Xform x;
    const Quad q = quadOf(k, s);
    const bool ok = mapedit::fromCorners(q.data(), x);
    if (exact)
        *exact = ok;
    return x;
}

// --------------------------------------------------------------------------------------------------------------
// Operacoes sobre a selecao

void translateSelection(MapViewKind k, Screen& screen, const std::vector<int>& idx, Point2 d, Content c)
{
    for (const int i : idx) {
        Slice& s = screen.slices[static_cast<size_t>(i)];
        Quad q = quadOf(k, s);
        for (Point2& p : q) {
            p.x += d.x;
            p.y += d.y;
        }
        setQuad(k, s, q, c);
    }
}

// Mover a selecao inteira respeitando os limites (as setas)
void nudgeSelection(MapViewKind k, Screen& screen, const std::vector<int>& idx, Point2 d, Content c)
{
    bool any = false;
    RectF bb;
    for (const int i : idx) {
        const RectF b = boundsOfQuad(quadOf(k, screen.slices[static_cast<size_t>(i)]));
        bb = any ? snap::unite(bb, b) : b;
        any = true;
    }
    if (!any)
        return;
    const RectF lim = limitOf(k, c);
    d.x = clampRange(d.x, lim.x0 - bb.x0, lim.x1 - bb.x1);
    d.y = clampRange(d.y, lim.y0 - bb.y0, lim.y1 - bb.y1);
    translateSelection(k, screen, idx, d, c);
}

void applyOffsets(MapViewKind k, Screen& screen, const std::vector<int>& idx, const std::vector<Point2>& offs, Content c)
{
    for (size_t n = 0; n < idx.size() && n < offs.size(); ++n)
        translateSelection(k, screen, {idx[n]}, offs[n], c);
}

std::vector<RectF> boxesOf(MapViewKind k, const Screen& screen, const std::vector<int>& idx)
{
    std::vector<RectF> out;
    for (const int i : idx)
        out.push_back(boundsOfQuad(quadOf(k, screen.slices[static_cast<size_t>(i)])));
    return out;
}

void doAlign(MapViewKind k, Screen& screen, const MappingSelection& sel, snap::Align a, Content c)
{
    const std::vector<int> idx = selectedIndices(sel, screen);
    if (idx.empty())
        return;
    const RectF whole{0.0f, 0.0f, static_cast<float>(c.w), static_cast<float>(c.h)};
    applyOffsets(k, screen, idx, snap::alignOffsets(boxesOf(k, screen, idx), a, idx.size() == 1 ? &whole : nullptr), c);
}

void doDistribute(MapViewKind k, Screen& screen, const MappingSelection& sel, snap::Distribute d, Content c)
{
    const std::vector<int> idx = selectedIndices(sel, screen);
    if (idx.size() < 3)
        return;
    applyOffsets(k, screen, idx, snap::distributeOffsets(boxesOf(k, screen, idx), d), c);
}

// "Encostar": o grupo selecionado cola no vizinho mais proximo na direcao escolhida
void doAbut(MapViewKind k, Screen& screen, const MappingSelection& sel, snap::Dir dir, Content c)
{
    const std::vector<int> idx = selectedIndices(sel, screen);
    if (idx.empty())
        return;
    std::vector<RectF> others;
    for (int j = 0; j < static_cast<int>(screen.slices.size()); ++j)
        if (std::find(idx.begin(), idx.end(), j) == idx.end())
            others.push_back(boundsOfQuad(quadOf(k, screen.slices[static_cast<size_t>(j)])));
    const std::vector<RectF> mine = boxesOf(k, screen, idx);
    RectF group = mine.front();
    for (const RectF& b : mine)
        group = snap::unite(group, b);
    Point2 off;
    if (snap::abutOffset(group, others, dir, off))
        translateSelection(k, screen, idx, off, c);
}

void removeSelected(PanelContext& ctx, Screen& screen)
{
    std::vector<int> idx = selectedIndices(ctx.mapping, screen);
    std::sort(idx.begin(), idx.end());
    if (idx.empty())
        return;
    for (auto it = idx.rbegin(); it != idx.rend(); ++it)
        ctx.output.removeSlice(ctx.mapping.screen, *it);
    const int n = static_cast<int>(screen.slices.size());
    selectOnly(ctx.mapping, n > 0 ? std::min(idx.front(), n - 1) : -1);
}

void duplicateSelected(PanelContext& ctx, Screen& screen)
{
    std::vector<int> idx = selectedIndices(ctx.mapping, screen);
    std::sort(idx.begin(), idx.end());
    if (idx.empty())
        return;
    std::vector<int> created;
    int added = 0;
    for (const int i : idx) {
        const int n = ctx.output.duplicateSlice(ctx.mapping.screen, i + added);
        if (n >= 0) {
            created.push_back(n);
            ++added;
        }
    }
    if (!created.empty())
        setSliceSelection(ctx.mapping, created, created.back());
}

bool undoNow(PanelContext& ctx)
{
    if (!g_shared.history.undo(ctx.output))
        return false;
    for (ViewState& v : g_views)
        v.drag = {};
    return true;
}

bool redoNow(PanelContext& ctx)
{
    if (!g_shared.history.redo(ctx.output))
        return false;
    for (ViewState& v : g_views)
        v.drag = {};
    return true;
}

// --------------------------------------------------------------------------------------------------------------
// Regua

float niceStep(float minContentStep, float* mantissa)
{
    float p = 1.0f;
    while (p * 10.0f <= minContentStep)
        p *= 10.0f;
    while (p > minContentStep)
        p /= 10.0f;
    for (const float m : {1.0f, 2.0f, 5.0f, 10.0f}) {
        if (p * m >= minContentStep) {
            *mantissa = m;
            return p * m;
        }
    }
    *mantissa = 10.0f;
    return p * 10.0f;
}

void drawRulers(ImDrawList* dl, ImVec2 vpMin, ImVec2 vpMax, ImVec2 origin, float scale, ImVec2 mouse, bool hovered)
{
    const float th = dp(14.0f), lw = dp(30.0f);
    ImFont* mono = fonts().mono;
    const float fpx = fontPx(mono) * 0.80f;
    const ImU32 bg = color(theme::kPanel, 0.88f);
    const ImU32 line = color(theme::kBorder);
    const ImU32 tick = color(theme::kTextDim);
    const ImU32 text = color(theme::kTextDim);

    dl->AddRectFilled(vpMin, ImVec2(vpMax.x, vpMin.y + th), bg);
    dl->AddRectFilled(vpMin, ImVec2(vpMin.x + lw, vpMax.y), bg);
    dl->AddLine(ImVec2(vpMin.x, vpMin.y + th), ImVec2(vpMax.x, vpMin.y + th), line);
    dl->AddLine(ImVec2(vpMin.x + lw, vpMin.y), ImVec2(vpMin.x + lw, vpMax.y), line);

    float mant = 1.0f;
    const float major = niceStep(dp(64.0f) / scale, &mant);
    const float sub = mant == 2.0f ? 4.0f : 5.0f;
    const float minor = major / sub;
    const bool drawMinor = minor * scale >= dp(5.0f);

    for (int axis = 0; axis < 2; ++axis) {
        const bool horiz = axis == 0;
        const float a0 = horiz ? vpMin.x : vpMin.y;
        const float a1 = horiz ? vpMax.x : vpMax.y;
        const float o = horiz ? origin.x : origin.y;
        const float first = std::floor((a0 - o) / scale / minor) * minor;
        dl->PushClipRect(horiz ? ImVec2(vpMin.x + lw, vpMin.y) : ImVec2(vpMin.x, vpMin.y + th),
                         horiz ? ImVec2(vpMax.x, vpMin.y + th) : ImVec2(vpMin.x + lw, vpMax.y), true);
        for (float c = first; o + c * scale <= a1; c += minor) {
            const float p = o + c * scale;
            if (p < a0)
                continue;
            const bool isMajor = std::abs(std::fmod(std::abs(c) + major * 0.5f, major) - major * 0.5f) < minor * 0.01f;
            if (!isMajor && !drawMinor)
                continue;
            const float len = isMajor ? dp(8.0f) : dp(4.0f);
            if (horiz) {
                dl->AddLine(ImVec2(p, vpMin.y + th - len), ImVec2(p, vpMin.y + th), tick);
                if (isMajor) {
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(c)));
                    dl->AddText(mono, fpx, ImVec2(p + dp(2.0f), vpMin.y), text, buf);
                }
            } else {
                dl->AddLine(ImVec2(vpMin.x + lw - len, p), ImVec2(vpMin.x + lw, p), tick);
                if (isMajor) {
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(c)));
                    dl->AddText(mono, fpx, ImVec2(vpMin.x + dp(2.0f), p - fpx - dp(1.0f)), text, buf);
                }
            }
        }
        dl->PopClipRect();
    }
    dl->AddRectFilled(vpMin, ImVec2(vpMin.x + lw, vpMin.y + th), color(theme::kPanelRaised));
    if (hovered) { // posicao do mouse nas reguas (discreto)
        const ImU32 red = color(theme::kAccent);
        if (mouse.x > vpMin.x + lw)
            dl->AddLine(ImVec2(mouse.x, vpMin.y), ImVec2(mouse.x, vpMin.y + th), red);
        if (mouse.y > vpMin.y + th)
            dl->AddLine(ImVec2(vpMin.x, mouse.y), ImVec2(vpMin.x + lw, mouse.y), red);
    }
}

// --------------------------------------------------------------------------------------------------------------
// Barra de ferramentas da vista

struct Bar
{
    float right = 0.0f;
    bool first = true;
    float gap = 0.0f;

    explicit Bar() { right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }
    void group() { gap = dp(10.0f); }
    // Posiciona o proximo item de largura 'w': na mesma linha se couber, senao na seguinte
    void place(float w)
    {
        if (!first) {
            const float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + gap;
            if (x + w <= right)
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + gap);
        }
        first = false;
        gap = 0.0f;
    }
};

bool barIcon(Bar& bar, const char* icon, const char* id, const char* tip, bool active = false)
{
    bar.place(ImGui::GetFrameHeight());
    return iconButton(icon, id, tip, ImVec2(0.0f, 0.0f), active ? IconButton_Active : 0);
}

bool barChip(Bar& bar, const char* label, bool on, const char* tip)
{
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    bar.place(w);
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    }
    const bool pressed = ImGui::Button(label);
    if (on)
        ImGui::PopStyleColor(4);
    if (tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    return pressed;
}

} // namespace

// --------------------------------------------------------------------------------------------------------------
// Selecao

bool isSliceSelected(const MappingSelection& sel, int slice)
{
    return std::find(sel.multi.begin(), sel.multi.end(), slice) != sel.multi.end();
}

void setSliceSelection(MappingSelection& sel, std::vector<int> slices, int primary)
{
    std::sort(slices.begin(), slices.end());
    slices.erase(std::unique(slices.begin(), slices.end()), slices.end());
    if (primary >= 0 && std::find(slices.begin(), slices.end(), primary) == slices.end())
        slices.push_back(primary);
    if (!slices.empty() && primary < 0)
        primary = slices.back();
    std::sort(slices.begin(), slices.end());
    sel.multi = std::move(slices);
    sel.multiScreen = sel.screen;
    sel.slice = primary;
    sel.corner = -1;
}

void selectOnly(MappingSelection& sel, int slice)
{
    setSliceSelection(sel, slice >= 0 ? std::vector<int>{slice} : std::vector<int>{}, slice);
}

void toggleSliceSelection(MappingSelection& sel, int slice)
{
    if (sel.multiScreen != sel.screen)
        sel.multi.clear();
    std::vector<int> v = sel.multi;
    const auto it = std::find(v.begin(), v.end(), slice);
    int primary = slice;
    if (it != v.end()) {
        v.erase(it);
        primary = v.empty() ? -1 : (sel.slice != slice && std::find(v.begin(), v.end(), sel.slice) != v.end() ? sel.slice : v.back());
    } else {
        v.push_back(slice);
    }
    setSliceSelection(sel, std::move(v), primary);
}

void normalizeSelection(MappingSelection& sel, int sliceCount)
{
    std::erase_if(sel.multi, [&](int i) { return i < 0 || i >= sliceCount; });
    std::sort(sel.multi.begin(), sel.multi.end());
    sel.multi.erase(std::unique(sel.multi.begin(), sel.multi.end()), sel.multi.end());
    if (sel.slice < 0 || sel.slice >= sliceCount) {
        if (sel.slice >= sliceCount)
            sel.slice = sliceCount - 1;
        if (sel.slice < 0) {
            sel.multi.clear();
            sel.multiScreen = sel.screen;
            return;
        }
    }
    if (sel.multiScreen != sel.screen || !isSliceSelected(sel, sel.slice))
        sel.multi = {sel.slice};
    sel.multiScreen = sel.screen;
}

// --------------------------------------------------------------------------------------------------------------
// Historico

void mapHistoryObserve(PanelContext& ctx)
{
    const ImGuiIO& io = ImGui::GetIO();
    bool gesture = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || io.WantTextInput;
    for (const ViewState& v : g_views)
        gesture = gesture || v.drag.kind != DragKind::None;
    g_shared.history.observe(ctx.output, gesture);
}

void mapHistoryCommit(PanelContext& ctx)
{
    g_shared.history.observe(ctx.output, false);
}

void mapDuplicateSelection(PanelContext& ctx)
{
    if (Screen* s = currentScreen(ctx))
        duplicateSelected(ctx, *s);
}

void mapRemoveSelection(PanelContext& ctx)
{
    if (Screen* s = currentScreen(ctx))
        removeSelected(ctx, *s);
}

void mapEditorReset(const OutputSetup& output)
{
    g_shared.history.reset(output);
    for (ViewState& v : g_views) {
        v.drag = {};
        v.autoFit = true;
        v.lastKey = -1;
    }
}

MapEditorInfo mapEditorInfo()
{
    MapEditorInfo info;
    info.undoCount = static_cast<int>(g_shared.history.undoCount());
    info.redoCount = static_cast<int>(g_shared.history.redoCount());
    info.snap = g_shared.snapOn;
    info.grid = g_shared.gridOn;
    info.gridStep = kGridSteps[g_shared.gridIdx];
    info.inputZoom = g_views[0].scale;
    info.outputZoom = g_views[1].scale;
    for (const ViewState& v : g_views) {
        info.dragging = info.dragging || (v.drag.kind != DragKind::None && v.drag.kind != DragKind::Pan);
        info.guides += static_cast<int>(v.drag.snap.guides.size());
    }
    return info;
}

// --------------------------------------------------------------------------------------------------------------
// Vista

void drawMapView(PanelContext& ctx, MapViewKind kind)
{
    const bool isOutput = kind == MapViewKind::Output;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(isOutput ? panel::kMapOutput : panel::kMapInput,
               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    Screen* screenPtr = currentScreen(ctx);
    if (!screenPtr) {
        ImGui::TextColored(theme::kTextDim, "Crie uma tela na Lista.");
        endPanel();
        ImGui::PopStyleVar();
        return;
    }
    Screen& screen = *screenPtr;
    ViewState& v = viewOf(kind);
    MappingSelection& sel = ctx.mapping;
    normalizeSelection(sel, static_cast<int>(screen.slices.size()));
    const ImGuiIO& io = ImGui::GetIO();

    const Content content = isOutput ? Content{std::max(screen.width, 1), std::max(screen.height, 1)}
                                     : Content{std::max(ctx.canvas.width(), 1), std::max(ctx.canvas.height(), 1)};
    const bool pointsMode = isOutput && sel.pointsMode;

    // Depois de desfazer/refazer/duplicar/remover os indices e referencias deste frame nao valem mais: fecha o painel
    bool bail = false;

    // --- Barra -----------------------------------------------------------------------------------------------
    {
        Bar bar;
        if (barIcon(bar, ICON_MAGNET, "##map_ima",
                    "Ímã: encaixa nas bordas, cantos e centros dos outros slices e da tela (Alt desliga enquanto segura)",
                    g_shared.snapOn))
            g_shared.snapOn = !g_shared.snapOn;
        if (barIcon(bar, ICON_GRID, "##map_grade", "Grade (o ímã também encaixa nela)", g_shared.gridOn))
            g_shared.gridOn = !g_shared.gridOn;
        if (g_shared.gridOn) {
            char label[16];
            std::snprintf(label, sizeof(label), "%d px", kGridSteps[g_shared.gridIdx]);
            bar.place(dp(64.0f));
            ImGui::SetNextItemWidth(dp(64.0f));
            if (beginCombo("##map_grade_tam", label)) {
                for (int i = 0; i < 4; ++i) {
                    char item[16];
                    std::snprintf(item, sizeof(item), "%d px", kGridSteps[i]);
                    if (ImGui::Selectable(item, g_shared.gridIdx == i))
                        g_shared.gridIdx = i;
                }
                ImGui::EndCombo();
            }
        }
        if (barIcon(bar, ICON_RULER, "##map_regua", "Régua em px", g_shared.ruler))
            g_shared.ruler = !g_shared.ruler;

        bar.group();
        if (barIcon(bar, ICON_MAXIMIZE, "##map_ajustar", "Ajustar à vista"))
            v.autoFit = true;
        bar.place(ImGui::CalcTextSize("100%").x + ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::Button("100%##map_100")) { // 1 px do conteudo = 1 px da tela, centrado quando a vista for medida
            v.autoFit = false;
            v.scale = 1.0f;
            v.recenter = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Zoom 100%%: 1 px do conteúdo = 1 px da tela");

        bar.group();
        ImGui::BeginDisabled(!g_shared.history.canUndo());
        if (barIcon(bar, ICON_UNDO, "##map_desfazer", "Desfazer (Ctrl+Z)"))
            bail = undoNow(ctx) || bail;
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!g_shared.history.canRedo());
        if (barIcon(bar, ICON_REDO, "##map_refazer", "Refazer (Ctrl+Y)"))
            bail = redoNow(ctx) || bail;
        ImGui::EndDisabled();

        if (isOutput) {
            bar.group();
            if (barChip(bar, "Transformar##map_modo_transformar", !sel.pointsMode,
                        "Posição, tamanho e rotação: o quad vira um retângulo"))
                sel.pointsMode = false;
            if (barChip(bar, "Pontos (warp)##map_modo_pontos", sel.pointsMode, "Os 4 cantos livres (perspectiva)"))
                sel.pointsMode = true;
        }

        const int nSel = static_cast<int>(selectedIndices(sel, screen).size());
        bar.group();
        ImGui::BeginDisabled(nSel < 1);
        if (barIcon(bar, ICON_ALIGN_LEFT, "##map_alinhar_esq", "Alinhar à esquerda (com 1 slice: à borda do canvas/tela)"))
            doAlign(kind, screen, sel, snap::Align::Left, content);
        if (barIcon(bar, ICON_ALIGN_CENTER_H, "##map_alinhar_centro_h", "Centralizar na horizontal"))
            doAlign(kind, screen, sel, snap::Align::CenterH, content);
        if (barIcon(bar, ICON_ALIGN_RIGHT, "##map_alinhar_dir", "Alinhar à direita"))
            doAlign(kind, screen, sel, snap::Align::Right, content);
        if (barIcon(bar, ICON_ALIGN_TOP, "##map_alinhar_topo", "Alinhar ao topo"))
            doAlign(kind, screen, sel, snap::Align::Top, content);
        if (barIcon(bar, ICON_ALIGN_MIDDLE_V, "##map_alinhar_meio", "Centralizar na vertical"))
            doAlign(kind, screen, sel, snap::Align::MiddleV, content);
        if (barIcon(bar, ICON_ALIGN_BOTTOM, "##map_alinhar_base", "Alinhar à base"))
            doAlign(kind, screen, sel, snap::Align::Bottom, content);
        ImGui::EndDisabled();
        bar.group();
        ImGui::BeginDisabled(nSel < 3);
        if (barIcon(bar, ICON_DISTRIBUTE_H, "##map_distribuir_h", "Distribuir na horizontal (3 ou mais slices)"))
            doDistribute(kind, screen, sel, snap::Distribute::Horizontal, content);
        if (barIcon(bar, ICON_DISTRIBUTE_V, "##map_distribuir_v", "Distribuir na vertical (3 ou mais slices)"))
            doDistribute(kind, screen, sel, snap::Distribute::Vertical, content);
        ImGui::EndDisabled();
        bar.group();
        ImGui::BeginDisabled(nSel < 1);
        if (barIcon(bar, ICON_ARROW_LEFT_TO_LINE, "##map_encostar_esq", "Encostar no vizinho da esquerda"))
            doAbut(kind, screen, sel, snap::Dir::Left, content);
        if (barIcon(bar, ICON_ARROW_RIGHT_TO_LINE, "##map_encostar_dir", "Encostar no vizinho da direita"))
            doAbut(kind, screen, sel, snap::Dir::Right, content);
        if (barIcon(bar, ICON_ARROW_UP_TO_LINE, "##map_encostar_cima", "Encostar no vizinho de cima"))
            doAbut(kind, screen, sel, snap::Dir::Up, content);
        if (barIcon(bar, ICON_ARROW_DOWN_TO_LINE, "##map_encostar_baixo", "Encostar no vizinho de baixo"))
            doAbut(kind, screen, sel, snap::Dir::Down, content);
        ImGui::EndDisabled();
    }
    if (bail) {
        endPanel();
        ImGui::PopStyleVar();
        return;
    }
    normalizeSelection(sel, static_cast<int>(screen.slices.size())); // a barra pode ter mexido na selecao

    // --- Area da vista -----------------------------------------------------------------------------------------
    const float captionH = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 vpSize(avail.x, avail.y - captionH);
    if (vpSize.x < dp(60.0f) || vpSize.y < dp(60.0f)) {
        endPanel();
        ImGui::PopStyleVar();
        return;
    }
    const std::string btnId = isOutput ? "##saida" : "##entrada";
    ImGui::InvisibleButton(btnId.c_str(), vpSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    // As setas sao da vista (nao navegam entre itens) e o Alt tambem: segurar Alt para desligar o ima nao pode, ao soltar,
    // ativar a navegacao da barra de menu do ImGui (que tiraria o foco do teclado da vista)
    for (ImGuiKey k : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiMod_Alt})
        ImGui::SetItemKeyOwner(k);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    const bool deactivated = ImGui::IsItemDeactivated();
    const ImVec2 vpMin = ImGui::GetItemRectMin();
    const ImVec2 vpMax = ImGui::GetItemRectMax();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 mouse = io.MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Ajustar a vista: o conteudo inteiro, centrado, com margem para as alcas e a regua
    const float cw = static_cast<float>(content.w), ch = static_cast<float>(content.h);
    const float margin = dp(34.0f);
    const float rulerW = g_shared.ruler ? dp(30.0f) : 0.0f, rulerH = g_shared.ruler ? dp(14.0f) : 0.0f;
    const float fitScale = std::max(0.001f, std::min((vpSize.x - rulerW - margin * 2.0f) / cw,
                                                     (vpSize.y - rulerH - margin * 2.0f) / ch));
    const int key = isOutput ? screen.id : -2;
    if (v.lastKey != key || v.lastW != content.w || v.lastH != content.h) {
        v.autoFit = true;
        v.lastKey = key;
        v.lastW = content.w;
        v.lastH = content.h;
    }
    if (v.autoFit) {
        v.scale = fitScale;
        v.offset = ImVec2(rulerW + (vpSize.x - rulerW - cw * v.scale) * 0.5f, rulerH + (vpSize.y - rulerH - ch * v.scale) * 0.5f);
    } else if (v.recenter) { // pedido de 100%: centraliza
        v.offset = ImVec2(rulerW + (vpSize.x - rulerW - cw * v.scale) * 0.5f, rulerH + (vpSize.y - rulerH - ch * v.scale) * 0.5f);
    }
    v.recenter = false;

    auto toScreen = [&](Point2 p) {
        return ImVec2(vpMin.x + v.offset.x + p.x * v.scale, vpMin.y + v.offset.y + p.y * v.scale);
    };
    auto toContent = [&](ImVec2 m) {
        return Point2{(m.x - vpMin.x - v.offset.x) / v.scale, (m.y - vpMin.y - v.offset.y) / v.scale};
    };

    // Zoom na roda, no ponto do cursor
    if (hovered && io.MouseWheel != 0.0f && v.drag.kind == DragKind::None) {
        const Point2 anchor = toContent(mouse);
        v.scale = std::clamp(v.scale * std::pow(1.12f, io.MouseWheel), 0.02f, 64.0f);
        v.offset = ImVec2(mouse.x - vpMin.x - anchor.x * v.scale, mouse.y - vpMin.y - anchor.y * v.scale);
        v.autoFit = false;
    }

    // --- Estado de selecao desta vista -----------------------------------------------------------------------
    std::vector<int> selIdx;
    bool single = false;
    int primary = -1;
    bool exactRect = true;
    Xform primaryX;
    bool transformHandles = false;
    auto refreshSelection = [&]() { // de novo depois de qualquer mudanca de selecao ou de geometria
        selIdx = selectedIndices(sel, screen);
        single = selIdx.size() == 1;
        primary = single ? selIdx.front() : -1;
        exactRect = true;
        if (single)
            primaryX = xformOf(kind, screen.slices[static_cast<size_t>(primary)], &exactRect);
        transformHandles = single && !pointsMode;
    };
    refreshSelection();
    const float rotDist = dp(26.0f) / v.scale;
    const bool canRotate = isOutput;

    auto handleScreen = [&](int h) { return toScreen(mapedit::handlePoint(primaryX, h)); };
    auto rotateScreen = [&]() { return toScreen(mapedit::rotateHandlePoint(primaryX, rotDist)); };
    auto hitResizeHandle = [&](ImVec2 m) {
        int best = -1;
        float bestD = handleRadius() + dp(2.0f);
        for (int h = 0; h < 8; ++h) {
            const float d = dist(handleScreen(h), m);
            if (d < bestD) {
                bestD = d;
                best = h;
            }
        }
        return best;
    };
    auto hitCorner = [&](ImVec2 m, int& sliceOut) {
        int best = -1;
        float bestD = handleRadius() + dp(4.0f);
        for (const int j : selIdx) {
            const Quad q = quadOf(kind, screen.slices[static_cast<size_t>(j)]);
            for (int c = 0; c < 4; ++c) {
                const float d = dist(toScreen(q[static_cast<size_t>(c)]), m);
                if (d < bestD) {
                    bestD = d;
                    best = c;
                    sliceOut = j;
                }
            }
        }
        return best;
    };

    const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space) && focused;

    // --- Pressionou o mouse -----------------------------------------------------------------------------------
    if (activated) {
        DragState d;
        d.startMouse = mouse;
        d.startContent = toContent(mouse);
        const bool modifier = io.KeyCtrl || io.KeyShift;
        if (io.MouseDown[ImGuiMouseButton_Middle] || (spaceHeld && io.MouseDown[ImGuiMouseButton_Left])) {
            d.kind = DragKind::Pan;
            d.startOffset = v.offset;
        } else {
            int cornerSlice = -1;
            int corner = pointsMode ? hitCorner(mouse, cornerSlice) : -1;
            if (corner >= 0) {
                d.kind = DragKind::Corner;
                d.handle = corner;
                d.cornerSlice = cornerSlice;
                d.start.push_back(quadOf(kind, screen.slices[static_cast<size_t>(cornerSlice)]));
                if (single)
                    sel.corner = corner;
            } else if (transformHandles && canRotate && dist(rotateScreen(), mouse) <= handleRadius() + dp(2.0f)) {
                d.kind = DragKind::Rotate;
                d.startX = primaryX;
                d.angle = primaryX.rot;
            } else if (transformHandles && hitResizeHandle(mouse) >= 0) {
                d.kind = DragKind::Resize;
                d.handle = hitResizeHandle(mouse);
                d.startX = primaryX;
                if (d.handle % 2 == 0) // alcas de canto: 0 TL, 2 TR, 4 BR, 6 BL
                    sel.corner = d.handle / 2;
            } else {
                const int hit = hitSlice(kind, screen, sel, d.startContent);
                if (hit >= 0) {
                    d.hitSlice = hit;
                    if (modifier) {
                        if (isSliceSelected(sel, hit))
                            d.toggleOnRelease = true;
                        else
                            toggleSliceSelection(sel, hit);
                    } else if (isSliceSelected(sel, hit)) {
                        if (selIdx.size() > 1)
                            d.collapseOnRelease = true;
                        sel.slice = hit;
                        sel.corner = -1;
                    } else {
                        selectOnly(sel, hit);
                    }
                    d.kind = DragKind::Move;
                    d.idx = selectedIndices(sel, screen);
                    for (const int j : d.idx)
                        d.start.push_back(quadOf(kind, screen.slices[static_cast<size_t>(j)]));
                } else {
                    d.kind = DragKind::Marquee;
                    d.baseSel = modifier ? selIdx : std::vector<int>{};
                    if (!modifier)
                        selectOnly(sel, -1);
                }
            }
        }
        v.drag = std::move(d);
    }

    // --- Arrasto -------------------------------------------------------------------------------------------------
    if (v.drag.kind != DragKind::None && !active && !deactivated)
        v.drag = {}; // perdeu o gesto (troca de modo, popup...)
    if (v.drag.kind != DragKind::None && (active || deactivated)) {
        DragState& d = v.drag;
        if (!d.moved && dist(mouse, d.startMouse) > dp(3.0f))
            d.moved = true;
        const float rawDx = (mouse.x - d.startMouse.x) / v.scale;
        const float rawDy = (mouse.y - d.startMouse.y) / v.scale;
        const RectF lim = limitOf(kind, content);
        d.snap = {};

        if (d.kind == DragKind::Pan) {
            v.offset = ImVec2(d.startOffset.x + (mouse.x - d.startMouse.x), d.startOffset.y + (mouse.y - d.startMouse.y));
            v.autoFit = false;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (d.kind == DragKind::Marquee && d.moved) {
            const Point2 a = d.startContent, b = toContent(mouse);
            const RectF box{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
            std::vector<int> pick = d.baseSel;
            for (int j = 0; j < static_cast<int>(screen.slices.size()); ++j)
                if (boundsOfQuad(quadOf(kind, screen.slices[static_cast<size_t>(j)])).overlaps(box))
                    pick.push_back(j);
            setSliceSelection(sel, pick, pick.empty() ? -1 : pick.back());
        } else if (d.kind == DragKind::Move && d.moved) {
            float dx = std::round(rawDx), dy = std::round(rawDy);
            RectF b0 = boundsOfQuad(d.start.front());
            for (const Quad& q : d.start)
                b0 = snap::unite(b0, boundsOfQuad(q));
            snap::Moving m;
            for (const Quad& q : d.start)
                for (const Point2& p : q) {
                    m.xs.push_back(p.x + dx);
                    m.ys.push_back(p.y + dy);
                    m.points.push_back({p.x + dx, p.y + dy});
                }
            m.xs.push_back(b0.cx() + dx);
            m.ys.push_back(b0.cy() + dy);
            m.extent = {b0.x0 + dx, b0.y0 + dy, b0.x1 + dx, b0.y1 + dy};
            // o Snap trabalha com a posicao proposta; os valores acima ja incluem dx/dy
            snap::Result r = snap::snapMoving(m, buildTargets(kind, screen, content, d.idx), snapOptions(v));
            float fx = dx + r.dx, fy = dy + r.dy;
            const float cx = clampRange(fx, lim.x0 - b0.x0, lim.x1 - b0.x1);
            const float cy = clampRange(fy, lim.y0 - b0.y0, lim.y1 - b0.y1);
            if (cx != fx) { r.guides.erase(std::remove_if(r.guides.begin(), r.guides.end(), [](const snap::Guide& g) { return g.vertical; }), r.guides.end()); r.marks.clear(); }
            if (cy != fy) { r.guides.erase(std::remove_if(r.guides.begin(), r.guides.end(), [](const snap::Guide& g) { return !g.vertical; }), r.guides.end()); r.marks.clear(); }
            fx = cx;
            fy = cy;
            for (size_t n = 0; n < d.idx.size(); ++n) {
                Quad q = d.start[n];
                for (Point2& p : q) {
                    p.x += fx;
                    p.y += fy;
                }
                setQuad(kind, screen.slices[static_cast<size_t>(d.idx[n])], q, content);
            }
            d.snap = std::move(r);
        } else if (d.kind == DragKind::Corner && d.moved) {
            const Quad& q0 = d.start.front();
            Point2 p{q0[static_cast<size_t>(d.handle)].x + std::round(rawDx), q0[static_cast<size_t>(d.handle)].y + std::round(rawDy)};
            const snap::Result r = snap::snapPoint(p, buildTargets(kind, screen, content, {d.cornerSlice}), snapOptions(v));
            p.x += r.dx;
            p.y += r.dy;
            p.x = clampRange(p.x, lim.x0, lim.x1);
            p.y = clampRange(p.y, lim.y0, lim.y1);
            Slice& s = screen.slices[static_cast<size_t>(d.cornerSlice)];
            Quad q = quadOf(kind, s);
            q[static_cast<size_t>(d.handle)] = p;
            setQuad(kind, s, q, content);
            d.snap = r;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        } else if (d.kind == DragKind::Resize && d.moved && single) {
            const Xform& s0 = d.startX;
            const bool axisAligned = s0.rot == 0.0f;
            float dx = axisAligned ? std::round(rawDx) : rawDx;
            float dy = axisAligned ? std::round(rawDy) : rawDy;
            Point2 local = mapedit::toLocal(s0, dx, dy);
            mapedit::ResizeOptions ro;
            ro.keepAspect = io.KeyShift;
            ro.fromCenter = io.KeyAlt; // Alt: do centro (e sem ima)
            if (axisAligned && !ro.keepAspect && !ro.fromCenter) {
                snap::Moving m;
                if (mapedit::handleLeft(d.handle)) m.xs.push_back(s0.x() + local.x);
                if (mapedit::handleRight(d.handle)) m.xs.push_back(s0.x() + s0.w + local.x);
                if (mapedit::handleTop(d.handle)) m.ys.push_back(s0.y() + local.y);
                if (mapedit::handleBottom(d.handle)) m.ys.push_back(s0.y() + s0.h + local.y);
                m.extent = {s0.x(), s0.y(), s0.x() + s0.w, s0.y() + s0.h};
                const snap::Result r = snap::snapMoving(m, buildTargets(kind, screen, content, {primary}), snapOptions(v));
                local.x += r.dx;
                local.y += r.dy;
                d.snap = r;
            }
            Xform n = mapedit::resizeXform(s0, d.handle, local.x, local.y, ro);
            if (axisAligned) {
                mapedit::roundToPixels(n);
                // dentro dos limites (a Entrada nao sai do canvas)
                float x0 = std::max(n.x(), lim.x0), y0 = std::max(n.y(), lim.y0);
                float x1 = std::min(n.x() + n.w, lim.x1), y1 = std::min(n.y() + n.h, lim.y1);
                x1 = std::max(x1, x0 + 1.0f);
                y1 = std::max(y1, y0 + 1.0f);
                n.w = x1 - x0;
                n.h = y1 - y0;
                n.setPosition(x0, y0);
            }
            Quad q;
            mapedit::toCorners(n, q.data());
            setQuad(kind, screen.slices[static_cast<size_t>(primary)], q, content);
        } else if (d.kind == DragKind::Rotate && d.moved && single) {
            const Point2 c{d.startX.cx, d.startX.cy};
            float a = mapedit::rotateAngle(c, d.startContent, toContent(mouse), d.startX.rot,
                                           io.KeyShift ? mapedit::degToRad(15.0f) : 0.0f);
            if (std::abs(a) < 1.0e-4f)
                a = 0.0f;
            Xform n = d.startX;
            n.rot = a;
            d.angle = a;
            Quad q;
            mapedit::toCorners(n, q.data());
            setQuad(kind, screen.slices[static_cast<size_t>(primary)], q, content);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        if (deactivated) {
            if (!d.moved && d.kind == DragKind::Move) {
                if (d.toggleOnRelease)
                    toggleSliceSelection(sel, d.hitSlice);
                else if (d.collapseOnRelease)
                    selectOnly(sel, d.hitSlice);
            }
            v.drag = {};
        }
    }

    // Cursor nas alcas
    if ((hovered || active) && v.drag.kind == DragKind::None) {
        if (spaceHeld)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        else if (transformHandles) {
            if (canRotate && dist(rotateScreen(), mouse) <= handleRadius() + dp(2.0f)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else {
                const int h = hitResizeHandle(mouse);
                if (h >= 0) {
                    static const ImGuiMouseCursor cur[4] = {ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNS,
                                                            ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW};
                    const int steps = static_cast<int>(std::lround(mapedit::radToDeg(primaryX.rot) / 45.0f));
                    ImGui::SetMouseCursor(cur[(((h + steps) % 8) + 8) % 8 % 4]);
                }
            }
        } else if (pointsMode) {
            int s2 = -1;
            if (hitCorner(mouse, s2) >= 0)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    } else if (v.drag.kind == DragKind::Resize) {
        static const ImGuiMouseCursor cur[4] = {ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNS,
                                                ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW};
        const int steps = static_cast<int>(std::lround(mapedit::radToDeg(v.drag.startX.rot) / 45.0f));
        ImGui::SetMouseCursor(cur[(((v.drag.handle + steps) % 8) + 8) % 8 % 4]);
    }

    refreshSelection();

    // --- Teclado (so com o foco na vista) -----------------------------------------------------------------------
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (focused && !io.WantTextInput && !popupOpen && v.drag.kind == DragKind::None) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            bail = (io.KeyShift ? redoNow(ctx) : undoNow(ctx)) || bail;
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            bail = redoNow(ctx) || bail;
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            std::vector<int> all;
            for (int j = 0; j < static_cast<int>(screen.slices.size()); ++j)
                all.push_back(j);
            setSliceSelection(sel, all, all.empty() ? -1 : (sel.slice >= 0 ? sel.slice : all.back()));
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            duplicateSelected(ctx, screen);
            bail = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            const int n = static_cast<int>(selIdx.size());
            if (n > 1) {
                v.openDelete = true;
                v.deleteCount = n;
            } else if (n == 1) {
                removeSelected(ctx, screen);
                bail = true;
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            selectOnly(sel, -1);
        } else if (!io.KeyCtrl && !io.KeyAlt) {
            const int step = io.KeyShift ? 10 : 1;
            int dx = 0, dy = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dx -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) dy -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) dy += step;
            if ((dx != 0 || dy != 0) && !selIdx.empty()) {
                const Point2 delta{static_cast<float>(dx), static_cast<float>(dy)};
                if (pointsMode && single && sel.corner >= 0) { // Pontos: o canto selecionado
                    Slice& s = screen.slices[static_cast<size_t>(primary)];
                    Quad q = quadOf(kind, s);
                    Point2& p = q[static_cast<size_t>(sel.corner)];
                    const RectF lim = limitOf(kind, content);
                    p.x = clampRange(p.x + delta.x, lim.x0, lim.x1);
                    p.y = clampRange(p.y + delta.y, lim.y0, lim.y1);
                    setQuad(kind, s, q, content);
                } else {
                    nudgeSelection(kind, screen, selIdx, delta, content);
                }
            }
        }
    }
    if (bail) {
        endPanel();
        ImGui::PopStyleVar();
        return;
    }
    refreshSelection();

    // ================================================================================================================
    // Desenho
    dl->PushClipRect(vpMin, vpMax, true);
    dl->AddRectFilled(vpMin, vpMax, color(theme::kField));

    const ImVec2 ia = toScreen({0.0f, 0.0f}), ib = toScreen({cw, ch});
    if (isOutput) {
        ScreenView view;
        if (ctx.outputRenderer.screenView(screen.id, view))
            dl->AddImage(static_cast<ImTextureID>(view.texture), ia, ib, ImVec2(0, 1), ImVec2(1, 0));
        else
            dl->AddRectFilled(ia, ib, color(theme::kBackground));
    } else {
        const Compositor& canvas = ctx.canvas;
        const float s = canvas.uvMax();
        const float hx = 0.5f / cw, hy = 0.5f / ch;
        dl->AddImage(static_cast<ImTextureID>(canvas.texture()), ia, ib, ImVec2(hx, s - hy), ImVec2(s - hx, hy));
    }
    dl->AddRect(ia, ib, color(theme::kBorder));
    qa::noteRect(isOutput ? "saida:imagem" : "entrada:imagem", ia.x, ia.y, ib.x, ib.y);
    qa::noteRect(isOutput ? "saida:vista" : "entrada:vista", vpMin.x, vpMin.y, vpMax.x, vpMax.y);

    // Grade
    if (g_shared.gridOn) {
        const float step = static_cast<float>(kGridSteps[g_shared.gridIdx]);
        if (step * v.scale >= dp(4.0f)) {
            const ImU32 gc = color(theme::kText, 0.10f);
            const float x0 = std::max(ia.x, vpMin.x), x1 = std::min(ib.x, vpMax.x);
            const float y0 = std::max(ia.y, vpMin.y), y1 = std::min(ib.y, vpMax.y);
            for (float c = 0.0f; c <= cw + 0.01f; c += step) {
                const float x = ia.x + c * v.scale;
                if (x >= x0 && x <= x1)
                    dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), gc);
            }
            for (float c = 0.0f; c <= ch + 0.01f; c += step) {
                const float y = ia.y + c * v.scale;
                if (y >= y0 && y <= y1)
                    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), gc);
            }
        }
    }

    // Slices: cinza nos demais, vermelho nos selecionados
    ImFont* body = fonts().body;
    const float line1 = std::max(1.0f, dp(1.5f));
    const float line2 = std::max(1.0f, dp(2.0f));
    auto drawSlice = [&](int j, bool selected) {
        const Slice& sl = screen.slices[static_cast<size_t>(j)];
        const Quad q = quadOf(kind, sl);
        ImVec2 pts[4];
        for (int c = 0; c < 4; ++c)
            pts[c] = toScreen(q[static_cast<size_t>(c)]);
        if (selected) {
            dl->AddConvexPolyFilled(pts, 4, color(theme::kAccent, 0.14f));
            dl->AddPolyline(pts, 4, color(theme::kAccent), ImDrawFlags_Closed, line2);
        } else {
            dl->AddConvexPolyFilled(pts, 4, color(theme::kText, 0.05f));
            dl->AddPolyline(pts, 4, color(theme::kTextDim, sl.enabled ? 0.9f : 0.4f), ImDrawFlags_Closed, line1);
        }
        const float left = std::min(std::min(pts[0].x, pts[3].x), std::min(pts[1].x, pts[2].x));
        const float top = std::min(std::min(pts[0].y, pts[1].y), std::min(pts[2].y, pts[3].y));
        dl->AddText(body, fontPx(body), ImVec2(std::max(left, vpMin.x) + dp(6.0f), std::max(top, vpMin.y) + dp(4.0f)),
                    color(theme::kText, selected ? 1.0f : 0.8f), sl.name.c_str());
    };
    for (int j = 0; j < static_cast<int>(screen.slices.size()); ++j)
        if (!isSliceSelected(sel, j))
            drawSlice(j, false);
    for (const int j : selIdx)
        drawSlice(j, true);

    // Alcas
    const float hs = dp(4.0f);
    auto square = [&](ImVec2 p) {
        dl->AddRectFilled(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), color(theme::kText));
        dl->AddRect(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), color(theme::kAccent), 0.0f, 0, line1);
    };
    if (transformHandles) {
        if (!exactRect) { // quad deformado: a caixa que envolve os cantos e o que se transforma
            const ImVec2 b0 = handleScreen(0), b1 = handleScreen(4);
            drawDashedRect(dl, b0, b1, color(theme::kWarning, 0.8f));
        }
        for (int h = 0; h < 8; ++h)
            square(handleScreen(h));
        if (canRotate) {
            const ImVec2 top = handleScreen(1), rp = rotateScreen();
            dl->AddLine(top, rp, color(theme::kAccent), line1);
            dl->AddCircleFilled(rp, dp(5.0f), color(theme::kText));
            dl->AddCircle(rp, dp(5.0f), color(theme::kAccent), 0, line1);
        }
    }
    if (pointsMode) {
        for (const int j : selIdx) {
            const Quad q = quadOf(kind, screen.slices[static_cast<size_t>(j)]);
            for (int c = 0; c < 4; ++c) {
                const bool on = single && sel.corner == c;
                const ImVec2 p = toScreen(q[static_cast<size_t>(c)]);
                dl->AddCircleFilled(p, dp(6.0f), on ? color(theme::kAccent) : color(theme::kText));
                dl->AddCircle(p, dp(6.0f), color(theme::kAccent), 0, line1);
            }
        }
    }

    // Retangulo de selecao
    if (v.drag.kind == DragKind::Marquee && v.drag.moved) {
        const ImVec2 a = toScreen(v.drag.startContent);
        const ImVec2 lo(std::min(a.x, mouse.x), std::min(a.y, mouse.y)), hi(std::max(a.x, mouse.x), std::max(a.y, mouse.y));
        dl->AddRectFilled(lo, hi, color(theme::kAccent, 0.12f));
        dl->AddRect(lo, hi, color(theme::kAccent, 0.9f), 0.0f, 0, std::max(1.0f, dp(1.0f)));
    }

    // Linhas-guia do ima (vermelhas, finas) e cantos encaixados
    if (v.drag.kind != DragKind::None) {
        const float gl = std::max(1.0f, dp(1.0f));
        const float ext = dp(28.0f); // a linha passa um pouco das pontas, como nos apps da Adobe
        for (const snap::Guide& g : v.drag.snap.guides) {
            ImVec2 p0 = g.vertical ? toScreen({g.pos, g.lo}) : toScreen({g.lo, g.pos});
            ImVec2 p1 = g.vertical ? toScreen({g.pos, g.hi}) : toScreen({g.hi, g.pos});
            if (g.vertical) {
                p0.y -= ext;
                p1.y += ext;
            } else {
                p0.x -= ext;
                p1.x += ext;
            }
            dl->AddLine(p0, p1, color(theme::kAccent), gl);
        }
        for (const Point2& m : v.drag.snap.marks) {
            const ImVec2 p = toScreen(m);
            dl->AddCircle(p, dp(6.5f), color(theme::kAccent), 0, std::max(1.0f, dp(1.5f)));
            dl->AddLine(ImVec2(p.x - dp(9.0f), p.y), ImVec2(p.x + dp(9.0f), p.y), color(theme::kAccent), gl);
            dl->AddLine(ImVec2(p.x, p.y - dp(9.0f)), ImVec2(p.x, p.y + dp(9.0f)), color(theme::kAccent), gl);
        }
        if (v.drag.kind == DragKind::Rotate) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f°", mapedit::radToDeg(v.drag.angle));
            pushMono();
            dl->AddText(ImVec2(mouse.x + dp(14.0f), mouse.y + dp(10.0f)), color(theme::kText), buf);
            ImGui::PopFont();
        }
    }

    // Aviso discreto: quad deformado no modo Transformar
    if (transformHandles && isOutput && !exactRect) {
        const char* msg = "Quad deformado: o modo Transformar usa a caixa que envolve os cantos";
        const ImVec2 sz = ImGui::CalcTextSize(msg);
        const ImVec2 p(vpMin.x + dp(40.0f), vpMax.y - sz.y - dp(8.0f));
        dl->AddRectFilled(ImVec2(p.x - dp(6.0f), p.y - dp(3.0f)), ImVec2(p.x + sz.x + dp(6.0f), p.y + sz.y + dp(3.0f)),
                          color(theme::kBackground, 0.8f), rounding());
        dl->AddText(p, color(theme::kWarning), msg);
    }

    if (g_shared.ruler)
        drawRulers(dl, vpMin, vpMax, ImVec2(vpMin.x + v.offset.x, vpMin.y + v.offset.y), v.scale, mouse, hovered);
    dl->PopClipRect();

    // --- Legenda ---------------------------------------------------------------------------------------------------
    {
        char buf[320];
        const int nSel = static_cast<int>(selIdx.size());
        if (isOutput) {
            const char* tag = screen.kind == OutputKind::Monitor ? screen.monitorName.c_str() : outputKindName(screen.kind);
            if (single && !pointsMode) {
                std::snprintf(buf, sizeof(buf), "%s  |  x=%.0f y=%.0f  %.0fx%.0f  rot %.1f°  |  %dx%d  %s  |  %d%%",
                              screen.slices[static_cast<size_t>(primary)].name.c_str(), primaryX.x(), primaryX.y(),
                              primaryX.w, primaryX.h, mapedit::radToDeg(primaryX.rot), screen.width, screen.height, tag,
                              static_cast<int>(std::lround(v.scale * 100.0f)));
            } else {
                std::snprintf(buf, sizeof(buf), "%s  |  %dx%d  |  %s  |  %d selecionado(s)  |  %d%%", screen.name.c_str(),
                              screen.width, screen.height, tag, nSel, static_cast<int>(std::lround(v.scale * 100.0f)));
            }
        } else if (single) {
            const Slice& sl = screen.slices[static_cast<size_t>(primary)];
            std::snprintf(buf, sizeof(buf), "%s  |  entrada x=%d y=%d  %dx%d  |  canvas %dx%d  |  %d%%", sl.name.c_str(),
                          sl.input.x, sl.input.y, sl.input.w, sl.input.h, content.w, content.h,
                          static_cast<int>(std::lround(v.scale * 100.0f)));
        } else {
            std::snprintf(buf, sizeof(buf), "canvas %dx%d  |  %d selecionado(s)  |  %d%%", content.w, content.h, nSel,
                          static_cast<int>(std::lround(v.scale * 100.0f)));
        }
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%s", buf);
        ImGui::PopFont();
    }

    // --- Confirmacao de remocao em lote -------------------------------------------------------------------------
    const char* popupName = "Remover slices##map_remover_conf";
    if (v.openDelete) {
        ImGui::OpenPopup(popupName);
        v.openDelete = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Remover %d slices selecionados?", v.deleteCount);
        ImGui::Spacing();
        if (ImGui::Button("Remover##map_remover_ok", ImVec2(dp(110.0f), 0.0f))) {
            removeSelected(ctx, screen);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar##map_remover_cancelar", ImVec2(dp(110.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    endPanel();
    ImGui::PopStyleVar();
}

} // namespace bsl::ui
