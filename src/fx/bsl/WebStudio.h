#pragma once

// Estado da pagina do BSL WebBackgrounds ("BSL Studio") como parametros nativos. O renderizador (assets/bsl/web_bg/index.html)
// recebe o estado inteiro por window.applyState(json); o painel web original (control.html) manda o mesmo JSON por
// POST /api/action?cmd=state. Aqui ficam os dois sentidos: parametros do clip -> estado, e estado -> parametros.
//
// Ids dos parametros: o caminho do campo no estado com '.' trocado por '_' (fx.brightness -> fx_brightness); os ajustes
// de cada fundo ficam em bgp_<fundo>_<ajuste>.

#include "media/SourceParams.h"

#include <string>
#include <vector>

namespace bsl::fx::bsllight {

// Dados do BSL Studio lidos de assets/bsl/web_bg/studio.json (fundos, ajustes de cada fundo, paletas, fontes, animacoes)
struct StudioBgParam
{
    std::string id, label, type; // type: number | color | select
    double min = 0.0, max = 1.0, step = 0.01;
    media::Json def;
    std::vector<std::string> options;
};
struct StudioBackground
{
    std::string id, label, category;
    std::vector<StudioBgParam> params;
};
struct StudioPalette
{
    std::string id, name;
    std::vector<std::string> colors; // 5 cores #rrggbb
};
struct StudioNamed
{
    std::string id, label;
    std::vector<int> weights;
};
struct StudioData
{
    std::vector<StudioBackground> backgrounds;
    std::vector<StudioPalette> palettes;
    std::vector<StudioNamed> fonts, textIn, textLoop, textOut;
    bool loaded = false;
};
const StudioData& studioData();

// Parametros nativos do BSL WebBackgrounds (sem a rede e a pagina): fundo, cores, efeitos, texto/logo
std::vector<media::ParamDesc> describeStudioParams(const media::ParamValues& values);
// Estado completo da pagina a partir dos parametros
media::Json buildStudioState(const media::ParamSet& params);
// Estado (completo ou so um pedaco) vindo do painel remoto -> parametros. Devolve true se algum valor mudou.
bool applyStudioState(const media::Json& state, media::ParamValues& values);

} // namespace bsl::fx::bsllight
