#pragma once

#include "core/Clip.h"
#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

// O que a Prévia mostra
enum class PreviewSource {
    Clip,  // o clip selecionado na grade (padrão): instância própria, não mexe na saída
    Layer, // a camada selecionada: o que ela toca agora, com o blend e a opacidade dela sobre preto
};

// Opções da Prévia (guardadas nos presets de layout)
struct PreviewOptions
{
    PreviewSource source = PreviewSource::Clip;
    bool checker = true;          // quadriculado atrás do alfa
    FitMode fit = FitMode::Fit;   // Ajustar ou Preencher
};
PreviewOptions& previewOptions();

// Situação da Prévia no último quadro (modo de teste --qa e testes de interface)
struct PreviewInfo
{
    bool layerMode = false;
    int layer = -1;        // camada/coluna que a Prévia mostra (-1 = nenhuma)
    int column = -1;
    bool hasClip = false;
    bool live = false;     // o clip mostrado está tocando na saída (a Prévia espelha a instância da camada)
    bool ownSource = false; // a Prévia tem instância própria aberta
    bool playing = false;  // transporte da Prévia
    double position = 0.0;
    double duration = 0.0;
};
PreviewInfo previewInfo();

// Saída: textura do Compositor (o que vai para as telas), mantendo a proporção.
// Com o canvas vazio, mostra o monograma BSL a 20%.
void drawOutputPanel(PanelContext& ctx);

// Prévia: mostra o clip (ou a camada) selecionado sem mexer na saída. Um clip que ainda não está ao vivo toca numa
// instância própria da Prévia, com transporte próprio; "Ao vivo" o dispara na camada dele. No perfil Eco mostra só a
// miniatura.
void drawPreviewPanel(PanelContext& ctx);

// Fecha a instância da prévia (chamar com o contexto GL ainda vivo, antes de encerrar)
void releasePreview();

} // namespace bsl::ui
