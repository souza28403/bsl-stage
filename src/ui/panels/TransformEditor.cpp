#include "ui/panels/TransformEditor.h"

#include "app/Qa.h"
#include "core/TransformHistory.h"
#include "output/MapEdit.h"
#include "ui/panels/DmxWindow.h"
#include "output/Snap.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace bsl::ui {

namespace {

using mapedit::Xform;

enum class DragKind { None, Move, Resize, Rotate, Crop };

struct Drag
{
    DragKind kind = DragKind::None;
    TransformPanel owner = TransformPanel::Output;
    int layerId = 0;
    ImVec2 startMouse{};
    Point2 startCanvas{};
    bool moved = false;
    int handle = -1;
    LayerTransform start;
    Xform startX;
    snap::Result snap;
    float angle = 0.0f;
};

struct Shared
{
    bool on[2] = {false, false};
    bool crop = false;
    bool snapOn = true;
    bool lockAspect = false;
    TransformHistory history{100};
    Drag drag;
    int guides = 0;
};
Shared g;

constexpr const char* kPanelTag[2] = {"saida", "previa"};

ImU32 color(const ImVec4& c, float alpha = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

float handleRadius() { return dp(7.0f); }
float dist(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

Xform toXform(const OrientedBox& b)
{
    Xform x;
    x.cx = b.cx;
    x.cy = b.cy;
    x.w = b.w;
    x.h = b.h;
    x.rot = b.rot;
    return x;
}

OrientedBox toBox(const Xform& x)
{
    OrientedBox b;
    b.cx = x.cx;
    b.cy = x.cy;
    b.w = x.w;
    b.h = x.h;
    b.rot = x.rot;
    return b;
}

Point2 localToCanvas(const LayerTransform& t, int cw, int ch, float lx, float ly)
{
    const float bw = t.width(cw), bh = t.height(ch);
    const float rad = t.rotation * mapedit::kPi / 180.0f;
    const float c = std::cos(rad), s = std::sin(rad);
    const float px = t.x + t.anchorX * bw, py = t.y + t.anchorY * bh;
    const float dx = t.x + lx - px, dy = t.y + ly - py;
    if (t.rotation == 0.0f)
        return {px + dx, py + dy};
    return {px + dx * c - dy * s, py + dx * s + dy * c};
}

Xform cropXform(const LayerTransform& t, int cw, int ch)
{
    const float bw = t.width(cw), bh = t.height(ch);
    const float w = std::max(1.0f, bw - t.cropL - t.cropR), h = std::max(1.0f, bh - t.cropT - t.cropB);
    const Point2 c = localToCanvas(t, cw, ch, t.cropL + w * 0.5f, t.cropT + h * 0.5f);
    Xform x;
    x.cx = c.x;
    x.cy = c.y;
    x.w = w;
    x.h = h;
    x.rot = t.rotation == 0.0f ? 0.0f : t.rotation * mapedit::kPi / 180.0f;
    return x;
}

int selectedLayer(const PanelContext& ctx)
{
    const Selection& sel = ctx.selection;
    return (sel.kind != Selection::Kind::None && sel.layer >= 0 && sel.layer < ctx.composition.layerCount()) ? sel.layer : -1;
}

void selectLayer(PanelContext& ctx, int layer)
{
    Selection& sel = ctx.selection;
    if (sel.kind == Selection::Kind::Clip && sel.layer == layer)
        return;
    sel.kind = Selection::Kind::Layer;
    sel.layer = layer;
    sel.column = -1;
}

bool layerShown(const Composition& comp, int i)
{
    const Layer& l = comp.layers[static_cast<size_t>(i)];
    if (l.sliceOnly || l.bypass)
        return false;
    const Clip* c = comp.activeClip(i);
    return c && c->isOpen() && c->source->texture() != 0;
}

snap::Options snapOptions(float zoom)
{
    snap::Options o;
    o.enabled = g.snapOn;
    o.altHeld = ImGui::GetIO().KeyAlt;
    o.thresholdPx = dp(8.0f);
    o.zoom = zoom;
    return o;
}

snap::Targets buildTargets(const Composition& comp, int exclude)
{
    snap::Targets t;
    for (int i = 0; i < comp.layerCount(); ++i) {
        if (i == exclude || !layerShown(comp, i))
            continue;
        const LayerTransform& lt = *comp.transformRef(i);
        Point2 c[4];
        mapedit::toCorners(toXform(lt.box(comp.width, comp.height)), c);
        snap::addQuad(t, c);
    }
    snap::addBounds(t, {0.0f, 0.0f, static_cast<float>(comp.width), static_cast<float>(comp.height)});
    return t;
}

const char* dragName(DragKind k)
{
    switch (k) {
    case DragKind::Move: return "move";
    case DragKind::Resize: return "resize";
    case DragKind::Rotate: return "rotate";
    case DragKind::Crop: return "crop";
    default: return "";
    }
}

ImGuiMouseCursor resizeCursor(int handle, float rotRad)
{
    static const ImGuiMouseCursor cur[4] = {ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNS,
                                            ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeEW};
    const int steps = static_cast<int>(std::lround(mapedit::radToDeg(rotRad) / 45.0f));
    return cur[(((handle + steps) % 8) + 8) % 8 % 4];
}

void dashedLine(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float dash, float gap, float thickness)
{
    const float len = dist(p0, p1);
    if (len < 1.0f)
        return;
    const ImVec2 dir((p1.x - p0.x) / len, (p1.y - p0.y) / len);
    for (float at = 0.0f; at < len; at += dash + gap) {
        const float end = std::min(at + dash, len);
        dl->AddLine(ImVec2(p0.x + dir.x * at, p0.y + dir.y * at), ImVec2(p0.x + dir.x * end, p0.y + dir.y * end), col,
                    thickness);
    }
}

bool popupOpen()
{
    return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

}

bool& transformEditOn(TransformPanel panel)
{
    return g.on[static_cast<int>(panel)];
}

bool transformToggleButton(TransformPanel panel, ImVec2 size)
{
    const std::string id = std::string("##transformar_") + kPanelTag[static_cast<int>(panel)];
    bool& on = transformEditOn(panel);
    if (!iconToggle(ICON_MOVE, id.c_str(), on,
                    "Transformar: mostra a composição inteira e a caixa da camada selecionada, com alças para posição, "
                    "tamanho e rotação",
                    size))
        return false;
    if (!on)
        g.drag = Drag{};
    return true;
}

void drawTransformPresetItems(PanelContext& ctx, int layer)
{
    Composition& comp = ctx.composition;
    for (int i = 0; i < kTransformPresetCount; ++i) {
        const bool grid = i == static_cast<int>(TransformPreset::Grid2x2) || i == static_cast<int>(TransformPreset::Grid3x3);
        if (i == static_cast<int>(TransformPreset::Grid2x2))
            ImGui::Separator();
        ImGui::BeginDisabled(layer < 0 && !grid);
        const std::string label = std::string(transformPresetName(i)) + "##tr_preset_" + std::to_string(i);
        if (ImGui::MenuItem(label.c_str()))
            comp.applyTransformPreset(layer, i);
        ImGui::EndDisabled();
    }
}

void drawTransformTools(PanelContext& ctx, TransformPanel panel)
{
    const std::string tag = kPanelTag[static_cast<int>(panel)];
    const int layer = selectedLayer(ctx);
    ImGui::SameLine();
    iconToggle(ICON_CROP, ("##transf_recortar_" + tag).c_str(), g.crop,
               "Recortar: as alças cortam cada lado da camada (o resto não muda de tamanho)");
    ImGui::SameLine();
    iconToggle(ICON_MAGNET, ("##transf_ima_" + tag).c_str(), g.snapOn,
               "Ímã: encaixa nas bordas e no centro do canvas e das outras camadas (Alt desliga enquanto segura)");
    ImGui::SameLine();
    const std::string popup = "##transf_presets_menu_" + tag;
    if (iconButton(ICON_GRID, ("##transf_presets_" + tag).c_str(),
                   "Presets: tela cheia, ajustar, preencher, metades, quadrantes e grades 2×2 / 3×3 com as camadas"))
        ImGui::OpenPopup(popup.c_str());
    if (ImGui::BeginPopup(popup.c_str())) {
        drawTransformPresetItems(ctx, layer);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!g.history.canUndo());
    if (iconButton(ICON_UNDO, ("##transf_desfazer_" + tag).c_str(), "Desfazer (Ctrl+Z)")) {
        g.history.undo(ctx.composition);
        g.drag = Drag{};
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g.history.canRedo());
    if (iconButton(ICON_REDO, ("##transf_refazer_" + tag).c_str(), "Refazer (Ctrl+Y)")) {
        g.history.redo(ctx.composition);
        g.drag = Drag{};
    }
    ImGui::EndDisabled();
}

std::string transformCaption(const PanelContext& ctx)
{
    const int li = selectedLayer(ctx);
    if (li < 0)
        return {};
    const Composition& comp = ctx.composition;
    const LayerTransform& t = *comp.transformRef(li);
    const Clip* c = comp.activeClip(li);
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Camada %d%s  |  x=%.0f y=%.0f  %.0fx%.0f  rot %.1f°%s", li + 1,
                  c && c->ownTransform ? " (transform do clip)" : "", t.x, t.y, t.width(comp.width), t.height(comp.height),
                  t.rotation, t.cropL + t.cropT + t.cropR + t.cropB > 0.0f ? "  recortada" : "");
    return buf;
}

void drawTransformOverlay(PanelContext& ctx, TransformPanel panel, ImVec2 a, ImVec2 b)
{
    Composition& comp = ctx.composition;
    const ImGuiIO& io = ImGui::GetIO();
    const int cw = comp.width, ch = comp.height;
    if (b.x - a.x < 4.0f || b.y - a.y < 4.0f || cw <= 0 || ch <= 0)
        return;
    const float k = (b.x - a.x) / static_cast<float>(cw);
    const std::string tag = kPanelTag[static_cast<int>(panel)];
    Drag& d = g.drag;

    const float margin = dp(30.0f);
    const ImVec2 winMin = ImGui::GetWindowPos(), winSize = ImGui::GetWindowSize();
    const ImVec2 areaMin(std::max(a.x - margin, winMin.x), std::max(a.y - margin, winMin.y));
    const ImVec2 areaMax(std::min(b.x + margin, winMin.x + winSize.x), std::min(b.y + margin, winMin.y + winSize.y));
    ImGui::SetCursorScreenPos(areaMin);
    ImGui::InvisibleButton(("##transf_area_" + tag).c_str(), ImVec2(std::max(areaMax.x - areaMin.x, 1.0f), std::max(areaMax.y - areaMin.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft);
    for (ImGuiKey key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiMod_Alt})
        ImGui::SetItemKeyOwner(key);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    const bool deactivated = ImGui::IsItemDeactivated();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 clipMin = areaMin, clipMax = areaMax;
    const ImVec2 mouse = io.MousePos;
    qa::noteRect(std::string("cmp_") + tag + ":imagem", a.x, a.y, b.x, b.y);

    auto toScreen = [&](Point2 p) { return ImVec2(a.x + p.x * k, a.y + p.y * k); };
    auto toCanvas = [&](ImVec2 m) { return Point2{(m.x - a.x) / k, (m.y - a.y) / k}; };

    int li = -1;
    LayerTransform* tr = nullptr;
    bool editable = false;
    const bool crop = g.crop;
    Xform boxX, handleX;
    auto refresh = [&] {
        li = selectedLayer(ctx);
        tr = li >= 0 ? comp.transformRef(li) : nullptr;
        editable = tr && layerShown(comp, li);
        if (editable) {
            boxX = toXform(tr->box(cw, ch));
            handleX = crop ? cropXform(*tr, cw, ch) : boxX;
        }
    };
    refresh();
    const float rotDist = dp(26.0f) / k;
    auto handleScreen = [&](int h) { return toScreen(mapedit::handlePoint(handleX, h)); };
    auto rotateScreen = [&]() { return toScreen(mapedit::rotateHandlePoint(handleX, rotDist)); };
    auto hitResize = [&](ImVec2 m) {
        int best = -1;
        float bestD = handleRadius() + dp(2.0f);
        for (int h = 0; h < 8; ++h) {
            const float dd = dist(handleScreen(h), m);
            if (dd < bestD) {
                bestD = dd;
                best = h;
            }
        }
        return best;
    };
    auto hitRotate = [&](ImVec2 m) { return !crop && dist(rotateScreen(), m) <= handleRadius() + dp(2.0f); };

    if (activated && d.kind == DragKind::None) {
        const Point2 p = toCanvas(mouse);
        if (io.MouseClickedCount[ImGuiMouseButton_Left] >= 2) {
            int target = (editable && tr->contains(p.x, p.y, cw, ch)) ? li : comp.layerAtPoint(p.x, p.y);
            if (target >= 0) {
                selectLayer(ctx, target);
                comp.resetTransform(target);
            }
        } else if (editable && hitRotate(mouse)) {
            d = Drag{};
            d.kind = DragKind::Rotate;
        } else if (editable && hitResize(mouse) >= 0) {
            const int h = hitResize(mouse);
            d = Drag{};
            d.kind = crop ? DragKind::Crop : DragKind::Resize;
            d.handle = h;
        } else {
            int hit = comp.layerAtPoint(p.x, p.y);
            if (io.KeyCtrl && editable && tr->contains(p.x, p.y, cw, ch))
                hit = li;
            if (hit >= 0 && hit != li) {
                selectLayer(ctx, hit);
                refresh();
            }
            if (hit >= 0 && tr) {
                d = Drag{};
                d.kind = DragKind::Move;
            }
        }
        if (d.kind != DragKind::None) {
            d.owner = panel;
            d.layerId = comp.layers[static_cast<size_t>(li)].id;
            d.startMouse = mouse;
            d.startCanvas = p;
            d.start = *tr;
            d.startX = toXform(tr->box(cw, ch));
            d.moved = false;
        }
    }
    refresh();

    if (d.kind != DragKind::None && d.owner == panel) {
        const int di = comp.findLayerById(d.layerId);
        LayerTransform* dt = di >= 0 ? comp.transformRef(di) : nullptr;
        if (!dt || (!active && !deactivated)) {
            d = Drag{};
        } else {
            if (!d.moved && dist(mouse, d.startMouse) > dp(3.0f))
                d.moved = true;
            const float rawDx = (mouse.x - d.startMouse.x) / k, rawDy = (mouse.y - d.startMouse.y) / k;
            const snap::Options so = snapOptions(k);
            d.snap = {};
            if (d.moved) {
                if (d.kind == DragKind::Move) {
                    const float dx = std::round(rawDx), dy = std::round(rawDy);
                    Point2 c[4];
                    mapedit::toCorners(d.startX, c);
                    snap::Moving m;
                    for (const Point2& p : c) {
                        m.xs.push_back(p.x + dx);
                        m.ys.push_back(p.y + dy);
                        m.points.push_back({p.x + dx, p.y + dy});
                    }
                    m.xs.push_back(d.startX.cx + dx);
                    m.ys.push_back(d.startX.cy + dy);
                    const snap::RectF bb = snap::boundsOf(c);
                    m.extent = {bb.x0 + dx, bb.y0 + dy, bb.x1 + dx, bb.y1 + dy};
                    snap::Result r = snap::snapMoving(m, buildTargets(comp, di), so);
                    Xform n = d.startX;
                    n.cx += dx + r.dx;
                    n.cy += dy + r.dy;
                    LayerTransform nt = d.start;
                    nt.setBox(toBox(n), cw, ch);
                    if (nt.rotation == 0.0f) {
                        nt.x = std::round(nt.x);
                        nt.y = std::round(nt.y);
                    }
                    *dt = nt;
                    d.snap = std::move(r);
                } else if (d.kind == DragKind::Resize) {
                    const Xform& s0 = d.startX;
                    const bool axisAligned = s0.rot == 0.0f;
                    const float dx = axisAligned ? std::round(rawDx) : rawDx, dy = axisAligned ? std::round(rawDy) : rawDy;
                    Point2 local = mapedit::toLocal(s0, dx, dy);
                    mapedit::ResizeOptions ro;
                    ro.keepAspect = io.KeyShift;
                    ro.fromCenter = io.KeyAlt;
                    if (axisAligned && !ro.keepAspect && !ro.fromCenter) {
                        snap::Moving m;
                        if (mapedit::handleLeft(d.handle)) m.xs.push_back(s0.x() + local.x);
                        if (mapedit::handleRight(d.handle)) m.xs.push_back(s0.x() + s0.w + local.x);
                        if (mapedit::handleTop(d.handle)) m.ys.push_back(s0.y() + local.y);
                        if (mapedit::handleBottom(d.handle)) m.ys.push_back(s0.y() + s0.h + local.y);
                        m.extent = {s0.x(), s0.y(), s0.x() + s0.w, s0.y() + s0.h};
                        const snap::Result r = snap::snapMoving(m, buildTargets(comp, di), so);
                        local.x += r.dx;
                        local.y += r.dy;
                        d.snap = r;
                    }
                    Xform n = mapedit::resizeXform(s0, d.handle, local.x, local.y, ro);
                    if (axisAligned)
                        mapedit::roundToPixels(n);
                    LayerTransform nt = d.start;
                    nt.setBox(toBox(n), cw, ch);
                    nt.clampCrop(cw, ch);
                    *dt = nt;
                } else if (d.kind == DragKind::Rotate) {
                    const float bw = d.start.width(cw), bh = d.start.height(ch);
                    const Point2 pivot{d.start.x + d.start.anchorX * bw, d.start.y + d.start.anchorY * bh};
                    float ang = mapedit::rotateAngle(pivot, d.startCanvas, toCanvas(mouse), mapedit::degToRad(d.start.rotation),
                                                     io.KeyShift ? mapedit::degToRad(15.0f) : 0.0f);
                    if (std::abs(ang) < 1.0e-4f)
                        ang = 0.0f;
                    d.angle = ang;
                    LayerTransform nt = d.start;
                    nt.rotation = mapedit::radToDeg(ang);
                    *dt = nt.settled(cw, ch);
                } else if (d.kind == DragKind::Crop) {
                    const Point2 loc = mapedit::toLocal(d.startX, std::round(rawDx), std::round(rawDy));
                    LayerTransform nt = d.start;
                    const float bw = nt.width(cw), bh = nt.height(ch);
                    if (mapedit::handleLeft(d.handle))
                        nt.cropL = std::clamp(std::round(d.start.cropL + loc.x), 0.0f, std::max(0.0f, bw - d.start.cropR - 1.0f));
                    if (mapedit::handleRight(d.handle))
                        nt.cropR = std::clamp(std::round(d.start.cropR - loc.x), 0.0f, std::max(0.0f, bw - d.start.cropL - 1.0f));
                    if (mapedit::handleTop(d.handle))
                        nt.cropT = std::clamp(std::round(d.start.cropT + loc.y), 0.0f, std::max(0.0f, bh - d.start.cropB - 1.0f));
                    if (mapedit::handleBottom(d.handle))
                        nt.cropB = std::clamp(std::round(d.start.cropB - loc.y), 0.0f, std::max(0.0f, bh - d.start.cropT - 1.0f));
                    *dt = nt;
                }
            }
            if (d.kind == DragKind::Resize || d.kind == DragKind::Crop)
                ImGui::SetMouseCursor(resizeCursor(d.handle, d.startX.rot));
            else if (d.kind == DragKind::Rotate)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (deactivated) {
                g.guides = 0;
                d = Drag{};
            }
        }
    } else if (d.kind != DragKind::None && d.owner != panel) {
    } else if (hovered && editable) {
        if (hitRotate(mouse))
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (const int h = hitResize(mouse); h >= 0)
            ImGui::SetMouseCursor(resizeCursor(h, handleX.rot));
    }

    if (focused && editable && !io.WantTextInput && !popupOpen() && d.kind == DragKind::None && !io.KeyCtrl && !io.KeyAlt) {
        const float step = io.KeyShift ? 10.0f : 1.0f;
        float dx = 0.0f, dy = 0.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dx -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) dy -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) dy += step;
        if ((dx != 0.0f || dy != 0.0f) && tr) {
            tr->x += dx;
            tr->y += dy;
            *tr = tr->settled(cw, ch);
        }
    }

    refresh();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(clipMin, clipMax, true);
    const float line1 = std::max(1.0f, dp(1.5f));
    const float line2 = std::max(1.0f, dp(2.0f));
    ImFont* body = fonts().body;

    for (int i = 0; i < comp.layerCount(); ++i) {
        if (i == li || !layerShown(comp, i))
            continue;
        Point2 c[4];
        mapedit::toCorners(toXform(comp.transformRef(i)->box(cw, ch)), c);
        ImVec2 pts[4];
        for (int n = 0; n < 4; ++n)
            pts[n] = toScreen(c[n]);
        dl->AddPolyline(pts, 4, color(theme::kText, 0.35f), ImDrawFlags_Closed, std::max(1.0f, dp(1.0f)));
        const std::string num = std::to_string(i + 1);
        dl->AddText(body, fontPx(body), ImVec2(std::min({pts[0].x, pts[1].x, pts[2].x, pts[3].x}) + dp(5.0f),
                                               std::min({pts[0].y, pts[1].y, pts[2].y, pts[3].y}) + dp(3.0f)),
                    color(theme::kText, 0.6f), num.c_str());
    }

    if (editable) {
        Point2 c[4];
        mapedit::toCorners(boxX, c);
        ImVec2 pts[4];
        for (int n = 0; n < 4; ++n)
            pts[n] = toScreen(c[n]);
        dl->AddPolyline(pts, 4, color(theme::kAccent), ImDrawFlags_Closed, line2);
        const std::string num = std::to_string(li + 1);
        dl->AddText(body, fontPx(body), ImVec2(std::min({pts[0].x, pts[1].x, pts[2].x, pts[3].x}) + dp(5.0f),
                                               std::min({pts[0].y, pts[1].y, pts[2].y, pts[3].y}) + dp(3.0f)),
                    color(theme::kAccent), num.c_str());

        const bool cropped = tr->cropL + tr->cropT + tr->cropR + tr->cropB > 0.0f;
        if (cropped || crop) {
            Point2 cc[4];
            mapedit::toCorners(cropXform(*tr, cw, ch), cc);
            ImVec2 cp[4];
            for (int n = 0; n < 4; ++n)
                cp[n] = toScreen(cc[n]);
            dl->AddConvexPolyFilled(cp, 4, color(theme::kAccent, crop ? 0.10f : 0.06f));
            for (int n = 0; n < 4; ++n)
                dashedLine(dl, cp[n], cp[(n + 1) % 4], color(theme::kWarning, 0.9f), dp(5.0f), dp(3.0f), line1);
        }

        const float hs = dp(4.0f);
        for (int h = 0; h < 8; ++h) {
            const ImVec2 p = handleScreen(h);
            dl->AddRectFilled(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), color(theme::kText));
            dl->AddRect(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), color(crop ? theme::kWarning : theme::kAccent), 0.0f,
                        0, line1);
        }
        if (!crop) {
            const ImVec2 top = handleScreen(1), rp = rotateScreen();
            dl->AddLine(top, rp, color(theme::kAccent), line1);
            dl->AddCircleFilled(rp, dp(5.0f), color(theme::kText));
            dl->AddCircle(rp, dp(5.0f), color(theme::kAccent), 0, line1);
        }
    }

    if (d.kind != DragKind::None) {
        const float gl = std::max(1.0f, dp(1.0f));
        const float ext = dp(28.0f);
        for (const snap::Guide& gd : d.snap.guides) {
            ImVec2 p0 = gd.vertical ? toScreen({gd.pos, gd.lo}) : toScreen({gd.lo, gd.pos});
            ImVec2 p1 = gd.vertical ? toScreen({gd.pos, gd.hi}) : toScreen({gd.hi, gd.pos});
            if (gd.vertical) {
                p0.y -= ext;
                p1.y += ext;
            } else {
                p0.x -= ext;
                p1.x += ext;
            }
            dl->AddLine(p0, p1, color(theme::kAccent), gl);
        }
        for (const Point2& mk : d.snap.marks) {
            const ImVec2 p = toScreen(mk);
            dl->AddCircle(p, dp(6.5f), color(theme::kAccent), 0, std::max(1.0f, dp(1.5f)));
        }
        if (d.kind == DragKind::Rotate) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f°", mapedit::radToDeg(d.angle));
            pushMono();
            dl->AddText(ImVec2(mouse.x + dp(14.0f), mouse.y + dp(10.0f)), color(theme::kText), buf);
            popMono();
        }
    }
    dl->PopClipRect();
    g.guides = static_cast<int>(d.snap.guides.size());

    if (li < 0) {
        const char* hint = "Clique numa camada para transformá-la";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        const ImVec2 p(a.x + dp(8.0f), b.y - ts.y - dp(8.0f));
        dl->AddRectFilled(ImVec2(p.x - dp(5.0f), p.y - dp(2.0f)), ImVec2(p.x + ts.x + dp(5.0f), p.y + ts.y + dp(2.0f)),
                          color(theme::kBackground, 0.8f), rounding());
        dl->AddText(p, color(theme::kTextDim), hint);
    } else if (!editable) {
        const char* hint = "Esta camada não tem imagem no canvas agora";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        const ImVec2 p(a.x + dp(8.0f), b.y - ts.y - dp(8.0f));
        dl->AddRectFilled(ImVec2(p.x - dp(5.0f), p.y - dp(2.0f)), ImVec2(p.x + ts.x + dp(5.0f), p.y + ts.y + dp(2.0f)),
                          color(theme::kBackground, 0.8f), rounding());
        dl->AddText(p, color(theme::kWarning), hint);
    }
}

