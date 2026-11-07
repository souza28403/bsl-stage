#pragma once

#include "ui/PanelContext.h"
#include "ui/ViewState.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace bsl::ui {

void viewBind(Settings& settings);

bool panelVisible(const char* window);
bool panelVisibleKey(const char* key);
void setPanelVisible(const char* key, bool visible);
void showAllPanels(AppMode mode);
bool statusBarVisible();

bool layoutLocked();
void setLayoutLocked(bool locked);
ImGuiDockNodeFlags viewDockSpaceFlags();

void requestDetachPanel(const char* key, bool detach);
bool panelDetached(const char* key);

void requestFullscreen(bool on);
int takeFullscreenRequest();
void setFullscreenState(bool on);
bool isFullscreen();

bool viewBegin(const char* name, ImGuiWindowFlags flags);

void viewFrame();

void drawPanelMenuItems(PanelContext& ctx);
void drawDetachMenu(PanelContext& ctx);
void drawWindowMenuItems(PanelContext& ctx);

struct ViewInfo
{
    struct PanelRow
    {
        std::string key;
        bool visible = true;
        bool detached = false;
        bool seen = false;
        bool docked = false;
        float rect[4] = {0, 0, 0, 0};
    };
    std::vector<PanelRow> panels;
    bool locked = false;
    bool fullscreen = false;
    int viewports = 1;
    std::vector<std::string> hidden;
    std::vector<std::string> detached;
    float mainPos[2] = {0, 0};
};
ViewInfo viewInfo();

}
