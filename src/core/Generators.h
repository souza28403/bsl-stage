#pragma once

#include "media/ISource.h"

#include <functional>
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
inline constexpr const char* kNdiId = "gen:ndi";
inline constexpr const char* kSpoutId = "gen:spout";
// Geradores ISF: "isf:<caminho relativo a pasta ISF>" (ver fx/IsfSource)
inline constexpr const char* kIsfPrefix = "isf:";
inline bool isIsfGeneratorId(const std::string& id) { return id.rfind(kIsfPrefix, 0) == 0; }
// Geradores FFGL (plugins do tipo fonte): "ffgl:<id>:<arquivo>" (ver ffgl/FfglSource)
inline constexpr const char* kFfglPrefix = "ffgl:";
inline bool isFfglGeneratorId(const std::string& id) { return id.rfind(kFfglPrefix, 0) == 0; }
// Fontes BSL Light nativas (GenBackgrounds, LEDMapperPro): "bsl:<nome>" (ver fx/bsl/BslSources)
inline constexpr const char* kBslPrefix = "bsl:";
inline bool isBslGeneratorId(const std::string& id) { return id.rfind(kBslPrefix, 0) == 0; }
// Fontes BSL Light que vivem no processo auxiliar (CEF / PDF e Office): tratadas como Web / documento paginado
inline constexpr const char* kBslWebBackgroundsId = "bsl:web-backgrounds";
inline constexpr const char* kBslWebMotionId = "bsl:web-motion";
inline constexpr const char* kBslPptManagerId = "bsl:ppt-manager";

// Registro dos geradores: tudo que o resto do app precisa saber de cada um sem conhecer a classe.
struct GeneratorInfo
{
    const char* id;
    const char* name;   // nome do tipo (PT)
    bool live;          // fonte ao vivo (dispositivo/captura): exclusiva, a Previa nao abre outra instancia
    bool needsSetup;    // ao ser colocado numa celula, pede uma escolha (ex.: alvo da captura)
    std::function<std::vector<media::ParamDesc>(const media::ParamValues& values)> describe;
    std::function<std::unique_ptr<media::ISource>(int canvasWidth, int canvasHeight)> create;
    std::function<std::string(const media::ParamValues& values)> label; // nome do clip na celula (vazio = name)
};

std::span<const GeneratorInfo> allGenerators();
const GeneratorInfo* findGenerator(const std::string& id);

} // namespace bsl
