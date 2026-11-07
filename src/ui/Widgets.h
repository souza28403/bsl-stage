#pragma once

#include "ui/Icons.h"

#include <imgui.h>

#include <climits>
#include <cmath>
#include <functional>
#include <string>

namespace bsl::ui {

bool beginPanel(const char* name, ImGuiWindowFlags flags = 0);
void endPanel();

void dimLabel(const char* label);

bool beginCombo(const char* id, const char* preview);

void pushMono();
void popMono();
void monoText(const char* fmt, ...) IM_FMTARGS(1);

std::string formatTime(double seconds);

bool toggleButton(const char* label, bool& value, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f));

enum IconButtonFlags_ {
    IconButton_Active = 1 << 0,
    IconButton_Flat = 1 << 1,
};
bool iconButton(const char* icon, const char* id, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f), int flags = 0,
                ImU32 color = 0);
bool iconToggle(const char* icon, const char* id, bool& value, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f),
                int flags = 0);
void drawIcon(ImDrawList* dl, const char* icon, ImVec2 center, ImU32 color, float scale = 1.0f);
void drawPlayTriangle(ImDrawList* dl, ImVec2 center, float size, ImU32 color);

void drawPatternThumb(ImDrawList* dl, ImVec2 a, ImVec2 b);
void drawCheckerboard(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell = 0.0f);
void drawDashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float dash = 0.0f, float gap = 0.0f);
void drawCenteredMono(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color);
void drawCenteredFit(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color);

bool brandHeader(const char* label);

bool hotNumber(const char* id, float& v, float lo, float hi, float step, const char* format = "%.2f",
               const char* unit = nullptr, float def = NAN, float width = 0.0f);
bool hotInt(const char* id, int& v, int lo, int hi, float step = 1.0f, const char* format = "%d",
            const char* unit = nullptr, int def = INT_MIN, float width = 0.0f);
void hotContextExtra(std::function<void()> draw);
bool hotPercent(const char* id, float& value, float def = NAN, float width = 0.0f);
void splitValueFormat(const char* format, std::string& numberFormat, std::string& unit);

}
