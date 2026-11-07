#include "ui/Widgets.h"

#include "app/Qa.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/ViewUi.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bsl::ui {

bool beginCombo(const char* id, const char* preview)
{
    qa::label(id);
    return ImGui::BeginCombo(id, preview);
}

void dimLabel(const char* label)
{
    ImGui::TextColored(theme::kTextDim, "%s", label);
}

void pushMono()
{
    ImGui::PushFont(fonts().mono, fonts().mono->LegacySize);
}

void popMono()
{
    ImGui::PopFont();
}

void monoText(const char* fmt, ...)
{
    pushMono();
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
    popMono();
}

std::string formatTime(double seconds)
{
    const int total = static_cast<int>(seconds);
    const int centi = static_cast<int>((seconds - total) * 100.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", total / 60, total % 60, centi);
    return buf;
}

bool beginPanel(const char* name, ImGuiWindowFlags flags)
{
    return viewBegin(name, flags);
}

void endPanel()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->DockNode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const ImRect r = window->DockNode->Rect();
        const float thickness = std::max(1.0f, dp(1.0f));
        const float inset = thickness * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(r.Min, r.Max, false);
        dl->AddRect(ImVec2(r.Min.x + inset, r.Min.y + inset), ImVec2(r.Max.x - inset, r.Max.y - inset),
                    ImGui::GetColorU32(ImVec4(theme::kAccent.x, theme::kAccent.y, theme::kAccent.z, 0.70f)), rounding(), 0,
                    thickness);
        dl->PopClipRect();
    }
    ImGui::End();
}

bool toggleButton(const char* label, bool& value, const char* tooltip, ImVec2 size)
{
    const bool on = value;
    if (size.x <= 0.0f)
        size.x = dp(28.0f);
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
        ImGui::PushStyleColor(ImGuiCol_Border, theme::kAccent);
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kField);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
        ImGui::PushStyleColor(ImGuiCol_Border, theme::kBorder);
    }
    const bool pressed = ImGui::Button(label, size);
    if (on) {
        ImGui::PopFont();
        ImGui::PopStyleColor(5);
    } else {
        ImGui::PopStyleColor(3);
    }
    if (pressed)
        value = !value;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

void drawPlayTriangle(ImDrawList* dl, ImVec2 c, float s, ImU32 color)
{
    dl->AddTriangleFilled(ImVec2(c.x - s * 0.35f, c.y - s * 0.5f), ImVec2(c.x - s * 0.35f, c.y + s * 0.5f),
                          ImVec2(c.x + s * 0.55f, c.y), color);
}

void drawIcon(ImDrawList* dl, const char* icon, ImVec2 center, ImU32 color, float scale)
{
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * scale;
    ImFontBaked* baked = font->GetFontBaked(size);
    float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX, pen = 0.0f;
    for (const char* p = icon; *p;) {
        unsigned int codepoint = 0;
        const int bytes = ImTextCharFromUtf8(&codepoint, p, nullptr);
        p += bytes > 0 ? bytes : 1;
        const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
        if (!glyph)
            continue;
        if (glyph->Visible) {
            x0 = std::min(x0, pen + glyph->X0);
            y0 = std::min(y0, glyph->Y0);
            x1 = std::max(x1, pen + glyph->X1);
            y1 = std::max(y1, glyph->Y1);
        }
        pen += glyph->AdvanceX;
    }
    ImVec2 pos = center;
    if (x1 > x0) {
        pos.x -= (x0 + x1) * 0.5f;
        pos.y -= (y0 + y1) * 0.5f;
    } else {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
        pos.x -= ts.x * 0.5f;
        pos.y -= ts.y * 0.5f;
    }
    dl->AddText(font, size, pos, color, icon);
}

bool iconButton(const char* icon, const char* id, const char* tooltip, ImVec2 size, int flags, ImU32 color)
{
    if (size.x <= 0.0f || size.y <= 0.0f) {
        const float side = ImGui::GetFrameHeight();
        size = ImVec2(size.x > 0.0f ? size.x : side, size.y > 0.0f ? size.y : side);
    }
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool active = (flags & IconButton_Active) != 0;
    const bool flat = (flags & IconButton_Flat) != 0;
    if (!flat || active) {
        ImVec4 bg = flat ? ImVec4(0, 0, 0, 0) : theme::kPanelRaised;
        if (active)
            bg = held ? theme::kAccentDim : (hovered ? theme::kAccentHover : theme::kAccent);
        else if (held)
            bg = theme::kPanelActive;
        else if (hovered)
            bg = theme::kPanelHover;
        dl->AddRectFilled(a, b, ImGui::GetColorU32(bg), rounding());
    } else if (hovered) {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, held ? 0.16f : 0.10f)), rounding());
    }

    ImU32 fg = color ? color : ImGui::GetColorU32(active ? theme::kOnAccent : theme::kText);
    if (!color && !active && !hovered && flat)
        fg = ImGui::GetColorU32(theme::kTextDim);
    drawIcon(dl, icon, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), fg);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool iconToggle(const char* icon, const char* id, bool& value, const char* tooltip, ImVec2 size, int flags)
{
    const bool pressed = iconButton(icon, id, tooltip, size, flags | (value ? IconButton_Active : 0));
    if (pressed)
        value = !value;
    return pressed;
}

