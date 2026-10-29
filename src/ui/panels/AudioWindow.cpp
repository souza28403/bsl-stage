#include "ui/panels/AudioWindow.h"

#include "audio/AudioAnalyzer.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

struct WindowState
{
    bool open = false;
    bool listed = false;
    audio::AudioSource listedSource = audio::AudioSource::Loopback;
    std::vector<audio::AudioDeviceInfo> devices;
};
WindowState g_window;

void refreshDevices(audio::AudioSource source)
{
    g_window.devices = audio::AudioAnalyzer::listDevices(source);
    g_window.listedSource = source;
    g_window.listed = true;
}

ImU32 levelColor(float v)
{
    if (v < 0.6f)
        return ImGui::GetColorU32(theme::kSuccess);
    if (v < 0.85f)
        return ImGui::GetColorU32(theme::kWarning);
    return ImGui::GetColorU32(theme::kAccent);
}

// Barra horizontal de 0..1 com rótulo à esquerda
void bandBar(const char* label, float value, float full)
{
    ImGui::AlignTextToFramePadding();
    dimLabel(label);
    ImGui::SameLine(dp(70.0f));
    const float h = ImGui::GetFrameHeight() * 0.55f;
    const float w = std::max(dp(60.0f), ImGui::GetContentRegionAvail().x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + (ImGui::GetFrameHeight() - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), ImGui::GetColorU32(theme::kPanelRaised), dp(2.0f));
    const float v = std::clamp(value / std::max(full, 0.01f), 0.0f, 1.0f);
    if (v > 0.0f)
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w * v, y + h), levelColor(v), dp(2.0f));
    ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
}

void spectrumView(const audio::AudioSnapshot& snap)
{
    const float h = dp(70.0f);
    const float w = std::max(dp(120.0f), ImGui::GetContentRegionAvail().x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(theme::kPanelRaised), dp(3.0f));
    const float bw = w / static_cast<float>(audio::kSpectrumBands);
    for (int i = 0; i < audio::kSpectrumBands; ++i) {
        const float v = std::clamp(snap.spectrum[static_cast<size_t>(i)], 0.0f, 1.0f);
        const float x0 = p.x + bw * static_cast<float>(i) + 1.0f;
        dl->AddRectFilled(ImVec2(x0, p.y + h - h * v), ImVec2(x0 + bw - 2.0f, p.y + h), levelColor(v));
    }
    ImGui::Dummy(ImVec2(w, h));
}

} // namespace

void openAudioWindow()
{
    g_window.open = true;
    g_window.listed = false;
}

bool audioMeter(const char* id, ImVec2 size)
{
    const audio::AudioSnapshot snap = audio::AudioAnalyzer::instance().snapshot();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ImGui::GetColorU32(theme::kPanelRaised), dp(2.0f));
    // O pico em escala de raiz: sinais fracos ainda se enxergam
    const float v = std::clamp(std::sqrt(snap.peak), 0.0f, 1.0f);
    const float r = std::clamp(std::sqrt(snap.rms), 0.0f, 1.0f);
    if (v > 0.0f)
        dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * v, b.y), snap.active ? levelColor(v) : ImGui::GetColorU32(theme::kTextDim), dp(2.0f));
    if (r > 0.0f)
        dl->AddRectFilled(ImVec2(a.x, b.y - dp(2.0f)), ImVec2(a.x + (b.x - a.x) * r, b.y), ImGui::GetColorU32(ImGuiCol_Text));
    if (ImGui::IsItemHovered()) {
        const std::string status = audio::AudioAnalyzer::instance().statusText();
        ImGui::SetTooltip("%s\nClique para configurar o áudio", status.c_str());
    }
    return clicked;
}

