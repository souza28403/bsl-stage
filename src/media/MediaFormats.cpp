#include "media/MediaFormats.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace bsl::media {

namespace {

constexpr std::array<std::string_view, 13> kVideoExt = {
    "mp4", "mov", "webm", "mkv", "avi", "m4v", "wmv", "flv", "mpg", "mpeg", "ts", "mxf", "ogv"};

constexpr std::array<std::string_view, 11> kImageExt = {
    "png", "jpg", "jpeg", "bmp", "tga", "gif", "webp", "tif", "tiff", "psd", "hdr"};

constexpr std::array<std::string_view, 1> kDocumentExt = {"pdf"};

constexpr std::array<std::string_view, 9> kOfficeExt = {"pptx", "ppt", "docx", "doc", "xlsx", "xls", "odp", "odt", "ods"};

constexpr std::array<std::string_view, 2> kWebExt = {"html", "htm"};

bool contains(std::span<const std::string_view> list, const std::string& ext)
{
    return std::find(list.begin(), list.end(), ext) != list.end();
}

} // namespace

std::span<const std::string_view> videoExtensions()
{
    return kVideoExt;
}

std::span<const std::string_view> imageExtensions()
{
    return kImageExt;
}

std::span<const std::string_view> documentExtensions()
{
    return kDocumentExt;
}

std::span<const std::string_view> officeExtensions()
{
    return kOfficeExt;
}

std::span<const std::string_view> webExtensions()
{
    return kWebExt;
}

const char* kindTag(MediaKind k)
{
    switch (k) {
    case MediaKind::Video: return "[VID]";
    case MediaKind::Image: return "[IMG]";
    case MediaKind::Document: return "[PDF]";
    case MediaKind::Office: return "[DOC]";
    case MediaKind::Web: return "[WEB]";
    case MediaKind::Unknown: break;
    }
    return "";
}

std::string lowerExtension(const std::filesystem::path& path)
{
    const auto u8 = path.extension().u8string();
    std::string ext(u8.begin(), u8.end());
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

MediaKind classify(const std::filesystem::path& path)
{
    const std::string ext = lowerExtension(path);
    if (ext.empty())
        return MediaKind::Unknown;
    if (contains(kVideoExt, ext))
        return MediaKind::Video;
    if (contains(kImageExt, ext))
        return MediaKind::Image;
    if (contains(kDocumentExt, ext))
        return MediaKind::Document;
    if (contains(kOfficeExt, ext))
        return MediaKind::Office;
    if (contains(kWebExt, ext))
        return MediaKind::Web;
    return MediaKind::Unknown;
}

} // namespace bsl::media