void drawPatternThumb(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    static const ImU32 bars[7] = {IM_COL32(235, 235, 235, 255), IM_COL32(235, 235, 0, 255),
                                  IM_COL32(0, 235, 235, 255),   IM_COL32(0, 235, 0, 255),
                                  IM_COL32(235, 0, 235, 255),   IM_COL32(235, 0, 0, 255),
                                  IM_COL32(0, 0, 235, 255)};
    const float w = (b.x - a.x) / 7.0f;
    const float split = a.y + (b.y - a.y) * 0.70f;
    for (int i = 0; i < 7; ++i)
        dl->AddRectFilled(ImVec2(a.x + w * static_cast<float>(i), a.y),
                          ImVec2(a.x + w * static_cast<float>(i + 1), split), bars[i]);
    dl->AddRectFilled(ImVec2(a.x, split), ImVec2(a.x + (b.x - a.x) * 0.5f, b.y), IM_COL32(40, 40, 40, 255));
    dl->AddRectFilled(ImVec2(a.x + (b.x - a.x) * 0.5f, split), b, IM_COL32(210, 210, 210, 255));
}

void drawCheckerboard(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell)
{
    if (cell <= 0.0f)
        cell = dp(10.0f);
    const ImU32 c1 = IM_COL32(44, 44, 46, 255);
    const ImU32 c2 = IM_COL32(64, 64, 67, 255);
    dl->PushClipRect(a, b, true);
    dl->AddRectFilled(a, b, c1);
    int iy = 0;
    for (float y = a.y; y < b.y; y += cell, ++iy) {
        int ix = 0;
        for (float x = a.x; x < b.x; x += cell, ++ix)
            if ((ix + iy) & 1)
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), c2);
    }
    dl->PopClipRect();
}

void drawDashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float dash, float gap)
{
    if (dash <= 0.0f)
        dash = dp(6.0f);
    if (gap <= 0.0f)
        gap = dp(4.0f);
    const float thickness = std::max(1.0f, dp(1.0f));
    auto line = [&](ImVec2 p, ImVec2 q) {
        const float len = std::hypot(q.x - p.x, q.y - p.y);
        if (len <= 0.0f)
            return;
        const ImVec2 d((q.x - p.x) / len, (q.y - p.y) / len);
        for (float t = 0.0f; t < len; t += dash + gap) {
            const float e = std::min(t + dash, len);
            dl->AddLine(ImVec2(p.x + d.x * t, p.y + d.y * t), ImVec2(p.x + d.x * e, p.y + d.y * e), color, thickness);
        }
    };
    line(a, ImVec2(b.x, a.y));
    line(ImVec2(b.x, a.y), b);
    line(b, ImVec2(a.x, b.y));
    line(ImVec2(a.x, b.y), a);
}

void drawCenteredMono(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color)
{
    ImFont* mono = fonts().mono;
    const float size = fontPx(mono);
    const ImVec2 ts = mono->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(mono, size, ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f), color,
                text);
}

void drawCenteredFit(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color)
{
    ImFont* font = ImGui::GetFont();
    float size = ImGui::GetFontSize();
    const float maxW = (b.x - a.x) - dp(10.0f);
    const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    if (w > maxW && w > 0.0f)
        size = std::max(dp(8.0f), size * maxW / w);
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f), color, text);
}

namespace {

struct HotState
{
    ImGuiID editId = 0;
    int editFrame = -1;
    bool focusEdit = false;
    char buf[64] = {};
    char initial[64] = {};

    ImGuiID dragId = 0;
    bool moved = false;
    double accum = 0.0;
    float startX = 0.0f;
    float lastX = 0.0f;

