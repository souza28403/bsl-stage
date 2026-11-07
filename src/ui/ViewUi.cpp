#include "ui/ViewUi.h"

#include "app/Settings.h"
#include "ui/KeysUi.h"
#include "ui/LayoutPreset.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui_internal.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <map>
#include <string>

namespace bsl::ui {

namespace {

using view::PanelDef;
using view::PanelMode;

constexpr ImGuiID kPixelDockRoot = 0x42534C50;

Settings* g_settings = nullptr;
bool g_dirty = false;
bool g_hintsDirty = false;
double g_hintsStamp = 0.0;
int g_frame = 0;

std::map<std::string, int> g_focusPending;
std::map<std::string, int> g_request;

int g_fullscreenRequest = -1;
bool g_fullscreen = false;

struct Runtime
{
    bool seen = false;
    bool detached = false;
    bool docked = false;
    int frame = -1;
    ImVec2 pos{0.0f, 0.0f};
    ImVec2 size{0.0f, 0.0f};
};
std::map<std::string, Runtime> g_runtime;

PanelMode modeOf(AppMode mode)
{
    return mode == AppMode::Mapping ? PanelMode::Mapping : mode == AppMode::PixelMap ? PanelMode::PixelMap : PanelMode::Composition;
}

ImGuiID rootOf(PanelMode mode)
{
    return mode == PanelMode::Mapping ? layout::kMapDockRoot : mode == PanelMode::PixelMap ? kPixelDockRoot : layout::kCompDockRoot;
}

bool inMainDockspace(const ImGuiDockNode* node)
{
    return node && node->HostWindow && node->HostWindow->Viewport == ImGui::GetMainViewport();
}

ImRect panelArea()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return ImRect(vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y));
}

ImGuiID dockTargetFor(const PanelDef& def)
{
    view::State& vs = g_settings->view;
    if (const auto it = vs.dockIds.find(def.key); it != vs.dockIds.end()) {
        const ImGuiDockNode* node = ImGui::DockBuilderGetNode(it->second);
        if (node && node->IsLeafNode() && inMainDockspace(node))
            return it->second;
    }
    const auto hint = vs.dockRects.find(def.key);
    if (hint != vs.dockRects.end()) {
        const ImRect area = panelArea();
        const ImRect want(ImVec2(area.Min.x + hint->second[0] * area.GetWidth(), area.Min.y + hint->second[1] * area.GetHeight()),
                          ImVec2(area.Min.x + (hint->second[0] + hint->second[2]) * area.GetWidth(),
                                 area.Min.y + (hint->second[1] + hint->second[3]) * area.GetHeight()));
        ImGuiDockNode* best = nullptr;
        float bestOverlap = 0.0f;
        for (const PanelDef& other : view::kPanels) {
            if (other.mode != def.mode || std::string(other.key) == def.key)
                continue;
            const ImGuiWindow* w = ImGui::FindWindowByName(other.window);
            ImGuiDockNode* node = (w && w->Viewport == ImGui::GetMainViewport()) ? w->DockNode : nullptr;
            if (!node || !node->IsLeafNode() || !inMainDockspace(node))
                continue;
            const ImRect box = node->Rect();
            const float ow = std::min(box.Max.x, want.Max.x) - std::max(box.Min.x, want.Min.x);
            const float oh = std::min(box.Max.y, want.Max.y) - std::max(box.Min.y, want.Min.y);
            if (ow > 0.0f && oh > 0.0f && ow * oh > bestOverlap) {
                bestOverlap = ow * oh;
                best = node;
            }
        }
        if (best) {
            const ImRect box = best->Rect();
            const ImVec2 dc((want.Min.x + want.Max.x) * 0.5f - (box.Min.x + box.Max.x) * 0.5f,
                            (want.Min.y + want.Max.y) * 0.5f - (box.Min.y + box.Max.y) * 0.5f);
            const bool vertical = std::abs(dc.y) / std::max(1.0f, box.GetHeight()) >= std::abs(dc.x) / std::max(1.0f, box.GetWidth());
            const ImGuiDir dir = vertical ? (dc.y < 0.0f ? ImGuiDir_Up : ImGuiDir_Down) : (dc.x < 0.0f ? ImGuiDir_Left : ImGuiDir_Right);
            const float along = vertical ? want.GetHeight() / std::max(1.0f, box.GetHeight()) : want.GetWidth() / std::max(1.0f, box.GetWidth());
            ImGuiID mine = 0;
            ImGuiID other = 0;
            ImGui::DockBuilderSplitNode(best->ID, dir, std::clamp(along, 0.15f, 0.85f), &mine, &other);
            if (mine)
                return mine;
        }
    }
    for (const PanelDef& other : view::kPanels) {
        if (other.mode != def.mode || std::string(other.key) == def.key)
            continue;
        const ImGuiWindow* w = ImGui::FindWindowByName(other.window);
        if (w && w->DockNode && w->DockId && w->Viewport == ImGui::GetMainViewport())
            return w->DockId;
    }
    return rootOf(def.mode);
}

