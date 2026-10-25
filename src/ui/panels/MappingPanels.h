#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Editor de mapeamento (modo "Mapeamento"): lista de Screens/Slices, entrada (canvas),
// saída (Screen selecionada) e propriedades.

// Corrige a seleção se Screens/Slices foram removidos
void validateMappingSelection(PanelContext& ctx);

void drawMappingListPanel(PanelContext& ctx);
void drawMappingInputPanel(PanelContext& ctx);
void drawMappingOutputPanel(PanelContext& ctx);
void drawMappingPropertiesPanel(PanelContext& ctx);

} // namespace bsl::ui
