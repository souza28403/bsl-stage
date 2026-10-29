#pragma once

#include "ui/PanelContext.h"

#include <string>
#include <vector>

#include <windows.h>

namespace bsl::ui {

// Presets de layout do usuário (Ver > Layout): salvar o layout atual, aplicar, gerenciar (renomear, excluir,
// exportar/importar .bsllayout) e Alt+1..9. Os arquivos ficam em %APPDATA%\BSL Stage\layouts\*.json.

// Itens do menu Ver > Layout depois dos layouts prontos (dentro do BeginMenu)
void drawLayoutPresetsMenu(PanelContext& ctx);
// Alt+1..9 aplicam os presets na ordem da lista
void handleLayoutShortcuts(PanelContext& ctx);
// Antes do DockSpaceOverViewport: aplica o pedido pendente e, no primeiro quadro, restaura o último preset usado
void applyPendingLayout(PanelContext& ctx);
// Janela "Salvar layout" e janela "Gerenciar layouts"
void drawLayoutWindows(PanelContext& ctx);
// Fora do desenho do ImGui (antes do NewFrame): diálogos de arquivo de Exportar/Importar
void runLayoutDialogs(HWND owner);
// Escolheu um layout pronto: o preset do usuário deixa de ser o ativo
void clearActiveLayoutPreset(PanelContext& ctx);

struct LayoutUiInfo
{
    std::vector<std::string> presets; // na ordem da lista
};
LayoutUiInfo layoutUiInfo();

} // namespace bsl::ui
