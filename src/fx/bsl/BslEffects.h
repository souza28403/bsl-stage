#pragma once

// Os plugins BSL Light reescritos como efeitos nativos (provedor "bsl:" do EffectRegistry). Cada um tem um descritor de
// parametros (sem GL) e uma fabrica.

#include "fx/IEffect.h"

#include <memory>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

// --- Efeitos ---
std::vector<media::ParamDesc> describeAlphaReactor();
std::unique_ptr<IEffect> makeAlphaReactor();

std::vector<media::ParamDesc> describeDatamosh();
std::unique_ptr<IEffect> makeDatamosh();

std::vector<media::ParamDesc> describeAudioTypo();
std::unique_ptr<IEffect> makeAudioTypo();

std::vector<media::ParamDesc> describeSliceRefiner();
std::unique_ptr<IEffect> makeSliceRefiner();

// BSL Slice Refiner (BSR3): 8 grupos de slices com borda animada, editor nativo e controle remoto pela rede
std::vector<media::ParamDesc> describeSliceRefiner3();
std::unique_ptr<IEffect> makeSliceRefiner3();

// BSL VHS (nativo, sem IA)
std::vector<media::ParamDesc> describeVhs();
std::unique_ptr<IEffect> makeVhs();

// Efeitos com IA local (ai::AiEngine): Relight 2.5D (profundidade) e Cybernetic Body-Parts Glitch (pessoa + profundidade)
std::vector<media::ParamDesc> describeRelight();
std::unique_ptr<IEffect> makeRelight();

std::vector<media::ParamDesc> describeBodyGlitch();
std::unique_ptr<IEffect> makeBodyGlitch();

// Volumetric Performer Aura & Fluid Collisions: a borda da mascara de pessoa em movimento empurra um fluido 2D na GPU
// (aura/fumaca, particulas ou grade de lasers)
std::vector<media::ParamDesc> describeAura();
std::unique_ptr<IEffect> makeAura();

// Z-Slice LED Mapper: fatia a imagem em 3 ou 4 zonas de profundidade e publica a mascara de zonas para os slices da saida
std::vector<media::ParamDesc> describeZSlice();
std::unique_ptr<IEffect> makeZSlice();

// --- Catalogo para a UI (efeitos e fontes juntos) ---
struct BslPluginInfo
{
    const char* id;          // typeId ("bsl:alpha-reactor")
    const char* name;        // nome exibido
    const char* category;
    const char* description; // tooltip (do MANUAL_DE_USO.md do plugin original, quando existe)
    bool generator;          // fonte (gerador) em vez de efeito
    bool ai = false;         // usa IA local (selo "IA" na lista de efeitos)
};
// Todos os plugins BSL desta etapa, na ordem de exibicao
const std::vector<BslPluginInfo>& bslPlugins();
const BslPluginInfo* findBslPlugin(const std::string& id);

} // namespace bsl::fx::bsllight
