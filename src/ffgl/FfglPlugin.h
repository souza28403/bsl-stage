#pragma once

// Uma DLL de plugin FFGL carregada no processo: LoadLibraryEx + plugMain, com toda chamada protegida por SEH.
// Nao inclui o cabecalho do FFGL (so os .cpp incluem): os codigos de funcao ficam em ffgl::fn.

#include "ffgl/FfglInfo.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace bsl::ffgl {

using Word = std::uintptr_t; // a union FFMixed (valor de 32 bits ou ponteiro)

struct CallResult
{
    bool ok = false;               // false = o plugin lancou excecao estruturada (access violation etc.)
    Word value = 0;
    unsigned long exception = 0;   // GetExceptionCode() quando !ok
    std::uint32_t u32() const { return static_cast<std::uint32_t>(value); }
    float f32() const;
};

std::string exceptionText(unsigned long code); // "0xC0000005 (violação de acesso)"
Word floatWord(float v);                        // bits de um float na parte baixa do FFMixed

class Plugin
{
public:
    // LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH): as DLLs ao lado do plugin sao achadas. nullptr + erro se a DLL nao
    // carrega ou nao exporta plugMain. So chame depois do filtro de PE (ffgl/FfglPe.h).
    // Mensagens que o plugin manda pelo SetLogCallback do FFGL (de qualquer thread). Vale para os plugins abertos depois.
    using LogSink = void (*)(const std::string& message);
    static void setLogSink(LogSink sink);

    static std::shared_ptr<Plugin> open(const std::filesystem::path& file, std::string* error);
    ~Plugin(); // nao descarrega a DLL (plugins com threads/CEF podem travar no FreeLibrary): so o fim do processo

    const std::filesystem::path& path() const { return path_; }

    CallResult call(std::uint32_t code, Word in = 0, void* instance = nullptr);
    CallResult callPtr(std::uint32_t code, const void* in, void* instance = nullptr)
    {
        return call(code, reinterpret_cast<Word>(in), instance);
    }

    // FF_INITIALISE_V2 uma vez por DLL; false (com motivo) se o plugin recusa ou lanca excecao
    bool initialise(std::string* error);
    bool initialised() const { return initialised_; }
    void deinitialise(); // FF_DEINITIALISE (no fim do processo ou da varredura)
    void unload();       // FreeLibrary (so a varredura em processo proprio usa; o app nunca descarrega)

    // Leituras seguras de memoria devolvida pelo plugin (ponteiro invalido ou FF_FAIL resultam em falso/vazio)
    bool readMemory(Word ptr, void* dst, std::size_t n);
    std::string readString(Word ptr, std::size_t maxLen = 512);

private:
    Plugin() = default;

    std::filesystem::path path_;
    void* module_ = nullptr;   // HMODULE
    void* plugMain_ = nullptr;
    bool initialised_ = false;
};

// Codigos de funcao do FFGL usados pelo host (iguais aos de FFGL.h; os .cpp conferem com static_assert)
namespace fn {
inline constexpr std::uint32_t GetInfo = 0;
inline constexpr std::uint32_t InitialiseV2 = 34;
inline constexpr std::uint32_t Deinitialise = 2;
inline constexpr std::uint32_t GetNumParameters = 4;
inline constexpr std::uint32_t GetParameterName = 5;
inline constexpr std::uint32_t GetParameterDefault = 6;
inline constexpr std::uint32_t GetParameterDisplay = 7;
inline constexpr std::uint32_t SetParameter = 8;
inline constexpr std::uint32_t GetParameter = 9;
inline constexpr std::uint32_t GetPluginCaps = 10;
inline constexpr std::uint32_t EnablePluginCap = 49;
inline constexpr std::uint32_t GetExtendedInfo = 13;
inline constexpr std::uint32_t GetParameterType = 15;
inline constexpr std::uint32_t ProcessOpenGL = 17;
inline constexpr std::uint32_t InstantiateGL = 18;
inline constexpr std::uint32_t DeinstantiateGL = 19;
inline constexpr std::uint32_t SetTime = 20;
inline constexpr std::uint32_t Resize = 23;
inline constexpr std::uint32_t GetNumParameterElements = 31;
inline constexpr std::uint32_t GetParameterElementName = 35;
inline constexpr std::uint32_t GetParameterElementValue = 36;
inline constexpr std::uint32_t GetParameterUsage = 32;
inline constexpr std::uint32_t SetBeatInfo = 38;
inline constexpr std::uint32_t SetHostInfo = 39;
inline constexpr std::uint32_t GetRange = 41;
inline constexpr std::uint32_t GetParamGroup = 50;
inline constexpr std::uint32_t GetNumFileExtensions = 43;
inline constexpr std::uint32_t GetFileExtension = 44;
inline constexpr std::uint32_t GetParamVisibility = 45;
inline constexpr std::uint32_t GetParamEvents = 46;
} // namespace fn

// Varre uma DLL: carrega, le informacoes e parametros, descarrega (o que o worker faz em --scan-ffgl).
// Nunca lanca; erro e excecao do plugin viram ScanResult::error.
// Se 'loaded' nao for nulo, devolve ai o plugin (ainda inicializado) para o chamador gravar o resultado antes de
// chamar deinitialise()/unload().
ScanResult scanPluginFile(const std::filesystem::path& file, std::shared_ptr<Plugin>* loaded = nullptr);

} // namespace bsl::ffgl
