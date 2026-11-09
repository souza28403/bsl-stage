#include "ui/panels/CaptureTargetChooser.h"

#include "media/CaptureTargets.h"
#include "render/Texture.h"
#include "ui/panels/NetStreamWindow.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

constexpr int kThumbW = 160;
constexpr int kThumbH = 90;
float cardW() { return dp(176.0f); }
float cardH() { return dp(6.0f + 90.0f + 52.0f); }
const char* const kPopupName = "Escolher alvo da captura";

struct Thumb
{
    Texture tex;
    bool failed = false;
};

struct ChooserState
{
    bool wantOpen = false;
    bool open = false;
    int deck = 0, layer = 0, column = 0;
    std::vector<media::MonitorTarget> monitors;
    std::vector<media::WindowTarget> windows;
    std::map<std::string, Thumb> thumbs;
    std::deque<std::string> pending;
};
ChooserState g_state;

std::string monitorKey(const media::MonitorTarget& m)
{
    return "m" + std::to_string(m.index);
}

std::string windowKey(const media::WindowTarget& w)
{
    return "w" + std::to_string(w.hwnd);
}

void refreshLists()
{
    g_state.monitors = media::listMonitors();
    g_state.windows = media::listWindows();
    g_state.thumbs.clear();
    g_state.pending.clear();
    for (const auto& m : g_state.monitors)
        g_state.pending.push_back(monitorKey(m));
    for (const auto& w : g_state.windows)
        g_state.pending.push_back(windowKey(w));
}

void uploadThumb(Thumb& t, const media::ThumbRgba& img)
{
    GLuint id = 0;
    GLint prevTex = 0, prevAlign = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    t.tex = Texture(id, img.w, img.h);
}

void pumpThumbs()
{
    for (int n = 0; n < 2 && !g_state.pending.empty(); ++n) {
        const std::string key = g_state.pending.front();
        g_state.pending.pop_front();
        Thumb& t = g_state.thumbs[key];
        media::ThumbRgba img;
        bool ok = false;
        if (key[0] == 'm') {
            for (const auto& m : g_state.monitors)
                if (monitorKey(m) == key)
                    ok = media::grabMonitorThumb(m, kThumbW, kThumbH, img);
        } else {
            for (const auto& w : g_state.windows)
                if (windowKey(w) == key)
                    ok = media::grabWindowThumb(w, kThumbW, kThumbH, img);
        }
        if (ok)
            uploadThumb(t, img);
        else
            t.failed = true;
    }
}

void drawClipped(ImDrawList* dl, ImVec2 pos, float width, ImU32 color, const std::string& text)
{
    dl->PushClipRect(pos, ImVec2(pos.x + width, pos.y + ImGui::GetFontSize() + dp(4.0f)), true);
    dl->AddText(pos, color, text.c_str());
    dl->PopClipRect();
}

bool card(const std::string& key, const std::string& title, const std::string& subtitle, bool current)
{
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::PushID(key.c_str());
    const bool pressed = ImGui::InvisibleButton("##card", ImVec2(cardW(), cardH()));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const ImVec2 b(a.x + cardW(), a.y + cardH());
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(a, b, ImGui::GetColorU32(hovered ? theme::kPanelHover : theme::kPanelRaised), rounding());
    const ImVec2 t0(a.x + dp(8.0f), a.y + dp(6.0f));
    const ImVec2 t1(t0.x + dp(static_cast<float>(kThumbW)), t0.y + dp(static_cast<float>(kThumbH)));
    const auto it = g_state.thumbs.find(key);
    if (it != g_state.thumbs.end() && it->second.tex.id) {
        dl->AddImage(static_cast<ImTextureID>(it->second.tex.id), t0, t1);
    } else {
        dl->AddRectFilled(t0, t1, ImGui::GetColorU32(theme::kBackground));
        drawCenteredMono(dl, t0, t1, it != g_state.thumbs.end() && it->second.failed ? "sem prévia" : "...",
                         ImGui::GetColorU32(theme::kTextDim));
    }
    drawClipped(dl, ImVec2(a.x + dp(8.0f), t1.y + dp(3.0f)), cardW() - dp(16.0f), ImGui::GetColorU32(theme::kText), title);
    drawClipped(dl, ImVec2(a.x + dp(8.0f), t1.y + dp(3.0f) + ImGui::GetFontSize() + dp(1.0f)), cardW() - dp(16.0f),
                ImGui::GetColorU32(theme::kTextDim), subtitle);
    if (current)
        dl->AddRect(a, b, ImGui::GetColorU32(theme::kAccent), rounding(), 0, dp(2.0f));
    if (hovered && !title.empty())
        ImGui::SetTooltip("%s\n%s", title.c_str(), subtitle.c_str());
    return pressed;
}

