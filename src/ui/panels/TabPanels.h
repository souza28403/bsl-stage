#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Abas do canto inferior direito da Composição (Arquivos e Geradores ficam no BrowserPanel)
void drawEffectsPanel(PanelContext& ctx); // implementado em ui/panels/EffectsPanel.cpp
// Testes (--fx-open nativos|isf|ffgl): so essa secao da aba Efeitos nasce aberta
void setEffectsPanelOnlyOpen(const std::string& section);
void drawCompositionTabPanel(PanelContext& ctx);
void drawLayerTabPanel(PanelContext& ctx);
void drawOutputTabPanel(PanelContext& ctx);

} // namespace bsl::ui