void markDirty()
{
    g_dirty = true;
}

void rescueViewports()
{
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    if (pio.Monitors.Size == 0)
        return;
    const ImGuiViewport* main = ImGui::GetMainViewport();
    for (int i = 0; i < pio.Viewports.Size; ++i) {
        ImGuiViewport* vp = pio.Viewports[i];
        if (vp == main || !vp->PlatformHandle || (vp->Flags & ImGuiViewportFlags_NoTaskBarIcon))
            continue;
        if (vp->Size.x < 1.0f || vp->Size.y < 1.0f)
            continue;
        const ImVec2 probe(vp->Pos.x + std::min(vp->Size.x * 0.5f, dp(120.0f)), vp->Pos.y + dp(12.0f));
        bool visible = false;
        for (const ImGuiPlatformMonitor& m : pio.Monitors)
            if (probe.x >= m.MainPos.x && probe.x < m.MainPos.x + m.MainSize.x && probe.y >= m.MainPos.y
                && probe.y < m.MainPos.y + m.MainSize.y)
                visible = true;
        if (visible)
            continue;
        ImGuiWindow* w = static_cast<ImGuiViewportP*>(vp)->Window;
        if (!w)
            continue;
        const ImVec2 size(std::min(vp->Size.x, main->Size.x * 0.8f), std::min(vp->Size.y, main->Size.y * 0.8f));
        ImGui::SetWindowPos(w, ImVec2(main->Pos.x + dp(60.0f), main->Pos.y + dp(80.0f)), ImGuiCond_Always);
        ImGui::SetWindowSize(w, size, ImGuiCond_Always);
    }
}

}

void viewBind(Settings& settings)
{
    g_settings = &settings;
}

bool panelVisibleKey(const char* key)
{
    return !g_settings || !g_settings->view.isHidden(key);
}

bool panelVisible(const char* window)
{
    const PanelDef* def = view::findPanelByWindow(window);
    return !def || panelVisibleKey(def->key);
}

void setPanelVisible(const char* key, bool visible)
{
    if (g_settings && g_settings->view.setHidden(key, !visible))
        markDirty();
}

void showAllPanels(AppMode mode)
{
    if (g_settings && g_settings->view.showAll(modeOf(mode)))
        markDirty();
}

bool statusBarVisible()
{
    return panelVisibleKey("status");
}

bool layoutLocked()
{
    return g_settings && g_settings->view.locked;
}

void setLayoutLocked(bool locked)
{
    if (g_settings && g_settings->view.locked != locked) {
        g_settings->view.locked = locked;
        markDirty();
    }
}

ImGuiDockNodeFlags viewDockSpaceFlags()
{
    if (!layoutLocked())
        return ImGuiDockNodeFlags_None;
    return ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit
           | ImGuiDockNodeFlags_NoDockingOverMe;
}

void requestDetachPanel(const char* key, bool detach)
{
    if (!view::findPanel(key))
        return;
    g_request[key] = detach ? 1 : -1;
    if (detach)
        setPanelVisible(key, true);
}

bool panelDetached(const char* key)
{
    const auto it = g_runtime.find(key);
    return it != g_runtime.end() && it->second.frame >= g_frame - 1 && it->second.detached;
}

void requestFullscreen(bool on)
{
    g_fullscreenRequest = on ? 1 : 0;
}

int takeFullscreenRequest()
{
    const int r = g_fullscreenRequest;
    g_fullscreenRequest = -1;
    return r;
}

void setFullscreenState(bool on)
{
    g_fullscreen = on;
}

bool isFullscreen()
{
    return g_fullscreen;
}

