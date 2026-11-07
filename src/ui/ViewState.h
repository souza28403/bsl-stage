#pragma once

#include "ui/PanelNames.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bsl::ui::view {

enum class PanelMode { Composition, Mapping, PixelMap, Any };

struct PanelDef
{
    const char* key;
    const char* label;
    const char* window;
    PanelMode mode;
    const char* group;
};

inline constexpr const char* kStatusWindow = "##statusbar";

inline constexpr PanelDef kPanels[] = {
    {"saida", "Saída", panel::kOutput, PanelMode::Composition, "monitores"},
    {"previa", "Prévia", panel::kPreview, PanelMode::Composition, "monitores"},
    {"composicao", "Composição (grade de clips)", panel::kComposition, PanelMode::Composition, "grade"},
    {"clip", "Clip", panel::kClip, PanelMode::Composition, "grade"},
    {"arquivos", "Arquivos", panel::kFiles, PanelMode::Composition, "abas"},
    {"fontes", "Fontes", panel::kGenerators, PanelMode::Composition, "abas"},
    {"efeitos", "Efeitos", panel::kEffects, PanelMode::Composition, "abas"},
    {"aba-composicao", "Aba Composição", panel::kCompositionTab, PanelMode::Composition, "abas"},
    {"aba-camada", "Aba Camada", panel::kLayerTab, PanelMode::Composition, "abas"},
    {"aba-saida", "Aba Saída", panel::kOutputTab, PanelMode::Composition, "abas"},
    {"map-lista", "Lista", panel::kMapList, PanelMode::Mapping, "mapeamento"},
    {"map-entrada", "Entrada", panel::kMapInput, PanelMode::Mapping, "mapeamento"},
    {"map-saida", "Saída", panel::kMapOutput, PanelMode::Mapping, "mapeamento"},
    {"map-propriedades", "Propriedades", panel::kMapProps, PanelMode::Mapping, "mapeamento"},
    {"pm-config", "Configuração", panel::kPmConfig, PanelMode::PixelMap, "pixelmap"},
    {"pm-overview", "Overview", panel::kPmOverview, PanelMode::PixelMap, "pixelmap"},
    {"pm-tabela", "Tabela", panel::kPmTable, PanelMode::PixelMap, "pixelmap"},
    {"status", "Barra de status", kStatusWindow, PanelMode::Any, "status"},
};
inline constexpr int kPanelCount = static_cast<int>(sizeof(kPanels) / sizeof(kPanels[0]));

const PanelDef* findPanel(std::string_view key);
const PanelDef* findPanelByWindow(std::string_view window);
int panelIndex(std::string_view key);

struct State
{
    std::vector<std::string> hidden;
    std::vector<std::string> detached;
    std::map<std::string, unsigned> dockIds;
    std::map<std::string, std::array<float, 4>> dockRects;
    bool locked = false;

    bool isHidden(std::string_view key) const;
    bool isDetached(std::string_view key) const;
    bool setHidden(std::string_view key, bool hide);
    bool setDetached(std::string_view key, bool on);
    bool setDockId(std::string_view key, unsigned id);
    bool setDockRect(std::string_view key, const std::array<float, 4>& rect);
    bool showAll(PanelMode mode);
    bool operator==(const State&) const = default;
};

nlohmann::json toJson(const State& s);
State fromJson(const nlohmann::json& j);

std::vector<std::string> sanitizeKeys(const std::vector<std::string>& keys);

}
