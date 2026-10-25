#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Aba "Arquivos": navegador de pastas com mídias (arrastar para a grade ou duplo clique)
void drawBrowserPanel(PanelContext& ctx);
// Aba "Geradores": Padrão de teste (arrastável) e geradores futuros desabilitados
void drawGeneratorsPanel(PanelContext& ctx);

} // namespace bsl::ui