bool viewBegin(const char* name, ImGuiWindowFlags flags)
{
    const PanelDef* def = g_settings ? view::findPanelByWindow(name) : nullptr;
    if (!def)
        return ImGui::Begin(name, nullptr, flags);

    view::State& vs = g_settings->view;
    const std::string key = def->key;
    const ImGuiViewport* mainVp = ImGui::GetMainViewport();
    const ImGuiWindow* prev = ImGui::FindWindowByName(name);
    const bool locked = vs.locked;

    int request = 0;
    if (const auto it = g_request.find(key); it != g_request.end()) {
        request = it->second;
        g_request.erase(it);
    }
    bool ownWindow = vs.isDetached(key);
    if (request > 0) {
        ownWindow = true;
        int frameTop = 0, ignored = 0;
        if (auto* mainWindow = static_cast<GLFWwindow*>(mainVp->PlatformHandle))
            glfwGetWindowFrameSize(mainWindow, &ignored, &frameTop, &ignored, &ignored);
        const float minY = mainVp->Pos.y + ImGui::GetFrameHeight() + static_cast<float>(frameTop) + dp(12.0f);
        ImVec2 pos = prev ? ImVec2(prev->Pos.x + dp(28.0f), prev->Pos.y + dp(28.0f))
                          : ImVec2(mainVp->Pos.x + dp(80.0f), mainVp->Pos.y + dp(80.0f));
        pos.y = std::max(pos.y, minY);
        const ImVec2 size = prev ? ImVec2(std::max(prev->Size.x, dp(360.0f)), std::max(prev->Size.y, dp(220.0f))) : dp(640.0f, 400.0f);
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    } else if (request < 0) {
        ownWindow = false;
        vs.setDetached(key, false);
        markDirty();
        ImGui::SetNextWindowDockID(dockTargetFor(*def), ImGuiCond_Always);
    }
    if (ownWindow) {
        ImGuiWindowClass cls;
        cls.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&cls);
    }

    const bool floating = !prev || !prev->DockNode;
    if (ownWindow && floating)
        flags |= ImGuiWindowFlags_NoTitleBar;
    if (locked) {
        flags |= ImGuiWindowFlags_NoMove;
        if (prev && prev->WasActive && !prev->DockNode && !ownWindow)
            flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;
    }

    const bool squareCorners = ownWindow && floating;
    if (squareCorners)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    bool open = true;
    const bool shown = ImGui::Begin(name, locked ? nullptr : &open, flags);
    if (squareCorners)
        ImGui::PopStyleVar();
    const ImGuiWindow* w = ImGui::GetCurrentWindow();

    Runtime& rt = g_runtime[key];
    rt.seen = true;
    rt.frame = g_frame;
    rt.detached = w->Viewport && w->Viewport != mainVp;
    rt.docked = w->DockNode != nullptr;
    rt.pos = w->DockNode ? w->DockNode->Pos : w->Pos;
    rt.size = w->DockNode ? w->DockNode->Size : w->Size;
    if (rt.detached) {
        if (vs.setDetached(key, true))
            markDirty();
    } else if (w->DockNode) {
        if (vs.setDetached(key, false))
            markDirty();
        if (w->DockId && vs.setDockId(key, w->DockId))
            markDirty();
        const ImRect area = panelArea();
        const ImRect box = w->DockNode->Rect();
        if (area.GetWidth() > 1.0f && area.GetHeight() > 1.0f && w->DockNode->IsLeafNode()
            && vs.setDockRect(key, {(box.Min.x - area.Min.x) / area.GetWidth(), (box.Min.y - area.Min.y) / area.GetHeight(),
                                    box.GetWidth() / area.GetWidth(), box.GetHeight() / area.GetHeight()}))
            g_hintsDirty = true;
    }
    if (request > 0)
        g_focusPending[key] = 30;
    if (const auto it = g_focusPending.find(key); it != g_focusPending.end()) {
        if (rt.detached && w->Viewport->PlatformHandle) {
            glfwFocusWindow(static_cast<GLFWwindow*>(w->Viewport->PlatformHandle));
            g_focusPending.erase(it);
        } else if (--it->second <= 0) {
            g_focusPending.erase(it);
        }
    }
    if (!locked && !open && vs.setHidden(key, true))
        markDirty();
    return shown;
}

void viewFrame()
{
    ++g_frame;
    if (g_hintsDirty && ImGui::GetTime() - g_hintsStamp > 1.5) {
        g_hintsStamp = ImGui::GetTime();
        g_hintsDirty = false;
        g_dirty = true;
    }
    if (g_settings && g_dirty) {
        g_dirty = false;
        g_settings->save();
    }
    if (g_frame % 30 == 0)
        rescueViewports();
}

namespace {

bool panelItem(const PanelDef& d)
{
    const bool visible = panelVisibleKey(d.key);
    const std::string label = std::string(d.label) + "##exibir_" + d.key;
    const std::string shortcut = actionShortcut(std::string("view.panel.") + d.key);
    if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), visible)) {
        setPanelVisible(d.key, !visible);
        return true;
    }
    return false;
}

}

