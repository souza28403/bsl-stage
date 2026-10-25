#pragma once

#include "core/Composition.h"
#include "output/OutputSetup.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::project {

// Formato do arquivo .bslstage: JSON legivel (UTF-8, indentado). Sem GL nem janela: testavel isolado.
inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFormatName = "bslstage";
inline constexpr const char* kExtension = ".bslstage";

struct LoadResult
{
    bool ok = false;
    std::string error;                 // preenchido quando !ok
    std::vector<std::string> warnings; // ex.: versao mais nova que a suportada
    int missingFiles = 0;              // clips marcados como ausentes
};

// Serializa o projeto. 'projectDir' (pasta do arquivo .bslstage) serve para gravar o relPath dos
// clips; vazio = so caminho absoluto.
std::string serialize(const Composition& comp, const OutputSetup& output, const std::filesystem::path& projectDir);

// Le o texto de um projeto para 'comp'/'output' (so sao tocados se o resultado for ok).
// Campos desconhecidos sao ignorados e os ausentes ficam no padrao. Ao abrir, nada comeca tocando.
// 'projectDir' resolve o relPath dos clips (primeiro o relPath, depois o path absoluto).
LoadResult parse(const std::string& text, const std::filesystem::path& projectDir, Composition& comp,
                 OutputSetup& output);

// Grava de forma atomica (arquivo temporario na mesma pasta + ReplaceFileW/MoveFileExW).
bool saveFile(const std::filesystem::path& file, const std::string& text, std::string* error);
// Le o arquivo e chama parse com a pasta dele.
LoadResult loadFile(const std::filesystem::path& file, Composition& comp, OutputSetup& output);

// Para cada clip de arquivo ausente, procura o mesmo nome de arquivo em 'folder'; devolve quantos achou.
int relinkMissingInFolder(Composition& comp, const std::filesystem::path& folder);

// Aponta o clip para um novo arquivo (tira a marca de ausente). false se o formato nao e suportado.
bool relinkClip(Clip& clip, const std::filesystem::path& file);

} // namespace bsl::project