void transformHistoryObserve(PanelContext& ctx)
{
    const ImGuiIO& io = ImGui::GetIO();
    const bool gesture = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || io.WantTextInput
                         || g.drag.kind != DragKind::None;
    g.history.observe(ctx.composition, gesture);

    if (io.WantTextInput || popupOpen() || g.drag.kind != DragKind::None || !GImGui || !GImGui->NavWindow)
        return;
    const ImGuiWindow* nav = GImGui->NavWindow;
    bool mine = false;
    for (const char* name : {panel::kOutput, panel::kPreview, panel::kLayerTab}) {
        const ImGuiWindow* w = ImGui::FindWindowByName(name);
        mine = mine || (w && (nav == w || nav->RootWindow == w));
    }
    if (!mine || !io.KeyCtrl)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        if (io.KeyShift)
            g.history.redo(ctx.composition);
        else
            g.history.undo(ctx.composition);
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
        g.history.redo(ctx.composition);
}

void transformEditorReset(const Composition& comp)
{
    g.history.reset(comp);
    g.drag = Drag{};
}

TransformEditorInfo transformEditorInfo()
{
    TransformEditorInfo i;
    i.onOutput = g.on[0];
    i.onPreview = g.on[1];
    i.crop = g.crop;
    i.snap = g.snapOn;
    i.undo = static_cast<int>(g.history.undoCount());
    i.redo = static_cast<int>(g.history.redoCount());
    i.dragging = g.drag.kind != DragKind::None;
    i.drag = dragName(g.drag.kind);
    i.guides = g.guides;
    return i;
}

