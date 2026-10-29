#pragma once

#include "ui/PanelContext.h"

#include <vector>

namespace bsl::ui {

// Vistas Entrada e Saida do editor de mapeamento: UM codigo para as duas (selecao multipla, mover, redimensionar, girar,
// zoom/pan, regua e grade, ima com linhas-guia, alinhar/distribuir/encostar, teclado e desfazer/refazer).
enum class MapViewKind { Input, Output };

// Desenha o painel inteiro (Begin..End) da vista
void drawMapView(PanelContext& ctx, MapViewKind kind);

// Uma vez por frame no modo Mapeamento, antes dos paineis: registra no historico o que mudou (mover, propriedades...)
void mapHistoryObserve(PanelContext& ctx);
// Registra agora, como uma operacao propria, o que mudou no OutputSetup desde o ultimo registro (fora do modo
// Mapeamento: o assistente "Rotear zonas para slices" chama antes e depois de aplicar)
void mapHistoryCommit(PanelContext& ctx);
// Projeto novo/aberto: esquece o historico e recentra as vistas
void mapEditorReset(const OutputSetup& output);

// Duplicar / remover todos os slices selecionados (Ctrl+D, Delete e os botoes da Lista)
void mapDuplicateSelection(PanelContext& ctx);
void mapRemoveSelection(PanelContext& ctx);

// --- Selecao (compartilhada com a Lista e as Propriedades) ---
bool isSliceSelected(const MappingSelection& sel, int slice);
void selectOnly(MappingSelection& sel, int slice);                 // -1 = limpa
void toggleSliceSelection(MappingSelection& sel, int slice);       // Ctrl+clique
void setSliceSelection(MappingSelection& sel, std::vector<int> slices, int primary);
// Corrige 'multi' depois de remocoes ou de mudancas diretas em screen/slice
void normalizeSelection(MappingSelection& sel, int sliceCount);

// Resumo do estado do editor para o modo --qa (snapshot.json)
struct MapEditorInfo
{
    int undoCount = 0;
    int redoCount = 0;
    bool snap = true;
    bool grid = false;
    int gridStep = 16;
    float inputZoom = 1.0f;
    float outputZoom = 1.0f;
    bool dragging = false;
    int guides = 0; // linhas-guia visiveis agora (arraste com ima)
};
MapEditorInfo mapEditorInfo();

} // namespace bsl::ui
