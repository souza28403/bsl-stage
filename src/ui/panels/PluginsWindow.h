#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Janela "Plugins" (menu Ver e botão da aba Efeitos): pastas de plugins FFGL, plugins do Resolume (opcional), importar,
// reescanear e a lista de bloqueados com "Tentar de novo".
void openPluginsWindow();
void drawPluginsWindow(PanelContext& ctx);

} // namespace bsl::ui
