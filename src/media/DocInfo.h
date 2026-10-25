#pragma once

// Registro (por caminho) do numero de paginas de documentos ja vistos: preenchido pelo worker em miniatura e
// pela fonte aberta; usado pelo ClipPanel e por "Expandir paginas na linha" sem abrir o documento.

#include <filesystem>

namespace bsl::media {

void setDocPageCount(const std::filesystem::path& file, int pages);
int docPageCount(const std::filesystem::path& file); // 0 = desconhecido

} // namespace bsl::media
