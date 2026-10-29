#pragma once

// Carga em tempo de execução do runtime do NDI® (Processing.NDI.Lib.x64.dll). O projeto não distribui nem linka o
// runtime: o cliente instala o NDI Tools/Runtime oficial (gratuito). Sem ele, o resto do app funciona normalmente e a
// interface mostra "NDI Runtime não encontrado".
//
// Ordem de busca:
//   1. o caminho passado a --ndi-lib (só para testes; se existir, é o único: sem alternativa)
//   2. a pasta do executável
//   3. as variáveis NDI_RUNTIME_DIR_V6 e NDI_RUNTIME_DIR_V5 (pasta que contém a DLL)

#include "ndi_min.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::ndi {

inline constexpr const char* kRuntimeDllName = "Processing.NDI.Lib.x64.dll";
inline constexpr const char* kDownloadUrl = "https://ndi.video/tools/";
inline constexpr const char* kMissingText = "NDI Runtime não encontrado";

class Runtime
{
public:
    Runtime() = default;
    ~Runtime(); // NDIlib_destroy se inicializou; a DLL fica carregada (threads internas do NDI)
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Runtime global do app. A primeira chamada carrega (usa o caminho de setLibraryOverride, se houver).
    static Runtime& instance();
    // --ndi-lib <dll>: chamar antes do primeiro instance()
    static void setLibraryOverride(const std::filesystem::path& dll);

    // Procura e carrega. 'explicitDll' vazio = ordem normal. Pode ser chamada de novo depois de uma falha.
    // Devolve available().
    bool load(const std::filesystem::path& explicitDll = {});

    // Caminhos candidatos na ordem de busca (para diagnóstico e testes)
    static std::vector<std::filesystem::path> candidates(const std::filesystem::path& explicitDll);

    bool available() const { return api_ != nullptr; }
    const NDIlib_v5_t* api() const { return api_; }
    const std::string& version() const { return version_; }  // ex.: "NDI SDK WIN64 05:34:29 ..."
    const std::string& error() const { return error_; }      // motivo da falha (vazio se carregou)
    const std::filesystem::path& libraryPath() const { return path_; }

private:
    const NDIlib_v5_t* api_ = nullptr;
    std::string version_;
    std::string error_;
    std::filesystem::path path_;
    bool initialized_ = false;
};

} // namespace bsl::ndi