void drawAudioWindow(PanelContext& ctx)
{
    if (!g_window.open)
        return;
    audio::AudioAnalyzer& analyzer = audio::AudioAnalyzer::instance();
    audio::AudioConfig cfg = analyzer.config();
    bool changed = false;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(480.0f), dp(560.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Áudio###janela_audio", &g_window.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Análise de áudio compartilhada por todo o app: os efeitos BSL Light (Alpha Reactor, Datamosh, "
                       "AudioReactive Typo) leem as bandas daqui. Sem dispositivo, tudo fica em zero.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    if (ImGui::Checkbox("Capturar áudio##audio_ligado", &cfg.enabled))
        changed = true;

    ImGui::SeparatorText("Fonte");
    int source = cfg.source == audio::AudioSource::Input ? 1 : 0;
    if (ImGui::RadioButton("Som do PC (loopback)##audio_fonte_pc", &source, 0)) {
        cfg.source = audio::AudioSource::Loopback;
        cfg.deviceId.clear();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Entrada (microfone / placa)##audio_fonte_entrada", &source, 1)) {
        cfg.source = audio::AudioSource::Input;
        cfg.deviceId.clear();
        changed = true;
    }

    if (!g_window.listed || g_window.listedSource != cfg.source)
        refreshDevices(cfg.source);
    {
        std::string preview = "Automático";
        if (cfg.source == audio::AudioSource::Loopback)
            preview += " (a saída que está tocando)";
        else
            preview += " (entrada padrão do Windows)";
        for (const audio::AudioDeviceInfo& d : g_window.devices)
            if (d.id == cfg.deviceId)
                preview = d.name;
        dimLabel("Dispositivo");
        const float btn = dp(30.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
        if (beginCombo("##audio_dispositivo", preview.c_str())) {
            if (ImGui::Selectable("Automático", cfg.deviceId.empty())) {
                cfg.deviceId.clear();
                changed = true;
            }
            for (const audio::AudioDeviceInfo& d : g_window.devices) {
                if (ImGui::Selectable((d.name + "##dev_" + d.id).c_str(), d.id == cfg.deviceId)) {
                    cfg.deviceId = d.id;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (iconButton(ICON_REFRESH, "##audio_atualizar", "Procurar dispositivos de novo"))
            refreshDevices(cfg.source);
    }

    ImGui::SeparatorText("Análise");
    auto number = [&](const char* label, const char* id, float& v, float lo, float hi, float step, const char* fmt, const char* unit,
                      float def, const char* tip) {
        ImGui::AlignTextToFramePadding();
        dimLabel(label);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        ImGui::SameLine(dp(150.0f));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (hotNumber(id, v, lo, hi, step, fmt, unit, def, -1.0f))
            changed = true;
    };
    number("Ganho geral", "##audio_ganho", cfg.gain, 0.1f, 4.0f, 0.01f, "%.2f", "x", 1.0f,
           "Multiplica o sinal antes da análise, para todos os efeitos. Cada efeito tem ainda o próprio ganho.");
    number("Noise gate geral", "##audio_gate", cfg.gate, 0.0f, 0.15f, 0.0005f, "%.4f", nullptr, 0.0f,
           "Bandas abaixo deste nível viram zero.");
    number("Ataque", "##audio_ataque", cfg.attackMs, 0.0f, 200.0f, 0.5f, "%.0f", "ms", 0.0f,
           "Tempo de subida das bandas. 0 = instantâneo (como os plugins originais).");
    number("Queda (release)", "##audio_release", cfg.releaseScale, 0.25f, 4.0f, 0.01f, "%.2f", "x", 1.0f,
           "Multiplica o tempo de queda das bandas: maior = mais suave.");

    ImGui::SeparatorText("Ao vivo");
    const audio::AudioSnapshot snap = analyzer.snapshot();
    ImGui::TextColored(snap.active ? theme::kSuccess : theme::kTextDim, "%s", analyzer.statusText().c_str());
    bandBar("Nível", std::sqrt(snap.peak), 1.0f);
    bandBar("Graves", snap.bass, 2.0f);
    bandBar("Médios", snap.mid, 2.0f);
    bandBar("Agudos", snap.high, 2.0f);
    bandBar("Batida", snap.kick, 1.0f);
    spectrumView(snap);

    ImGui::Spacing();
    if (ImGui::Button("Restaurar padrões##audio_padroes")) {
        const bool wasEnabled = cfg.enabled;
        cfg = audio::AudioConfig{};
        cfg.enabled = wasEnabled;
        changed = true;
    }
    ImGui::End();

    if (changed) {
        analyzer.setConfig(cfg);
        ctx.settings.audio = analyzer.config();
        ctx.settings.save();
    }
}

} // namespace bsl::ui
