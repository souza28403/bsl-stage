#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Janela "IA" (menu Ver): estado da IA local opcional (ONNX Runtime + DirectML), dispositivo em uso, ms por inferência,
// fps de IA, modelos carregados, taxa e dispositivo configuráveis e o botão "Testar". Desligada no perfil Eco.
void openAiWindow();
void drawAiWindow(PanelContext& ctx);

} // namespace bsl::ui
