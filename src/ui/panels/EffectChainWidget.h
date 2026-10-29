#pragma once

#include "fx/EffectChain.h"
#include "ui/PanelContext.h"

#include <imgui.h>

#include <string>

namespace bsl::ui {

// Payloads de arrastar e soltar dos efeitos
inline constexpr const char* kEffectPayload = "BSL_EFFECT";          // dados: typeId (texto terminado em NUL)
inline constexpr const char* kEffectMovePayload = "BSL_EFFECT_MOVE"; // dados: EffectMoveRef (reordenar dentro da cadeia)

struct EffectMoveRef
{
    const fx::EffectChain* chain;
    int index;
};

// Widget da cadeia de efeitos, o mesmo no painel Clip, na aba Camada e na aba Composição (master).
// Cada efeito tem um cabeçalho recolhível (alça de arrastar, ícone, nome, bypass, menu) e os parâmetros logo abaixo.
// Aceita soltar um efeito (da aba Efeitos) no cabeçalho de um efeito (entra antes dele) ou na área tracejada do fim
// (entra no fim), e reordenar arrastando a alça.
//
// 'id' curto e estável ("clip", "camada", "master"): compõe os IDs dos controles (ex.: "##clip_efeito_1_bypass"),
// que o teste de interface (uiqa) usa para clicar. Devolve true se a cadeia mudou (efeito novo, removido, movido
// ou parâmetro editado).
//
// 'ctx' e 'zoneSource' (so nas cadeias da camada e do master) ligam o assistente "Rotear zonas para slices..." do efeito
// Z-Slice: 'zoneSource' = kZoneSourceMaster ou o indice da camada. Sem 'ctx', o efeito nao mostra o botao.
inline constexpr int kNoZoneSource = -2;
bool drawEffectChain(fx::EffectChain& chain, const char* id, PanelContext* ctx = nullptr, int zoneSource = kNoZoneSource);

// Minúsculas sem acento (PT), para busca: "distorcao" acha "Distorção"
std::string foldSearchText(const std::string& s);

// Aceita um efeito solto no último item desenhado (célula, cabeçalho de camada...). Devolve o typeId solto, ou vazio.
std::string acceptEffectDrop();

// Pequeno ícone "sparkles" no canto de uma célula/camada que tem efeitos
void drawEffectsBadge(ImDrawList* dl, ImVec2 center, const fx::EffectChain& chain);

} // namespace bsl::ui
