#pragma once

#include "media/SourceParams.h"
#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

// Assistente "Rotear zonas para slices…" do efeito Z-Slice: uma tabela com uma linha por zona (Frente, Meio, Fundo e, com
// 4 zonas, Zona 4) e um combo com todos os slices do Mapeamento; "Aplicar" grava Slice::zFilter / zSource de uma vez e
// entra no desfazer do Mapeamento. Chamado dentro do corpo do efeito (mesmo escopo de ID do popup).
//
// 'params' = parametros do Z-Slice (número de zonas); 'zoneSource' = kZoneSourceMaster ou o índice da camada onde o efeito
// está; 'id' = prefixo estável dos IDs dos controles (ex.: "camada_efeito_1").
void drawZoneRoutingButton(PanelContext& ctx, const media::ParamSet& params, int zoneSource, const std::string& id);

// Nome da fonte de zonas para a interface: "Master" ou "Camada N: nome"
std::string zoneSourceLabel(const Composition& comp, int zoneSource);

} // namespace bsl::ui
