#pragma once

#include "ui/PanelContext.h"

#include <imgui.h>

namespace bsl::ui {

// Janela "Áudio" (menu Ver e clique no medidor da barra de status): fonte da captura (som do PC ou entrada), dispositivo,
// ganho, noise gate, ataque/release das bandas e o painel ao vivo (bandas, espectro, batida) do AudioAnalyzer.
void openAudioWindow();
void drawAudioWindow(PanelContext& ctx);

// Medidor de nível pequeno (barra de status): desenha 'size' px e devolve true se foi clicado
bool audioMeter(const char* id, ImVec2 size);

} // namespace bsl::ui
