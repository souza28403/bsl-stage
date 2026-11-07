#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace bsl::media {

enum class MediaKind { Unknown, Video, Image, Document, Office, Web };

std::span<const std::string_view> videoExtensions();
std::span<const std::string_view> imageExtensions();
std::span<const std::string_view> documentExtensions();
std::span<const std::string_view> officeExtensions();
std::span<const std::string_view> webExtensions();

inline bool isRemoteKind(MediaKind k)
{
    return k == MediaKind::Document || k == MediaKind::Office || k == MediaKind::Web;
}
inline bool isPagedKind(MediaKind k)
{
    return k == MediaKind::Document || k == MediaKind::Office;
}
const char* kindTag(MediaKind k);

MediaKind classify(const std::filesystem::path& path);

std::string lowerExtension(const std::filesystem::path& path);

}
