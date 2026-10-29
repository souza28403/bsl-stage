#include "ui/panels/ZoneRouting.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/MapView.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <vector>

namespace bsl::ui {

namespace {

struct SliceRef
{
    int screen = 0;
    int slice = 0;
};

// Estado do assistente aberto (so um por vez: e um modal)
struct Wizard
{
    int zones = 3;
    int source = kZoneSourceMaster;
    int pick[4] = {-1, -1, -1, -1}; // indice na lista de slices; -1 = nenhum
    bool open = false;
};
Wizard g_wizard;

std::vector<SliceRef> allSlices(const OutputSetup& out)
{
    std::vector<SliceRef> list;
    for (int s = 0; s < static_cast<int>(out.screens.size()); ++s)
        for (int i = 0; i < static_cast<int>(out.screens[static_cast<size_t>(s)].slices.size()); ++i)
            list.push_back({s, i});
    return list;
}

Slice& sliceAt(OutputSetup& out, const SliceRef& r)
{
    return out.screens[static_cast<size_t>(r.screen)].slices[static_cast<size_t>(r.slice)];
}

std::string sliceLabel(const OutputSetup& out, const SliceRef& r)
{
    const Screen& s = out.screens[static_cast<size_t>(r.screen)];
    return s.name + " / " + s.slices[static_cast<size_t>(r.slice)].name;
}

// Sugestao inicial: o que ja esta roteado para esta fonte; o resto, os proximos slices ainda sem zona
void prefill(const OutputSetup& out, const std::vector<SliceRef>& list)
{
    for (int& p : g_wizard.pick)
        p = -1;
    for (int z = 0; z < g_wizard.zones; ++z)
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            const Slice& s = out.screens[static_cast<size_t>(list[static_cast<size_t>(i)].screen)]
                                 .slices[static_cast<size_t>(list[static_cast<size_t>(i)].slice)];
            if (s.zFilter == z + 1 && s.zSource == g_wizard.source) {
                g_wizard.pick[z] = i;
                break;
            }
        }
    for (int z = 0; z < g_wizard.zones; ++z) {
        if (g_wizard.pick[z] >= 0)
            continue;
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            const Slice& s = out.screens[static_cast<size_t>(list[static_cast<size_t>(i)].screen)]
                                 .slices[static_cast<size_t>(list[static_cast<size_t>(i)].slice)];
            const bool taken = std::find(std::begin(g_wizard.pick), std::end(g_wizard.pick), i) != std::end(g_wizard.pick);
            if (!taken && s.zFilter == kZoneAll) {
                g_wizard.pick[z] = i;
                break;
            }
        }
    }
}

void apply(PanelContext& ctx, const std::vector<SliceRef>& list)
{
    mapHistoryCommit(ctx); // o que estava pendente vira uma operacao propria
    // Os slices que estavam roteados para esta fonte e ficaram sem zona na tabela voltam a mostrar tudo
    for (const SliceRef& r : list) {
        Slice& s = sliceAt(ctx.output, r);
        if (s.zSource == g_wizard.source && s.zFilter >= kZoneFront && s.zFilter < kZoneFront + g_wizard.zones)
            s.zFilter = kZoneAll;
    }
    for (int z = 0; z < g_wizard.zones; ++z) {
        const int p = g_wizard.pick[z];
        if (p < 0 || p >= static_cast<int>(list.size()))
            continue;
        Slice& s = sliceAt(ctx.output, list[static_cast<size_t>(p)]);
        s.zFilter = kZoneFront + z;
        s.zSource = g_wizard.source;
    }
    mapHistoryCommit(ctx); // uma operacao de desfazer para tudo
}

} // namespace

std::string zoneSourceLabel(const Composition& comp, int zoneSource)
{
    if (zoneSource < 0)
        return "Master";
    std::string label = "Camada " + std::to_string(zoneSource + 1);
    if (zoneSource < comp.layerCount() && !comp.layers[static_cast<size_t>(zoneSource)].name.empty())
        label += ": " + comp.layers[static_cast<size_t>(zoneSource)].name;
    return label;
}

void drawZoneRoutingButton(PanelContext& ctx, const media::ParamSet& params, int zoneSource, const std::string& id)
{
    const std::string popupName = "Rotear zonas para slices##" + id + "_zrota";
    if (ImGui::Button(("Rotear zonas para slices…##" + id + "_zrotear").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
        g_wizard.zones = params.s("zones") == "4" ? 4 : 3;
        g_wizard.source = zoneSource;
        prefill(ctx.output, allSlices(ctx.output));
        g_wizard.open = true;
        ImGui::OpenPopup(popupName.c_str());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Escolhe qual slice do Mapeamento recebe cada zona de profundidade deste efeito e aplica de uma vez "
                          "(dá para desfazer no Mapeamento)");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(popupName.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    const std::vector<SliceRef> list = allSlices(ctx.output);
    ImGui::TextColored(theme::kTextDim, "Fonte das zonas: %s", zoneSourceLabel(ctx.composition, g_wizard.source).c_str());
    ImGui::Spacing();
    if (list.empty()) {
        ImGui::TextUnformatted("Não há slices no Mapeamento. Crie slices no modo Mapeamento primeiro.");
    } else if (ImGui::BeginTable("##zrota_tabela", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("zona", ImGuiTableColumnFlags_WidthFixed, dp(90.0f));
        ImGui::TableSetupColumn("slice", ImGuiTableColumnFlags_WidthFixed, dp(280.0f));
        for (int z = 0; z < g_wizard.zones; ++z) {
            ImGui::PushID(z);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(zoneFilterName(kZoneFront + z));
            ImGui::TableSetColumnIndex(1);
            const int cur = g_wizard.pick[z];
            const std::string preview = cur >= 0 && cur < static_cast<int>(list.size())
                                            ? sliceLabel(ctx.output, list[static_cast<size_t>(cur)])
                                            : std::string("(nenhum)");
            ImGui::SetNextItemWidth(-FLT_MIN);
            const std::string comboId = "##" + id + "_zrota_zona_" + std::to_string(z + 1);
            if (beginCombo(comboId.c_str(), preview.c_str())) {
                if (ImGui::Selectable(("(nenhum)##zrota_nenhum_" + std::to_string(z)).c_str(), cur < 0))
                    g_wizard.pick[z] = -1;
                for (int i = 0; i < static_cast<int>(list.size()); ++i) {
                    const std::string label = sliceLabel(ctx.output, list[static_cast<size_t>(i)]) + "##zrota_s" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), cur == i)) {
                        for (int& other : g_wizard.pick) // um slice recebe uma zona so
                            if (other == i)
                                other = -1;
                        g_wizard.pick[z] = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextColored(theme::kTextDim, "Cada slice mostra só a sua zona; os não escolhidos ficam como estavam.");
    ImGui::Spacing();

    ImGui::BeginDisabled(list.empty());
    if (ImGui::Button(("Aplicar##" + id + "_zrota_aplicar").c_str(), ImVec2(dp(120.0f), 0.0f))) {
        apply(ctx, list);
        g_wizard.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(("Cancelar##" + id + "_zrota_cancelar").c_str(), ImVec2(dp(120.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_wizard.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace bsl::ui
