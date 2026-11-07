#pragma once

#include <imgui.h>

namespace bsl::ui {

namespace theme {

constexpr ImVec4 rgb(unsigned hex, float alpha = 1.0f)
{
    return ImVec4(static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(hex & 0xFF) / 255.0f, alpha);
}

constexpr unsigned kAccentHex = 0xEC3337;

constexpr ImVec4 kBackground = rgb(0x121212);
constexpr ImVec4 kPanel = rgb(0x1F1F1F);
constexpr ImVec4 kPanelRaised = rgb(0x2C2C2C);
constexpr ImVec4 kPanelHover = rgb(0x383838);
constexpr ImVec4 kPanelActive = rgb(0x454545);
constexpr ImVec4 kField = rgb(0x171717);
constexpr ImVec4 kBorder = rgb(0x333333);
constexpr ImVec4 kText = rgb(0xD8D8D8);
constexpr ImVec4 kTextStrong = rgb(0xF2F2F2);
constexpr ImVec4 kTextDim = rgb(0x8C8C8C);
constexpr ImVec4 kAccent = rgb(kAccentHex);
constexpr ImVec4 kAccentHover = rgb(0xF05155);
constexpr ImVec4 kAccentDim = rgb(0xB0181C);
constexpr ImVec4 kAccentSoft = rgb(kAccentHex, 0.18f);
constexpr ImVec4 kOnAccent = rgb(0xFFFFFF);

constexpr ImVec4 kSuccess = rgb(0x1EA75A);
constexpr ImVec4 kWarning = rgb(0xF5A524);
constexpr ImVec4 kError = rgb(0xFF4D4F);
constexpr ImVec4 kInfo = rgb(0x2F7FE0);

constexpr float kRounding = 4.0f;
constexpr float kRoundingWindow = 6.0f;
constexpr float kSpacing = 8.0f;
constexpr float kPadding = 10.0f;
constexpr float kPanelGap = 4.0f;
constexpr float kFontSize = 16.0f;
constexpr float kMonoSize = 14.0f;

}

void applyTheme(float scale = 1.0f);

float uiScale();
inline float dp(float px) { return px * uiScale(); }
inline ImVec2 dp(float x, float y) { return ImVec2(x * uiScale(), y * uiScale()); }

inline float rounding() { return dp(theme::kRounding); }
inline float padding() { return dp(theme::kPadding); }

inline float fontPx(const ImFont* font) { return font->LegacySize * uiScale(); }

void loadFont(ImGuiIO& io);

struct Fonts {
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* mono = nullptr;
};
const Fonts& fonts();

}
