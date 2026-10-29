#pragma once

// Filtro de DLLs sem carregar: le o cabecalho PE do arquivo e olha a tabela de exportacao. Uma DLL so e candidata a
// plugin FFGL se for x64 e exportar "plugMain". Nunca use LoadLibrary numa DLL que nao passou por aqui (o libcef.dll
// de 300 MB, por exemplo, nao pode ser carregado a toa).

#include <filesystem>
#include <string>

namespace bsl::ffgl {

enum class PeVerdict
{
    Candidate,    // x64 e exporta plugMain
    Unreadable,   // nao abriu / truncado
    NotPe,        // nao e um executavel PE
    NotX64,       // PE de outra arquitetura (x86, ARM)
    NoPlugMain,   // DLL x64 sem a exportacao plugMain
};

PeVerdict inspectPe(const std::filesystem::path& file, std::string* detail = nullptr);
const char* peVerdictText(PeVerdict v); // PT, curto

} // namespace bsl::ffgl