void drawClipTransformToggle(PanelContext& ctx, Clip& clip, int layerIndex)
{
    bool own = clip.ownTransform;
    if (ImGui::Checkbox("Enquadramento próprio do clip##clip_transform_proprio", &own)) {
        if (own && layerIndex >= 0 && layerIndex < ctx.composition.layerCount() && clip.transform.isDefault())
            clip.transform = ctx.composition.layers[static_cast<size_t>(layerIndex)].transform;
        clip.ownTransform = own;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ligado: enquanto este clip toca, o Transform dele (alças da Saída/Prévia e números da aba Camada) "
                          "vence o da camada. O da camada continua guardado");
}

void drawTransformFields(PanelContext& ctx, int layerIndex)
{
    Composition& comp = ctx.composition;
    if (layerIndex < 0 || layerIndex >= comp.layerCount())
        return;
    const int cw = comp.width, ch = comp.height;
    Clip* active = comp.activeClip(layerIndex);

    ImGui::SeparatorText("Transform");
    if (active) {
        drawClipTransformToggle(ctx, *active, layerIndex);
    }
    LayerTransform* tr = comp.transformRef(layerIndex);
    if (!tr)
        return;
    LayerTransform t = *tr;
    bool changed = false;

    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f / uiScale();

    auto pair = [&](const char* label, const char* idA, float& va, float loA, float hiA, const char* idB, float& vb, float loB,
                    float hiB, const char* unit, float defA, float defB) {
        dimLabel(label);
        bool c = hotNumber(idA, va, loA, hiA, 1.0f, "%.0f", unit, defA, half);
        ImGui::SameLine();
        c = hotNumber(idB, vb, loB, hiB, 1.0f, "%.0f", unit, defB, -1.0f) || c;
        return c;
    };

    const int dmxLayerId = dmxLayerIdOf(layerIndex);
    dimLabel("Posição (X, Y)");
    hotDmx({dmx::ParamKind::LayerPosX, dmxLayerId});
    changed |= hotNumber("##tr_x", t.x, -16384.0f, 16384.0f, 1.0f, "%.0f", "px", 0.0f, half);
    ImGui::SameLine();
    hotDmx({dmx::ParamKind::LayerPosY, dmxLayerId});
    changed |= hotNumber("##tr_y", t.y, -16384.0f, 16384.0f, 1.0f, "%.0f", "px", 0.0f, -1.0f);

    float bw = t.width(cw), bh = t.height(ch);
    const float oldW = bw, oldH = bh;
    dimLabel("Tamanho (L, A)");
    bool sizeChanged = hotNumber("##tr_w", bw, 1.0f, 32768.0f, 1.0f, "%.0f", "px", static_cast<float>(cw), half);
    ImGui::SameLine();
    sizeChanged = hotNumber("##tr_h", bh, 1.0f, 32768.0f, 1.0f, "%.0f", "px", static_cast<float>(ch), -1.0f) || sizeChanged;
    if (sizeChanged) {
        if (g.lockAspect) {
            if (bw != oldW && oldW > 0.0f)
                bh = std::max(1.0f, std::round(oldH * bw / oldW));
            else if (bh != oldH && oldH > 0.0f)
                bw = std::max(1.0f, std::round(oldW * bh / oldH));
        }
        t.w = bw;
        t.h = bh;
        changed = true;
    }
    {
        float scalePct = 100.0f * t.width(cw) / static_cast<float>(cw);
        dimLabel("Escala uniforme");
        hotDmx({dmx::ParamKind::LayerScale, dmxLayerId});
        if (hotNumber("##tr_esc", scalePct, 1.0f, 1000.0f, 0.5f, "%.0f", "%", 100.0f, half)) {
            const float k = std::max(0.01f, scalePct / 100.0f);
            const float nw = std::max(1.0f, std::round(static_cast<float>(cw) * k));
            const float ratio = nw / std::max(1.0f, t.width(cw));
            t.h = std::max(1.0f, std::round(t.height(ch) * ratio));
            t.w = nw;
            changed = true;
        }
        ImGui::SameLine();
        iconToggle(g.lockAspect ? ICON_LOCK : ICON_LOCK_OPEN, "##tr_proporcao", g.lockAspect,
                   "Manter a proporção ao digitar L ou A");
    }
    dimLabel("Rotação");
    hotDmx({dmx::ParamKind::LayerRotation, dmxLayerId});
    changed |= hotNumber("##tr_rot", t.rotation, -360.0f, 360.0f, 0.5f, "%.1f", "°", 0.0f, half);
    {
        float ax = t.anchorX * 100.0f, ay = t.anchorY * 100.0f;
        dimLabel("Âncora (X, Y)");
        bool c = hotNumber("##tr_ax", ax, 0.0f, 100.0f, 0.5f, "%.0f", "%", 50.0f, half);
        ImGui::SameLine();
        c = hotNumber("##tr_ay", ay, 0.0f, 100.0f, 0.5f, "%.0f", "%", 50.0f, -1.0f) || c;
        if (c) {
            const OrientedBox before = t.box(cw, ch);
            t.anchorX = ax / 100.0f;
            t.anchorY = ay / 100.0f;
            t.setBox(before, cw, ch);
            changed = true;
        }
    }
    changed |= pair("Recorte (esquerda, topo)", "##tr_cl", t.cropL, 0.0f, 32768.0f, "##tr_ct", t.cropT, 0.0f, 32768.0f, "px", 0.0f, 0.0f);
    changed |= pair("Recorte (direita, base)", "##tr_cr", t.cropR, 0.0f, 32768.0f, "##tr_cb", t.cropB, 0.0f, 32768.0f, "px", 0.0f, 0.0f);

    if (changed) {
        t.clampCrop(cw, ch);
        *tr = t.settled(cw, ch);
        if (tr->isIdentity(cw, ch))
            tr->rotation = 0.0f;
    }

    if (ImGui::Button("Resetar##tr_reset"))
        comp.resetTransform(layerIndex);
    ImGui::SameLine();
    if (ImGui::Button("Presets…##tr_presets"))
        ImGui::OpenPopup("##tr_presets_popup");
    if (ImGui::BeginPopup("##tr_presets_popup")) {
        drawTransformPresetItems(ctx, layerIndex);
        ImGui::EndPopup();
    }
}

}
