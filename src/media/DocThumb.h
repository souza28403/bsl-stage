#pragma once

// Miniatura da pagina 1 de um PDF ou documento do Office, gerada pelo proprio worker em modo --thumb
// (processo curto; o Office e convertido e fica em cache). Bloqueia: chamar em thread de fundo.

#include "media/Frame.h"

#include <filesystem>
#include <string>

namespace bsl::media {

// 'out' sai RGBA8 w x h (pagina encaixada, bordas transparentes). 'pages' recebe o total de paginas.
bool renderDocThumbnail(const std::filesystem::path& file, int w, int h, Frame& out, int* pages = nullptr,
                        std::string* error = nullptr);

// Pede para as geracoes em andamento terminarem logo (fechamento do app): da alguns segundos para o worker
// acabar sozinho e depois o encerra.
void cancelDocThumbnails();

} // namespace bsl::media
