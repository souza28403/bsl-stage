#pragma once

#include "ui/PanelContext.h"
#include "ui/Theme.h"

namespace bsl::ui {

// Transporte global (play/pause/stop), BPM, TAP, RESYNC, ÷2/×2 e indicador de beat.
// Desenha dentro da janela atual (o painel Composição).
void drawTransportBar(PanelContext& ctx);

// Altura da barra, para o painel reservar o espaço
inline float transportBarHeight() { return dp(28.0f); }

// A barra cabe numa linha com ~700 dp de largura; mais estreita, TAP/RESYNC/div2/mul2/beat vao para uma segunda linha
inline int transportRows(float availWidth) { return availWidth < dp(700.0f) ? 2 : 1; }

} // namespace bsl::ui
