#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

// Modo "Pixel Map": a ferramenta BSL Light Pixel Mapping Pro dentro do BSL Stage.
//   Configuração (esquerda): input (ligado ao canvas), físico total, marca d'água, adicionar painel, mapeamento, exportar
//   Overview (centro): mapa ao vivo da composição com os painéis (selecionar, arrastar com ímã, zoom e pan)
//   Tabela (embaixo): identificação, físico, composição, "Gerar padrão HD", remover, edição inline
// Os dados vivem em Composition::pixelMap (vão para o projeto); aqui só o desenho e as ações.

// Uma vez por frame no modo Pixel Map, antes dos painéis: sincroniza o input com o canvas, limpa os dados e valida
void pixelMapPrepare(PanelContext& ctx);

void drawPixelMapConfigPanel(PanelContext& ctx);
void drawPixelMapOverviewPanel(PanelContext& ctx);
void drawPixelMapTablePanel(PanelContext& ctx);

// Projeto novo/aberto: esquece seleção e vista
void pixelMapReset();
// Antes de destruir o contexto GL: solta a textura da marca d'água
void pixelMapRelease();

// Resumo do estado da interface para o modo --qa (snapshot.json)
struct PixelMapUiInfo
{
    int selected = -1;
    bool snap = true;
    bool dragging = false;
    int guides = 0;
    float zoom = 1.0f;
    std::string status; // última mensagem de uma ação (exportar, criar mapeamento...)
};
PixelMapUiInfo pixelMapUiInfo();

} // namespace bsl::ui