    int frame = -1;
    int seq = 0;
    int tabTarget = -1;
    int tabFrame = -1;
};
HotState g_hot;

void formatValue(char* out, size_t n, const char* format, double v, bool isInt)
{
    if (isInt)
        std::snprintf(out, n, format, static_cast<int>(std::lround(v)));
    else
        std::snprintf(out, n, format, v);
}

bool parseValue(const char* text, double& out)
{
    std::string t(text);
    std::replace(t.begin(), t.end(), ',', '.');
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

std::function<void()> g_hotExtra;

bool hotCore(const char* id, double& value, double lo, double hi, double step, bool isInt, const char* format,
             const char* unit, double def, float width, bool bar)
{
    const std::function<void()> extraMenu = std::move(g_hotExtra);
    g_hotExtra = nullptr;
    HotState& h = g_hot;
    const int frame = ImGui::GetFrameCount();
    if (h.frame != frame) {
        h.frame = frame;
        h.seq = 0;
    }
    if (h.editId != 0 && h.editFrame < frame - 1)
        h.editId = 0;
    if (h.tabTarget >= 0 && h.tabFrame < frame - 1)
        h.tabTarget = -1;
    const int seq = h.seq++;
    const ImGuiID uid = ImGui::GetID(id);
    const double original = value;

    const float height = ImGui::GetFrameHeight();
    const float availW = ImGui::GetContentRegionAvail().x;
    char text[64];
    formatValue(text, sizeof(text), format, value, isInt);

    ImFont* mono = fonts().mono;
    ImGui::PushFont(mono, mono->LegacySize);
    const float monoPx = ImGui::GetFontSize();
    const float valueW = ImGui::CalcTextSize(text).x;
    ImGui::PopFont();
    const float gap = dp(3.0f);
    const bool hasUnit = unit && unit[0] != '\0';
    const float unitW = hasUnit ? ImGui::CalcTextSize(unit).x : 0.0f;
    const float textW = valueW + (hasUnit ? gap + unitW : 0.0f);
    float w = width < 0.0f ? std::max(availW, dp(24.0f)) : (width > 0.0f ? dp(width) : textW + dp(2.0f));
    w = std::max(w, dp(18.0f));

    if (h.tabTarget == seq && h.editId == 0) {
        h.tabTarget = -1;
        h.editId = uid;
        h.focusEdit = true;
        std::snprintf(h.buf, sizeof(h.buf), "%s", text);
        std::snprintf(h.initial, sizeof(h.initial), "%s", text);
    }

    if (h.editId == uid) {
        h.editFrame = frame;
        if (h.focusEdit) {
            ImGui::SetKeyboardFocusHere();
            h.focusEdit = false;
        }
        ImGui::PushFont(mono, mono->LegacySize);
        ImGui::SetNextItemWidth(std::max(w, dp(64.0f)));
        const bool entered = ImGui::InputText(id, h.buf, sizeof(h.buf),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
                                                  | ImGuiInputTextFlags_CharsScientific);
        ImGui::PopFont();
        ImGui::SetItemKeyOwner(ImGuiKey_Tab);
        const bool tab = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Tab, false);
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (entered || deactivated || tab) {
            if (!cancelled && std::strcmp(h.buf, h.initial) != 0) {
                double parsed = 0.0;
                if (parseValue(h.buf, parsed))
                    value = std::clamp(isInt ? std::round(parsed) : parsed, lo, hi);
            }
            h.editId = 0;
            if (tab) {
                h.tabTarget = std::max(0, seq + (ImGui::GetIO().KeyShift ? -1 : 1));
                h.tabFrame = frame;
            }
        }
        return value != original;
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivated();
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsItemActivated()) {
        h.dragId = uid;
        h.moved = false;
        h.accum = value;
        h.startX = h.lastX = io.MousePos.x;
    }
    if (active && h.dragId == uid) {
        if (!h.moved && std::fabs(io.MousePos.x - h.startX) >= dp(3.0f))
            h.moved = true;
        if (h.moved) {
            const double mult = io.KeyShift ? 10.0 : (io.KeyCtrl ? 0.1 : 1.0);
            h.accum = std::clamp(h.accum + static_cast<double>(io.MousePos.x - h.lastX) * step * mult, lo, hi);
            h.lastX = io.MousePos.x;
            value = isInt ? std::round(h.accum) : h.accum;
        }
    }
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (released && h.dragId == uid && !h.moved && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        h.editId = uid;
        h.focusEdit = true;
        h.editFrame = frame;
        std::snprintf(h.buf, sizeof(h.buf), "%s", text);
        std::snprintf(h.initial, sizeof(h.initial), "%s", text);
    }

    if ((std::isfinite(def) || extraMenu) && ImGui::BeginPopupContextItem(nullptr)) {
        if (std::isfinite(def) && ImGui::MenuItem("Restaurar padrão"))
            value = std::clamp(def, lo, hi);
        if (extraMenu) {
            if (std::isfinite(def))
                ImGui::Separator();
            extraMenu();
        }
        ImGui::EndPopup();
    }
    if (hovered && !active && !ImGui::IsPopupOpen(uid, ImGuiPopupFlags_None) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Arraste para ajustar (Shift: x10, Ctrl: x0,1)\nClique para digitar%s",
                          std::isfinite(def) ? "\nBotão direito: restaurar padrão" : "");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float textY = pos.y + (height - monoPx) * 0.5f;
    const ImU32 valueCol = ImGui::GetColorU32(theme::kAccentHover);
    dl->AddText(mono, monoPx, ImVec2(pos.x, textY), valueCol, text);
    if (hasUnit)
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(pos.x + valueW + gap, pos.y + (height - ImGui::GetFontSize()) * 0.5f),
                    ImGui::GetColorU32(theme::kTextDim), unit);
    if (hovered || active) {
        const float y = textY + monoPx + dp(1.0f);
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + textW, y), valueCol, std::max(1.0f, dp(1.0f)));
    }
    if (bar) {
        const float t = hi > lo ? static_cast<float>(std::clamp((value - lo) / (hi - lo), 0.0, 1.0)) : 0.0f;
        const float bh = std::max(2.0f, dp(2.0f));
        const ImVec2 b0(pos.x, pos.y + height - bh - dp(1.0f));
        const ImVec2 b1(pos.x + w, b0.y + bh);
        dl->AddRectFilled(b0, b1, ImGui::GetColorU32(theme::kPanelRaised), bh * 0.5f);
        if (t > 0.0f)
            dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * t, b1.y),
                              ImGui::GetColorU32(hovered || active ? theme::kAccentHover : theme::kAccent), bh * 0.5f);
    }

    value = std::clamp(value, lo, hi);
    return value != original;
}

}

