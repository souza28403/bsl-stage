#pragma once

// Fontes (geradores) BSL Light nativas: GenBackgrounds e LEDMapperPro. Registradas em core/Generators como "bsl:...".

#include "core/Generators.h"
#include "media/ISource.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

std::vector<media::ParamDesc> describeGenBackgrounds();
std::unique_ptr<media::ISource> makeGenBackgrounds(int canvasW, int canvasH);

std::vector<media::ParamDesc> describeLedMapper();
std::unique_ptr<media::ISource> makeLedMapper(int canvasW, int canvasH);

// Padrão de calibração BSL: o padrão HD de cada painel do Pixel Map no seu retângulo escalado do canvas (ver PixelMapFeed)
std::vector<media::ParamDesc> describePixelMapPattern();
std::unique_ptr<media::ISource> makePixelMapPattern(int canvasW, int canvasH);

// BSL WebBackgrounds (WBBG) e BSL WebMotion (WB01): paginas web no processo auxiliar (CEF), controladas por parametros
// nativos. 'values' escolhe os ajustes do fundo (WebBackgrounds).
std::vector<media::ParamDesc> describeWebBackgrounds(const media::ParamValues& values);
std::unique_ptr<media::ISource> makeWebBackgrounds(int canvasW, int canvasH);
std::vector<media::ParamDesc> describeWebMotion(const media::ParamValues& values);
std::unique_ptr<media::ISource> makeWebMotion(int canvasW, int canvasH);
// O CEF esta presente? (sem ele as duas fontes web ficam desabilitadas)
bool bslWebAvailable();

// BSL PPT Manager (BSPT): a fonte PDF/Office do app com o parametro "Documento" (como o plugin) e os extras do original
std::vector<media::ParamDesc> describePptManager(const media::ParamValues& values, int pages = 0);
std::unique_ptr<media::ISource> makePptManager(int canvasW, int canvasH);

// Informacao dos geradores "bsl:..." para o registro de Generators (nullptr se o id nao for de uma fonte BSL)
const GeneratorInfo* bslGeneratorInfo(const std::string& id);

// --- LED Mapper: exportacao do XML no formato Advanced Output do Resolume 7 ---
struct LedGrid
{
    int columns = 1;
    int rows = 1;
    float cabinetW = 128.0f; // px
    float cabinetH = 128.0f;
    float gapX = 0.0f;
    float gapY = 0.0f;
};
// Um Screen com um Slice por cabinet (InputRect = OutputRect). 'seed' gera os uniqueId (o plugin usava rand()).
std::string buildLedMapperXml(const LedGrid& grid, std::uint32_t seed);
bool writeLedMapperXml(const std::filesystem::path& file, const LedGrid& grid, std::string* error = nullptr);

} // namespace bsl::fx::bsllight
