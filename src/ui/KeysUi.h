#pragma once

#include "control/ActionArgs.h"
#include "control/ActionRegistry.h"
#include "ui/PanelContext.h"

#include <imgui.h>

#include <string>

#include <windows.h>

namespace bsl::ui {

void keysFrame(PanelContext& ctx);
void drawKeysWindows(PanelContext& ctx);
void openKeysEditor();
void runKeysDialogs(HWND owner);

control::SelectionRef selectionRef(const Selection& selection);

std::string keyHint(const control::Target& target);
float keyFlash(const control::Target& target);
float keyHintWidth(const std::string& text);
void drawKeyHint(ImDrawList* dl, ImVec2 topLeft, const std::string& text);
void drawKeyFlash(ImDrawList* dl, ImVec2 a, ImVec2 b, float amount, float rounding);

void keyAssignMenu(const control::Target& target, bool leadingSeparator = true);

std::string actionShortcut(const std::string& actionId, const control::ActionArgs& args = {});

struct KeysUiInfo
{
    bool editorOpen = false;
    bool capturing = false;
    bool conflictOpen = false;
    int rows = 0;
};
KeysUiInfo keysUiInfo();

}
