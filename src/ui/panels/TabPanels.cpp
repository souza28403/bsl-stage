#include "ui/panels/TabPanels.h"

#include "output/OutputRenderer.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/EffectChainWidget.h"
#include "ui/panels/TransformEditor.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>

namespace bsl::ui {

namespace {

void canvasControls(Composition& comp)
{
    struct Preset
    {
        const char* label;
        int w, h;
    };
    static const Preset presets[] = {{"1920 x 1080", 1920, 1080},
                                     {"1280 x 720", 1280, 720},
                                     {"3840 x 2160", 3840, 2160},
                                     {"1080 x 1920 (vertical)", 1080, 1920}};

    const char* current = "Personalizado";
    for (const Preset& p : presets)
        if (p.w == comp.width && p.h == comp.height)
            current = p.label;

    dimLabel("Canvas");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##canvasPreset", current)) {
        for (const Preset& p : presets)
            if (ImGui::Selectable(p.label, p.w == comp.width && p.h == comp.height)) {
                comp.width = p.w;
                comp.height = p.h;
            }
        ImGui::EndCombo();
    }

    static int editW = 0, editH = 0, lastW = 0, lastH = 0;
    if (comp.width != lastW || comp.height != lastH) {
        editW = lastW = comp.width;
        editH = lastH = comp.height;
    }
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    hotInt("##canvasW", editW, 16, 8192, 4.0f, "%d", "px", comp.width, half / uiScale());
    ImGui::SameLine();
    hotInt("##canvasH", editH, 16, 8192, 4.0f, "%d", "px", comp.height, -1.0f);

    const bool dirty = editW != comp.width || editH != comp.height;
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Aplicar tamanho", ImVec2(-FLT_MIN, 0.0f))) {
        comp.width = std::clamp(editW, 16, 8192);
        comp.height = std::clamp(editH, 16, 8192);
    }
    ImGui::EndDisabled();
}

std::string destinationLabel(const Screen& s, const std::vector<MonitorInfo>& monitors)
{
    switch (s.kind) {
    case OutputKind::Monitor:
        for (const MonitorInfo& m : monitors)
            if (m.index == s.monitorIndex && m.name == s.monitorName)
                return "Monitor " + std::to_string(m.index + 1) + ": " + m.name;
        return "Monitor ausente: " + s.monitorName;
    case OutputKind::Window: return "Janela";
    case OutputKind::Spout: return "Spout: " + s.spoutName;
    case OutputKind::Ndi: return "NDI: " + s.ndiName;
    default: return "Desativado";
    }
}

}

void drawCompositionTabPanel(PanelContext& ctx)
{
    beginPanel(panel::kCompositionTab);
    Composition& comp = ctx.composition;

    canvasControls(comp);

    ImGui::Spacing();
    dimLabel("Taxa de quadros");
    hotNumber("##fps", comp.fps, 1.0f, 120.0f, 0.25f, "%.0f", "fps", 60.0f);

    dimLabel("BPM");
    float bpm = comp.bpm;
    if (hotNumber("##bpmTab", bpm, 20.0f, 300.0f, 0.1f, "%.2f", nullptr, 120.0f))
        comp.bpm = bpm;

    dimLabel("Master");
    hotDmx({dmx::ParamKind::MasterOpacity});
    hotPercent("##masterTab", comp.masterOpacity, 1.0f, -1.0f);
    ImGui::Checkbox("Bypass master", &comp.masterBypass);

    ImGui::Spacing();
    ImGui::SeparatorText("Efeitos do master");
    drawEffectChain(comp.masterEffects, "master", &ctx, kZoneSourceMaster, EffectOwner{2, -1, -1, -1});

    endPanel();
}

void drawLayerTabPanel(PanelContext& ctx)
{
    beginPanel(panel::kLayerTab);
    Composition& comp = ctx.composition;
    const Selection& sel = ctx.selection;

    if (sel.kind != Selection::Kind::None && sel.layer >= 0 && sel.layer < comp.layerCount()) {
        Layer& layer = comp.layers[static_cast<size_t>(sel.layer)];

        dimLabel("Nome");
        char name[128];
        std::snprintf(name, sizeof(name), "%s", layer.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##nomeCamada", name, sizeof(name)))
            layer.name = name;

        dimLabel("Opacidade");
        hotDmx({dmx::ParamKind::LayerOpacity, layer.id});
        hotPercent("##opacidade", layer.opacity, 1.0f, -1.0f);

        dimLabel("Blend mode");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (beginCombo("##blend", blendModeName(layer.blendMode))) {
            for (int m = 0; m < kBlendModeCount; ++m) {
                const auto mode = static_cast<BlendMode>(m);
                if (ImGui::Selectable(blendModeName(mode), mode == layer.blendMode))
                    layer.blendMode = mode;
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginPopupContextItem("##blendctx_aba")) {
            dmxParamMenu({dmx::ParamKind::LayerBlend, layer.id});
            ImGui::EndPopup();
        }

        ImGui::Checkbox("Bypass", &layer.bypass);
        ImGui::SameLine(0.0f, dp(20.0f));
        ImGui::Checkbox("Solo", &layer.solo);

        ImGui::Spacing();
        ImGui::BeginDisabled(layer.activeColumn < 0);
        if (ImGui::Button("Parar camada"))
            comp.stopLayer(sel.layer);
        ImGui::EndDisabled();

        ImGui::Spacing();
        drawTransformFields(ctx, sel.layer);

        ImGui::Spacing();
        ImGui::SeparatorText("Efeitos da camada");
        drawEffectChain(layer.effects, "camada", &ctx, sel.layer, EffectOwner{1, -1, sel.layer, -1});
    } else {
        ImGui::TextColored(theme::kTextDim, "Selecione uma camada ou um clip para editar a camada.");
    }

    endPanel();
}

void drawOutputTabPanel(PanelContext& ctx)
{
    beginPanel(panel::kOutputTab);

    const std::vector<MonitorInfo>& monitors = ctx.outputRenderer.monitors();
    if (ctx.output.screens.empty())
        ImGui::TextColored(theme::kTextDim, "Nenhuma tela configurada.");

    for (const Screen& screen : ctx.output.screens) {
        ImGui::PushID(screen.id);
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
        ImGui::TextUnformatted(screen.name.c_str());
        ImGui::PopFont();

        ImGui::TextColored(theme::kTextDim, "%s", destinationLabel(screen, monitors).c_str());
        ImGui::SameLine();
        switch (ctx.outputRenderer.status(screen.id)) {
        case ScreenStatus::Active: ImGui::TextColored(theme::kSuccess, "Saindo"); break;
        case ScreenStatus::MonitorMissing: ImGui::TextColored(theme::kWarning, "Monitor não encontrado"); break;
        case ScreenStatus::Error: ImGui::TextColored(theme::kWarning, "Falha ao abrir a saída"); break;
        default: ImGui::TextColored(theme::kTextDim, screen.enabled ? "Sem destino" : "Desativada"); break;
        }
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%d x %d  |  %zu slice(s)", screen.width, screen.height,
                           screen.slices.size());
        popMono();
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Abrir Mapeamento"))
        ctx.mode = AppMode::Mapping;

    endPanel();
}

}
