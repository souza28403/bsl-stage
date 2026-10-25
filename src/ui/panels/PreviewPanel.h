#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

// Saída: textura do Compositor (o que vai para as telas), mantendo a proporção.
// Com o canvas vazio, mostra o monograma BSL a 20%.
void drawOutputPanel(PanelContext& ctx);

// Prévia: toca o clip selecionado numa instância de fonte própria (não afeta a saída), sobre um
// fundo quadriculado. No perfil Eco mostra só a miniatura.
void drawPreviewPanel(PanelContext& ctx);

// Fecha a instância da prévia (chamar com o contexto GL ainda vivo, antes de encerrar)
void releasePreview();

} // namespace bsl::ui
