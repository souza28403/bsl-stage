#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Janela "Escolher alvo da captura": monitores e janelas com miniatura. Vale para a celula (deck, camada, coluna).
void requestCaptureTarget(int deck, int layer, int column);

// Desenhar uma vez por quadro (dentro do ImGui)
void drawCaptureTargetChooser(PanelContext& ctx);

// Chamar depois de colocar um clip numa celula do deck atual: se o gerador pede uma escolha (captura de tela),
// abre a janela de alvo.
void afterClipPlaced(const Composition& comp, int layer, int column);

} // namespace bsl::ui
