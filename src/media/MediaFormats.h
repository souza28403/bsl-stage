#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace bsl::media {

// Document (pdf), Office (pptx, docx, xlsx...) e Web (html solto) sao renderizados pelo processo auxiliar
// (bslstage_worker.exe); Office e convertido para PDF la dentro.
enum class MediaKind { Unknown, Video, Image, Document, Office, Web };

// Extensoes (minusculas, sem ponto) suportadas. Unica fonte de verdade.
std::span<const std::string_view> videoExtensions();
std::span<const std::string_view> imageExtensions();
std::span<const std::string_view> documentExtensions();
std::span<const std::string_view> officeExtensions();
std::span<const std::string_view> webExtensions();

// PDF, Office ou Web: fonte remota (processo auxiliar), exclusiva como as fontes ao vivo
inline bool isRemoteKind(MediaKind k)
{
    return k == MediaKind::Document || k == MediaKind::Office || k == MediaKind::Web;
}
// PDF e Office: documento paginado
inline bool isPagedKind(MediaKind k)
{
    return k == MediaKind::Document || k == MediaKind::Office;
}
// Etiqueta curta do Navegador: [VID], [IMG], [PDF], [DOC], [WEB]
const char* kindTag(MediaKind k);

// Classifica pela extensao (case-insensitive).
MediaKind classify(const std::filesystem::path& path);

// Extensao minuscula sem ponto.
std::string lowerExtension(const std::filesystem::path& path);

} // namespace bsl::media