void drawPanelMenuItems(PanelContext& ctx)
{
    const PanelMode mode = modeOf(ctx.mode);
    std::string lastGroup;
    for (const PanelDef& d : view::kPanels) {
        if (d.mode != mode)
            continue;
        if (std::string(d.group) == "abas")
            continue;
        if (!lastGroup.empty() && lastGroup != d.group)
            ImGui::Separator();
        lastGroup = d.group;
        panelItem(d);
    }
    if (mode == PanelMode::Composition && ImGui::BeginMenu("Abas##exibir_abas")) {
        for (const PanelDef& d : view::kPanels)
            if (d.mode == mode && std::string(d.group) == "abas")
                panelItem(d);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    for (const PanelDef& d : view::kPanels)
        if (d.mode == PanelMode::Any)
            panelItem(d);
    bool anyHidden = false;
    for (const PanelDef& d : view::kPanels)
        if ((d.mode == mode || d.mode == PanelMode::Any) && !panelVisibleKey(d.key))
            anyHidden = true;
    const std::string showAllShortcut = actionShortcut("view.showAll");
    if (ImGui::MenuItem(ICON_EYE "  Mostrar todos os painéis##exibir_mostrar_todos",
                        showAllShortcut.empty() ? nullptr : showAllShortcut.c_str(), false, anyHidden))
        showAllPanels(ctx.mode);
}

void drawDetachMenu(PanelContext& ctx)
{
    if (!ImGui::BeginMenu(ICON_MAXIMIZE "  Soltar em janela##exibir_soltar"))
        return;
    const PanelMode mode = modeOf(ctx.mode);
    ImGui::TextColored(theme::kTextDim, "Abre o painel numa janela do sistema (pode ir para outro monitor)");
    ImGui::Separator();
    std::string lastGroup;
    for (const PanelDef& d : view::kPanels) {
        if (d.mode != mode)
            continue;
        if (!lastGroup.empty() && lastGroup != d.group)
            ImGui::Separator();
        lastGroup = d.group;
        const bool detached = g_settings && g_settings->view.isDetached(d.key);
        const std::string label = std::string(d.label) + "##exibir_soltar_" + d.key;
        if (ImGui::MenuItem(label.c_str(), nullptr, detached))
            requestDetachPanel(d.key, !detached);
    }
    ImGui::EndMenu();
}

void drawWindowMenuItems(PanelContext&)
{
    const std::string fsShortcut = actionShortcut("view.fullscreen");
    if (ImGui::MenuItem("Tela cheia##exibir_tela_cheia", fsShortcut.empty() ? nullptr : fsShortcut.c_str(), g_fullscreen))
        requestFullscreen(!g_fullscreen);
    const std::string lockShortcut = actionShortcut("view.lock");
    if (ImGui::MenuItem(ICON_LOCK "  Bloquear layout##exibir_bloquear", lockShortcut.empty() ? nullptr : lockShortcut.c_str(),
                        layoutLocked()))
        setLayoutLocked(!layoutLocked());
}

ViewInfo viewInfo()
{
    ViewInfo info;
    if (!g_settings)
        return info;
    for (const PanelDef& d : view::kPanels) {
        ViewInfo::PanelRow row;
        row.key = d.key;
        row.visible = !g_settings->view.isHidden(d.key);
        row.detached = g_settings->view.isDetached(d.key);
        const auto it = g_runtime.find(d.key);
        if (it != g_runtime.end()) {
            row.seen = it->second.frame >= g_frame - 1;
            row.docked = it->second.docked;
            row.rect[0] = it->second.pos.x;
            row.rect[1] = it->second.pos.y;
            row.rect[2] = it->second.size.x;
            row.rect[3] = it->second.size.y;
        }
        info.panels.push_back(std::move(row));
    }
    info.locked = g_settings->view.locked;
    info.fullscreen = g_fullscreen;
    info.hidden = g_settings->view.hidden;
    info.detached = g_settings->view.detached;
    if (ImGui::GetCurrentContext()) {
        int n = 0;
        for (const ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports)
            if (vp == ImGui::GetMainViewport() || (vp->PlatformHandle && !(vp->Flags & ImGuiViewportFlags_NoTaskBarIcon)))
                ++n;
        info.viewports = n;
        const ImGuiViewport* main = ImGui::GetMainViewport();
        info.mainPos[0] = main->Pos.x;
        info.mainPos[1] = main->Pos.y;
    }
    return info;
}

}