void cardFlow(int& column, int perRow)
{
    ++column;
    if (column < perRow)
        ImGui::SameLine();
    else
        column = 0;
}

}

void requestCaptureTarget(int deck, int layer, int column)
{
    g_state.wantOpen = true;
    g_state.deck = deck;
    g_state.layer = layer;
    g_state.column = column;
}

void afterClipPlaced(const Composition& comp, int layer, int column)
{
    if (!comp.validCell(layer, column))
        return;
    const Clip& placed = comp.clip(layer, column);
    if (placed.isGenerator() && placed.generatorId == kNetStreamId) {
        requestNetStreamWindow(comp.currentDeck, layer, column);
        return;
    }
    const GeneratorInfo* info = placed.generatorInfo();
    if (info && info->needsSetup)
        requestCaptureTarget(comp.currentDeck, layer, column);
}

void drawCaptureTargetChooser(PanelContext& ctx)
{
    if (g_state.wantOpen) {
        g_state.wantOpen = false;
        refreshLists();
        ImGui::OpenPopup(kPopupName);
        g_state.open = true;
    }
    if (!g_state.open)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(dp(820.0f, 560.0f), ImGuiCond_Appearing);
    bool keepOpen = true;
    if (!ImGui::BeginPopupModal(kPopupName, &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(kPopupName)) {
            g_state.open = false;
            g_state.thumbs.clear();
        }
        return;
    }

    Composition& comp = ctx.composition;
    media::Json current;
    Clip* clip = nullptr;
    if (comp.validCell(g_state.deck, g_state.layer, g_state.column)) {
        Clip& c = comp.clipAt(g_state.deck, g_state.layer, g_state.column);
        if (c.isGenerator() && c.generatorId == kScreenCaptureId) {
            clip = &c;
            if (const media::Json* t = c.params.find("target"))
                current = *t;
        }
    }
    if (!clip) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        g_state.open = false;
        g_state.thumbs.clear();
        return;
    }

    pumpThumbs();

    ImGui::TextColored(theme::kTextDim,
                       "Escolha um monitor ou uma janela para capturar. As janelas do BSL Stage ficam fora da lista "
                       "(evita o espelho infinito).");
    ImGui::Spacing();

    const float footer = ImGui::GetFrameHeightWithSpacing() + dp(8.0f);
    ImGui::BeginChild("##alvos", ImVec2(0.0f, -footer), ImGuiChildFlags_None);
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) /
                                                    (cardW() + ImGui::GetStyle().ItemSpacing.x)));

    media::Json chosen;
    ImGui::SeparatorText("Monitores");
    int col = 0;
    const bool curIsMonitor = current.is_object() && current.value("kind", std::string("monitor")) == "monitor";
    const int curIndex = (current.is_object() && current.contains("index") && current["index"].is_number_integer())
                             ? current["index"].get<int>()
                             : 1;
    for (const media::MonitorTarget& m : g_state.monitors) {
        const std::string title = "Monitor " + std::to_string(m.index) + (m.primary ? " (principal)" : "");
        const std::string sub = std::to_string(m.w) + " x " + std::to_string(m.h);
        if (card(monitorKey(m), title, sub, curIsMonitor && curIndex == m.index))
            chosen = media::monitorTargetJson(m.index);
        cardFlow(col, perRow);
    }
    if (g_state.monitors.empty())
        ImGui::TextColored(theme::kWarning, "Nenhum monitor encontrado.");
    ImGui::NewLine();

    ImGui::SeparatorText("Janelas");
    col = 0;
    const std::string curExe = current.is_object() ? current.value("exe", std::string()) : std::string();
    const std::string curTitle = current.is_object() ? current.value("title", std::string()) : std::string();
    for (const media::WindowTarget& w : g_state.windows) {
        const bool isCurrent = !curIsMonitor && w.exe == curExe && w.title == curTitle;
        if (card(windowKey(w), w.title, w.exe, isCurrent))
            chosen = media::windowTargetJson(w);
        cardFlow(col, perRow);
    }
    if (g_state.windows.empty())
        ImGui::TextColored(theme::kTextDim, "Nenhuma janela disponível.");
    ImGui::EndChild();

    if (ImGui::Button("Atualizar lista"))
        refreshLists();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(dp(110.0f), 0.0f)))
        keepOpen = false;

    if (chosen.is_object()) {
        clip->setParam("target", chosen);
        keepOpen = false;
    }
    if (!keepOpen) {
        ImGui::CloseCurrentPopup();
        g_state.open = false;
        g_state.thumbs.clear();
        g_state.pending.clear();
    }
    ImGui::EndPopup();
}

}
