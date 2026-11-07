#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

void drawEffectsPanel(PanelContext& ctx);
void setEffectsPanelOnlyOpen(const std::string& section);
void drawCompositionTabPanel(PanelContext& ctx);
void drawLayerTabPanel(PanelContext& ctx);
void drawOutputTabPanel(PanelContext& ctx);

}
