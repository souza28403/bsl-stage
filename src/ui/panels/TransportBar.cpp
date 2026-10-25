#include "ui/panels/TransportBar.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bsl::ui {

namespace {

constexpr float kBpmMin = 20.0f;
constexpr float kBpmMax = 300.0f;
constexpr double kTapResetSeconds = 2.0;
constexpr size_t kTapWindow = 4; // média das últimas 4 batidas

float clampBpm(float v)
{
    return std::clamp(v, kBpmMin, kBpmMax);
}

// TAP: média dos intervalos entre as últimas 4 batidas; 2 s sem toque recomeça a contagem
void registerTap(Composition& comp)
{
    static std::vector<double> taps;
    const double now = ImGui::GetTime();
    if (!taps.empty() && now - taps.back() > kTapResetSeconds)
        taps.clear();
    taps.push_back(now);
    if (taps.size() > kTapWindow)
        taps.erase(taps.begin());
    if (taps.size() >= 2) {
        const double avg = (taps.back() - taps.front()) / static_cast<double>(taps.size() - 1);
        if (avg > 0.0)
            comp.bpm = clampBpm(static_cast<float>(60.0 / avg));
    }
}

// Pisca em vermelho no início de cada batida e apaga em seguida
void drawBeatIndicator(const Composition& comp)
{
    const float h = transportBarHeight();
    ImGui::Dummy(ImVec2(h, h));
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 c(a.x + h * 0.5f, a.y + h * 0.5f);

    const double phase = comp.beat - std::floor(comp.beat);
    const float flash = comp.paused ? 0.0f : static_cast<float>(std::pow(1.0 - phase, 3.0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = dp(9.0f);
    dl->AddCircleFilled(c, radius, ImGui::GetColorU32(theme::kPanelRaised), 24);
    if (flash > 0.01f) {
        ImVec4 col = theme::kAccent;
        col.w = flash;
        dl->AddCircleFilled(c, radius, ImGui::GetColorU32(col), 24);
    }
    dl->AddCircle(c, radius, ImGui::GetColorU32(theme::kBorder), 24, std::max(1.0f, dp(1.0f)));
}

} // namespace

void drawTransportBar(PanelContext& ctx)
{
    Composition& comp = ctx.composition;
    const ImVec2 btn(dp(36.0f), transportBarHeight());
    const float frameY = (transportBarHeight() - ImGui::GetFontSize()) * 0.5f;
    const bool twoRows = transportRows(ImGui::GetContentRegionAvail().x) == 2;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(10.0f), frameY));

    // --- Play / pause / stop globais ---
    if (iconButton(ICON_PLAY, "##transport_play", "Tocar (retoma tudo)", btn,
                   comp.anyActive() && !comp.paused ? IconButton_Active : 0))
        comp.resumeAll();
    ImGui::SameLine();
    if (iconButton(ICON_PAUSE, "##transport_pause", "Pausar tudo", btn, comp.paused ? IconButton_Active : 0))
        comp.pauseAll();
    ImGui::SameLine();
    if (iconButton(ICON_STOP, "##transport_stop", "Parar tudo (limpa as camadas)", btn)) {
        comp.stopAll();
        comp.beat = 0.0;
    }

    // --- BPM ---
    ImGui::SameLine(0.0f, dp(20.0f));
    ImGui::AlignTextToFramePadding();
    dimLabel("BPM");
    ImGui::SameLine();
    const ImVec2 square(transportBarHeight(), transportBarHeight());
    if (iconButton(ICON_MINUS, "##bpm_menos", "Diminui 1 BPM", square))
        comp.bpm = clampBpm(comp.bpm - 1.0f);
    ImGui::SameLine();
    float bpm = comp.bpm;
    if (hotNumber("##bpm", bpm, kBpmMin, kBpmMax, 0.1f, "%.2f", nullptr, 120.0f, 72.0f))
        comp.bpm = clampBpm(bpm);
    ImGui::SameLine();
    if (iconButton(ICON_PLUS, "##bpm_mais", "Aumenta 1 BPM", square))
        comp.bpm = clampBpm(comp.bpm + 1.0f);

    if (!twoRows)
        ImGui::SameLine();
    if (ImGui::Button("TAP", ImVec2(dp(52.0f), transportBarHeight())))
        registerTap(comp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toque no ritmo: média das últimas 4 batidas");
    ImGui::SameLine();
    if (iconButton(ICON_RESYNC, "##transport_resync", "RESYNC: zera a fase do beat", square))
        comp.beat = 0.0;
    ImGui::SameLine();
    if (ImGui::Button("÷2", ImVec2(dp(38.0f), transportBarHeight())))
        comp.bpm = clampBpm(comp.bpm * 0.5f);
    ImGui::SameLine();
    if (ImGui::Button("×2", ImVec2(dp(38.0f), transportBarHeight())))
        comp.bpm = clampBpm(comp.bpm * 2.0f);

    // --- Indicador de beat + posição no compasso ---
    ImGui::SameLine(0.0f, dp(14.0f));
    drawBeatIndicator(comp);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const int inBar = static_cast<int>(std::floor(comp.beat)) % 4 + 1;
    pushMono();
    ImGui::TextColored(theme::kTextDim, "%d", inBar);
    popMono();

    ImGui::PopStyleVar(2);
}

} // namespace bsl::ui
