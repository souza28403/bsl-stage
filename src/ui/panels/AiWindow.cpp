#include "ui/panels/AiWindow.h"

#include "ai/AiEngine.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <string>

namespace bsl::ui {

namespace {

bool g_open = false;

void kv(const char* label, const std::string& value, const ImVec4* color = nullptr)
{
    ImGui::AlignTextToFramePadding();
    dimLabel(label);
    ImGui::SameLine(dp(150.0f));
    ImGui::PushTextWrapPos(0.0f);
    if (color)
        ImGui::TextColored(*color, "%s", value.c_str());
    else
        ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

std::string fmt1(double v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

} // namespace

void openAiWindow()
{
    g_open = true;
}

void drawAiWindow(PanelContext& ctx)
{
    if (!g_open)
        return;
    ai::AiEngine& engine = ai::AiEngine::instance();
    const ai::AiStats st = engine.stats();
    bool changed = false;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(520.0f), dp(520.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("IA###janela_ia", &g_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "IA local e opcional, usada pelos efeitos marcados com \"IA\" (Relight 2.5D, Cybernetic Body-Parts Glitch). "
                       "Tudo roda neste PC: nenhuma imagem sai do computador. Desligada no perfil Eco.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::SeparatorText("Estado");
    if (!st.enabled) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kWarning, "IA desligada no perfil Eco: os efeitos de IA passam a imagem direto. Escolha o perfil "
                                            "Balanceado ou Máximo na barra de status para usar.");
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextColored(theme::kSuccess, "IA ligada: %d fps de inferência por fonte", st.rate);
    }

    // Dispositivo em uso: o dos modelos carregados; antes de carregar, o que o DirectML vai usar
    std::string device = "ainda não carregado";
    bool onGpu = false;
    for (const ai::ModelStats& m : st.models)
        if (m.loaded) {
            device = m.device;
            onGpu = m.onGpu;
            break;
        }
    kv("Dispositivo em uso", device);
    if (!st.gpuName.empty())
        kv("GPU escolhida (DX12)", st.gpuName);
    else if (st.runtimeTried && st.runtimeLoaded)
        kv("GPU escolhida (DX12)", "nenhuma: usa a CPU");
    kv("ONNX Runtime", st.runtimeLoaded ? st.runtimeVersion : (st.runtimeTried ? (st.runtimeError.empty() ? "carregando…" : st.runtimeError) : "carrega no primeiro uso"));
    kv("fps de IA", fmt1(static_cast<double>(st.inferencesPerSec)) + " inferências/s (todas as fontes)");
    kv("Fontes ativas", std::to_string(st.activeSources));
    (void)onGpu;

    ImGui::SeparatorText("Testar");
    ImGui::BeginDisabled(st.testRunning);
    if (ImGui::Button((st.testRunning ? "Testando…##ia_testar" : "Testar##ia_testar")))
        engine.requestSelfTest();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(theme::kTextDim, "carrega os modelos e roda uma imagem de teste (mesmo no Eco)");
    if (st.testDone && !st.testReport.empty()) {
        pushMono();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(st.testReport.c_str());
        ImGui::PopTextWrapPos();
        popMono();
    }


    ImGui::SeparatorText("Modelos");
    for (int k = 0; k < ai::kModelKindCount; ++k) {
        const ai::ModelSpec& spec = ai::modelSpec(static_cast<ai::ModelKind>(k));
        const ai::ModelStats& m = st.models[k];
        ImGui::PushID(k);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(spec.label);
        if (!m.present) {
            const ImVec4 red = theme::kWarning;
            kv("   Estado", std::string("arquivo ausente (") + spec.fileName + ")", &red);
        } else if (m.loaded) {
            const ImVec4 ok = theme::kSuccess;
            kv("   Estado", "carregado — " + m.device, &ok);
            kv("   Tempo", fmt1(m.lastMs) + " ms por inferência (média " + fmt1(m.avgMs) + " ms)");
        } else if (!m.error.empty()) {
            const ImVec4 red = theme::kWarning;
            kv("   Estado", "falhou: " + m.error, &red);
        } else {
            kv("   Estado", "não carregado (carrega quando um efeito de IA pede)");
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Configuração");
    {
        ImGui::AlignTextToFramePadding();
        dimLabel("Dispositivo");
        ImGui::SameLine(dp(150.0f));
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char* labels[] = {"Automático (GPU, com CPU de reserva)", "Só GPU (DirectML)", "Só CPU"};
        const char* keys[] = {"auto", "gpu", "cpu"};
        int cur = ctx.settings.aiDevice == "gpu" ? 1 : ctx.settings.aiDevice == "cpu" ? 2 : 0;
        if (beginCombo("##ia_dispositivo", labels[cur])) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(labels[i], i == cur)) {
                    ctx.settings.aiDevice = keys[i];
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::AlignTextToFramePadding();
        dimLabel("Taxa de inferência");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inferências por segundo em cada fonte (clip). 0 = a do perfil: Balanceado 15, Máximo 30.\n"
                              "Menos = mais leve; a imagem usa o último resultado da IA entre uma inferência e outra.");
        ImGui::SameLine(dp(150.0f));
        ImGui::SetNextItemWidth(-FLT_MIN);
        float fps = static_cast<float>(ctx.settings.aiFps);
        if (hotNumber("##ia_taxa", fps, 0.0f, 60.0f, 0.2f, "%.0f", "fps (0 = perfil)", 0.0f, -1.0f)) {
            ctx.settings.aiFps = static_cast<int>(fps + 0.5f);
            changed = true;
        }
    }

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Modelos: Depth Anything V2 Small (Apache-2.0) e MODNet (Apache-2.0). Motor: ONNX Runtime e DirectML (MIT). "
                       "Detalhes em docs/PATENTES-E-LICENCAS.md.");
    ImGui::PopTextWrapPos();
    ImGui::End();

    if (changed)
        ctx.settings.save();
}

} // namespace bsl::ui
