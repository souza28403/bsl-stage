#pragma once

#include "ui/PanelContext.h"

#include <string>
#include <vector>

#include <windows.h>

namespace bsl::ui {

void drawLayoutPresetsMenu(PanelContext& ctx);
bool applyLayoutPresetIndex(int index);
void applyPendingLayout(PanelContext& ctx);
void drawLayoutWindows(PanelContext& ctx);
void runLayoutDialogs(HWND owner);
void clearActiveLayoutPreset(PanelContext& ctx);

struct LayoutUiInfo
{
    std::vector<std::string> presets;
};
LayoutUiInfo layoutUiInfo();

}
