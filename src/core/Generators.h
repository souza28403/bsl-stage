#pragma once

#include "media/ISource.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bsl {

inline constexpr const char* kTestPatternId = "gen:padrao-de-teste";
inline constexpr const char* kSolidColorId = "gen:cor-solida";
inline constexpr const char* kTextId = "gen:texto";
inline constexpr const char* kScreenCaptureId = "gen:captura-de-tela";
inline constexpr const char* kCaptureDeviceId = "gen:placa-de-captura";
inline constexpr const char* kWebUrlId = "gen:web-url";
inline constexpr const char* kNdiId = "gen:ndi";
inline constexpr const char* kSpoutId = "gen:spout";
inline constexpr const char* kNetStreamId = "gen:stream-de-rede";
inline constexpr const char* kIsfPrefix = "isf:";
inline bool isIsfGeneratorId(const std::string& id) { return id.rfind(kIsfPrefix, 0) == 0; }
inline constexpr const char* kFfglPrefix = "ffgl:";
inline bool isFfglGeneratorId(const std::string& id) { return id.rfind(kFfglPrefix, 0) == 0; }
inline constexpr const char* kBslPrefix = "bsl:";
inline bool isBslGeneratorId(const std::string& id) { return id.rfind(kBslPrefix, 0) == 0; }
inline constexpr const char* kBslWebBackgroundsId = "bsl:web-backgrounds";

struct GeneratorInfo
{
    const char* id;
    const char* name;
    bool live;
    bool needsSetup;
    std::function<std::vector<media::ParamDesc>(const media::ParamValues& values)> describe;
    std::function<std::unique_ptr<media::ISource>(int canvasWidth, int canvasHeight)> create;
    std::function<std::string(const media::ParamValues& values)> label;
};

std::span<const GeneratorInfo> allGenerators();
const GeneratorInfo* findGenerator(const std::string& id);

}
