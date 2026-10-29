#pragma once

// Descricao de um plugin FFGL lida pelo processo de varredura (bslstage_worker --scan-ffgl): so dados, sem GL e sem
// o cabecalho do FFGL (que puxa windows.h com varios NO*). E o que o cache em disco guarda e o que a UI/registro usam.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bsl::ffgl {

using Json = nlohmann::json;

// Tipos de plugin (FF_EFFECT / FF_SOURCE / FF_MIXER)
inline constexpr unsigned kTypeEffect = 0;
inline constexpr unsigned kTypeSource = 1;
inline constexpr unsigned kTypeMixer = 2;

// Tipos de parametro (FF_TYPE_*)
inline constexpr unsigned kPBoolean = 0;
inline constexpr unsigned kPEvent = 1;
inline constexpr unsigned kPRed = 2;
inline constexpr unsigned kPGreen = 3;
inline constexpr unsigned kPBlue = 4;
inline constexpr unsigned kPXPos = 5;
inline constexpr unsigned kPYPos = 6;
inline constexpr unsigned kPStandard = 10;
inline constexpr unsigned kPOption = 11;
inline constexpr unsigned kPBuffer = 12;
inline constexpr unsigned kPInteger = 13;
inline constexpr unsigned kPFile = 14;
inline constexpr unsigned kPText = 100;
inline constexpr unsigned kPHue = 200;
inline constexpr unsigned kPSaturation = 201;
inline constexpr unsigned kPBrightness = 202;
inline constexpr unsigned kPAlpha = 203;

struct OptionInfo
{
    std::string name;
    float value = 0.0f;
};

struct ParamInfo
{
    int index = 0;
    std::string name;
    unsigned type = kPStandard;
    float defFloat = 0.0f;
    std::string defText;
    float min = 0.0f;
    float max = 1.0f;
    std::string group;
    unsigned usage = 0;      // FF_USAGE_*
    bool visible = true;
    std::vector<OptionInfo> options;       // so tipo Option
    std::vector<std::string> extensions;   // so tipo File (sem ponto)
};

struct PluginInfo
{
    std::string path;        // UTF-8, absoluto
    std::string fileName;    // nome do arquivo (ex.: "WebMotion.dll")
    std::uint64_t size = 0;  // bytes (chave do cache)
    std::int64_t mtime = 0;  // FILETIME em 100 ns (chave do cache)

    std::string pluginId;    // 4 caracteres
    std::string name;        // ate 16 caracteres, sem espacos no fim
    unsigned type = kTypeEffect;
    unsigned apiMajor = 0, apiMinor = 0;
    unsigned versionMajor = 0, versionMinor = 0;
    std::string description;
    std::string about;       // autor/creditos
    bool supportsTime = false;       // FF_CAP_SET_TIME
    bool supportsTopLeft = false;    // FF_CAP_TOP_LEFT_TEXTURE_ORIENTATION
    int minInputs = -1, maxInputs = -1;
    std::vector<ParamInfo> params;

    Json toJson() const;
    static std::optional<PluginInfo> fromJson(const Json& j);
};

// Resultado de varrer uma DLL (JSON que o worker devolve)
struct ScanResult
{
    bool ok = false;
    std::string error;       // quando !ok: o motivo (exibido na lista de bloqueados)
    PluginInfo info;

    Json toJson() const;
    static ScanResult fromJson(const Json& j);
};

// typeId "ffgl:<id de 4 caracteres>:<nome do arquivo>"; caracteres nao imprimiveis do id viram '?'
std::string makeTypeId(const std::string& pluginId, const std::string& fileName);
// Quebra um typeId em (id, nome do arquivo); false se nao for um typeId FFGL
bool splitTypeId(const std::string& typeId, std::string& pluginId, std::string& fileName);

} // namespace bsl::ffgl
