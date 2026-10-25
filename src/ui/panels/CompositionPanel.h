#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Composição: cabeçalho master + camadas (controles fixos à esquerda) + células de clip (rolam na
// horizontal) + abas de deck + barra de transporte.
void drawCompositionPanel(PanelContext& ctx);

} // namespace bsl::ui
