#include "ui/Theme.h"

#include "app/Paths.h"

#include <filesystem>

namespace bsl::ui {

namespace {

Fonts g_fonts;
float g_scale = 1.0f;

ImFont* tryFont(ImGuiIO& io, const std::filesystem::path& path, float size)
{
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec))
        return nullptr;
    return io.Fonts->AddFontFromFileTTF(paths::toUtf8(path).c_str(), size);
}

void mergeIcons(ImGuiIO& io, const std::filesystem::path& fontsDir, float size)
{
    if (fontsDir.empty())
        return;
    const std::filesystem::path file = fontsDir / L"lucide.ttf";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.ExtraSizeScale = 0.90f;
    cfg.GlyphOffset = ImVec2(0.0f, size * 0.12f);
    cfg.GlyphMinAdvanceX = size * 1.05f;
    io.Fonts->AddFontFromFileTTF(paths::toUtf8(file).c_str(), size, &cfg);
}

}

void loadFont(ImGuiIO& io)
{
    const std::filesystem::path fontsDir = paths::assetsDir().empty()
        ? std::filesystem::path()
        : paths::assetsDir() / L"fonts";

    g_fonts.body = tryFont(io, fontsDir.empty() ? fontsDir : fontsDir / L"Barlow-Medium.ttf", theme::kFontSize);
    if (!g_fonts.body)
        g_fonts.body = tryFont(io, L"C:\\Windows\\Fonts\\segoeui.ttf", theme::kFontSize - 1.0f);
    if (!g_fonts.body)
        g_fonts.body = io.Fonts->AddFontDefault();
    mergeIcons(io, fontsDir, theme::kFontSize);

    g_fonts.bold = tryFont(io, fontsDir.empty() ? fontsDir : fontsDir / L"Barlow-SemiBold.ttf", theme::kFontSize);
    if (g_fonts.bold)
        mergeIcons(io, fontsDir, theme::kFontSize);
    g_fonts.mono = tryFont(io, fontsDir.empty() ? fontsDir : fontsDir / L"JetBrainsMono.ttf", theme::kMonoSize);
    if (!g_fonts.bold)
        g_fonts.bold = g_fonts.body;
    if (!g_fonts.mono)
        g_fonts.mono = g_fonts.body;
}

const Fonts& fonts()
{
    return g_fonts;
}

float uiScale()
{
    return g_scale;
}

void applyTheme(float scale)
{
    using namespace theme;
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();

    s.WindowPadding = ImVec2(kPadding, 8.0f);
    s.FramePadding = ImVec2(8.0f, 4.0f);
    s.CellPadding = ImVec2(6.0f, 4.0f);
    s.ItemSpacing = ImVec2(kSpacing, 6.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 10.0f;

    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.TabBorderSize = 0.0f;
    s.TabBarBorderSize = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.DockingSeparatorSize = kPanelGap;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    s.WindowRounding = kRoundingWindow;
    s.ChildRounding = kRounding;
    s.FrameRounding = kRounding;
    s.PopupRounding = kRounding;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 8.0f;
    s.TabRounding = kRounding;

    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(0x262626);
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = kField;
    c[ImGuiCol_FrameBgHovered] = rgb(0x1C1C1C);
    c[ImGuiCol_FrameBgActive] = rgb(0x121212);
    c[ImGuiCol_TitleBg] = kPanel;
    c[ImGuiCol_TitleBgActive] = kPanel;
    c[ImGuiCol_TitleBgCollapsed] = kPanel;
    c[ImGuiCol_MenuBarBg] = kPanel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kPanelActive;
    c[ImGuiCol_ScrollbarGrabHovered] = kAccentDim;
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;
    c[ImGuiCol_Button] = kPanelRaised;
    c[ImGuiCol_ButtonHovered] = kPanelHover;
    c[ImGuiCol_ButtonActive] = kPanelActive;
    c[ImGuiCol_Header] = kAccentSoft;
    c[ImGuiCol_HeaderHovered] = rgb(kAccentHex, 0.28f);
    c[ImGuiCol_HeaderActive] = rgb(kAccentHex, 0.38f);
    c[ImGuiCol_Separator] = kBackground;
    c[ImGuiCol_SeparatorHovered] = kAccentDim;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = kAccentDim;
    c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_Tab] = kPanel;
    c[ImGuiCol_TabHovered] = kPanelRaised;
    c[ImGuiCol_TabSelected] = kPanel;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_TabDimmed] = kPanel;
    c[ImGuiCol_TabDimmedSelected] = kPanel;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = rgb(kAccentHex, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = kBackground;
    c[ImGuiCol_TextSelectedBg] = rgb(kAccentHex, 0.30f);
    c[ImGuiCol_DragDropTarget] = kAccent;
    c[ImGuiCol_NavCursor] = kAccent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

    g_scale = scale;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

}
