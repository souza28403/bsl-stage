#pragma once

#include "media/ISource.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bsl {

// Identificadores dos geradores (gravados no projeto e usados no arrastar/soltar)
inline constexpr const char* kTestPatternId = "gen:padrao-de-teste";
inline constexpr const char* kSolidColorId = "gen:cor-solida";
inline constexpr const char* kTextId = "gen:texto";
inline constexpr const char* kScreenCaptureId = "gen:captura-de-tela";
inline constexpr const char* kCaptureDeviceId = "gen:placa-de-captura";
inline constexpr const char* kWebUrlId = "gen:web-url";

// Registro dos geradores: tudo que o resto do app precisa saber de cada um sem conhecer a classe.
struct GeneratorInfo
{
    const char* id;
    const char* name;   // nome do tipo (PT)
    bool live;          // fonte ao vivo (dispositivo/captura): exclusiva, a Previa nao abre outra instancia
    bool needsSetup;    // ao ser colocado numa celula, pede uma escolha (ex.: alvo da captura)
    std::vector<media::ParamDesc> (*describe)(const media::ParamValues& values);
    std::unique_ptr<media::ISource> (*create)(int canvasWidth, int canvasHeight);
    std::string (*label)(const media::ParamValues& values); // nome do clip na celula (nullptr = name)
};

std::span<const GeneratorInfo> allGenerators();
const GeneratorInfo* findGenerator(const std::string& id);

} // namespace bsl
