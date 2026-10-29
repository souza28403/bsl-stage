#pragma once

// Editor nativo do BSL Slice Refiner (substitui a aba "Slices" do painel web da porta 8083): preview dos slices com selecao,
// lista, atribuicao a 8 grupos e todos os controles de cada grupo (formato, borda, efeito, animacao). Desenhado no lugar do
// parametro Custom "bsl-slices" (ParamDesc::custom) do efeito BSL Slice Refiner.

#include "media/SourceParams.h"

namespace bsl::ui {

// 'params' = todos os parametros do efeito (a origem dos slices e o arquivo XML); 'stateId' = id do parametro com o
// estado. Devolve true com o novo estado (JSON v1 do Slice Refiner) em 'out' quando o usuario mudou algo.
bool drawSliceEditor(const media::ParamSet& params, const std::string& stateId, media::Json& out);

} // namespace bsl::ui
