#pragma once

// Conversao de documentos do Office (pptx, docx, xlsx, ...) para PDF, no worker.
// Ordem: Microsoft Office via COM (se instalado) -> LibreOffice headless (se encontrado) -> erro claro.
// Cache em %LOCALAPPDATA%\BSL Stage\cache\docs\ (hash de caminho + tamanho + mtime).

#include <filesystem>
#include <string>

namespace bsl::worker {

struct ConvertResult
{
    bool ok = false;
    std::filesystem::path pdf;
    std::string error;       // texto para a celula (PT)
    bool fromCache = false;
    std::string via;         // "Microsoft Office", "LibreOffice" ou "cache"
};

// Onde fica (ou ficaria) o PDF convertido deste arquivo; muda quando o arquivo muda
std::filesystem::path docCachePath(const std::filesystem::path& source);

// Bloqueia ate converter (chame numa thread). Usa COM: inicializa e desinicializa a thread chamadora.
ConvertResult convertToPdf(const std::filesystem::path& source);

} // namespace bsl::worker
