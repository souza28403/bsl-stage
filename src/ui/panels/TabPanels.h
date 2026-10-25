#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Abas do canto inferior direito da Composição (Arquivos e Geradores ficam no BrowserPanel)
void drawEffectsPanel(PanelContext& ctx);
void drawCompositionTabPanel(PanelContext& ctx);
void drawLayerTabPanel(PanelContext& ctx);
void drawOutputTabPanel(PanelContext& ctx);

} // namespace bsl::ui
