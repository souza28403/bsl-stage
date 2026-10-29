#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Aba "Arquivos": navegador de pastas com mídias (arrastar para a grade ou duplo clique)
void drawBrowserPanel(PanelContext& ctx);
// Aba "Fontes" (id interno: geradores): seção BSL Light no topo, depois os geradores nativos, ISF e FFGL
void drawGeneratorsPanel(PanelContext& ctx);

} // namespace bsl::ui