void splitValueFormat(const char* format, std::string& numberFormat, std::string& unit)
{
    numberFormat.clear();
    unit.clear();
    const char* f = format && format[0] ? format : "%.2f";
    const char* p = f;
    for (; *p; ++p) {
        if (*p == '%' && p[1] == '%') {
            ++p;
            continue;
        }
        if (*p == '%')
            break;
    }
    if (!*p) {
        numberFormat = "%.2f";
        unit = f;
        return;
    }
    const char* q = p + 1;
    while (*q && !std::strchr("diouxXeEfFgG", *q))
        ++q;
    if (*q)
        ++q;
    numberFormat.assign(f, static_cast<size_t>(q - f));
    std::string rest(q);
    for (size_t i = 0; i + 1 < rest.size();) {
        if (rest[i] == '%' && rest[i + 1] == '%')
            rest.erase(i + 1, 1);
        else
            ++i;
    }
    const size_t first = rest.find_first_not_of(' ');
    unit = first == std::string::npos ? std::string() : rest.substr(first);
}

void hotContextExtra(std::function<void()> draw)
{
    g_hotExtra = std::move(draw);
}

bool hotNumber(const char* id, float& v, float lo, float hi, float step, const char* format, const char* unit, float def,
               float width)
{
    double d = static_cast<double>(v);
    const bool changed = hotCore(id, d, static_cast<double>(lo), static_cast<double>(hi), static_cast<double>(step), false,
                                 format ? format : "%.2f", unit, static_cast<double>(def), width, false);
    if (changed)
        v = static_cast<float>(d);
    return changed;
}

bool hotInt(const char* id, int& v, int lo, int hi, float step, const char* format, const char* unit, int def, float width)
{
    double d = static_cast<double>(v);
    const bool changed = hotCore(id, d, static_cast<double>(lo), static_cast<double>(hi), static_cast<double>(step), true,
                                 format ? format : "%d", unit, def == INT_MIN ? static_cast<double>(NAN) : static_cast<double>(def),
                                 width, false);
    if (changed)
        v = static_cast<int>(std::lround(d));
    return changed;
}

bool brandHeader(const char* label)
{
    const bool open = ImGui::CollapsingHeader(label);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Texture& mark = brand().monogram;
    const float h = (b.y - a.y) - dp(6.0f);
    if (mark && mark.height > 0) {
        const float w = h * static_cast<float>(mark.width) / static_cast<float>(mark.height);
        const float x1 = b.x - dp(8.0f);
        dl->AddImage(static_cast<ImTextureID>(mark.id), ImVec2(x1 - w, a.y + dp(3.0f)), ImVec2(x1, a.y + dp(3.0f) + h));
    } else {
        drawIcon(dl, ICON_ZAP, ImVec2(b.x - dp(16.0f), (a.y + b.y) * 0.5f), ImGui::GetColorU32(theme::kAccent), 1.1f);
    }
    return open;
}

bool hotPercent(const char* id, float& value, float def, float width)
{
    double d = static_cast<double>(value) * 100.0;
    const double defPercent = std::isfinite(def) ? static_cast<double>(def) * 100.0 : static_cast<double>(NAN);
    const bool changed = hotCore(id, d, 0.0, 100.0, 0.5, false, "%.0f", "%", defPercent, width == 0.0f ? 64.0f : width, true);
    if (changed)
        value = static_cast<float>(d / 100.0);
    return changed;
}

}
