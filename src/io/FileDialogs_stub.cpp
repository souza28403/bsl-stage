#include "io/FileDialogs.h"

#include "platform/Unavailable.h"

namespace bsl::dialogs {

namespace {

std::optional<std::filesystem::path> unavailable()
{
    bslplat::reportUnavailable("Dialogos de arquivo");
    return std::nullopt;
}

}

std::optional<std::filesystem::path> openProject(void*, const std::filesystem::path&)
{
    return unavailable();
}

std::optional<std::filesystem::path> saveProject(void*, const std::filesystem::path&)
{
    return unavailable();
}

std::optional<std::filesystem::path> openMedia(void*, const std::filesystem::path&)
{
    return unavailable();
}

std::optional<std::filesystem::path> openFile(void*, const std::wstring&, const std::wstring&, const std::filesystem::path&)
{
    return unavailable();
}

std::optional<std::filesystem::path> saveFile(void*, const std::wstring&, const std::wstring&, const std::wstring&,
                                              const std::wstring&, const std::wstring&, const std::filesystem::path&)
{
    return unavailable();
}

std::optional<std::filesystem::path> pickFolder(void*, const std::filesystem::path&)
{
    return unavailable();
}

}
