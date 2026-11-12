#pragma once

#include "control/ActionArgs.h"
#include "dmx/DmxEngine.h"
#include "ui/PanelContext.h"

#include <windows.h>

#include <imgui.h>

#include <string>

namespace bsl::ui {

void openDmxWindow();
void openDmxWindowTab(int tab);
void drawDmxWindow(PanelContext& ctx);

void dmxFrame(PanelContext& ctx);
void runDmxDialogs(HWND owner);

bool dmxIndicator(const char* id, ImVec2 size);

void dmxParamMenu(const dmx::ParamRef& ref);
void dmxActionMenu(const control::Target& target, bool leadingSeparator = true);
void hotDmx(const dmx::ParamRef& ref);
int dmxLayerIdOf(int layerIndex);

struct DmxParamOwner
{
    bool valid = false;
    int scope = 1;
    int layerIndex = -1;
    int deck = -1;
    int column = 0;
    int effect = 0;
};
void dmxSetParamOwner(const DmxParamOwner& owner);
bool dmxParamOwnerValid();
dmx::ParamRef dmxEffectRef(const std::string& paramId);
control::ActionArgs dmxEffectActionArgs(const std::string& paramId);
void dmxEventMenu(const control::ActionArgs& args, bool offerHold);

struct DmxUiInfo
{
    bool open = false;
    int tab = 0;
    bool learning = false;
    std::string toast;
    int blocks = 0;
    int maps = 0;
};
DmxUiInfo dmxUiInfo();

}
