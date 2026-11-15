#include "io/FileDialogs.h"

#include "app/FrameWatch.h"
#include "media/MediaFormats.h"
#include "platform/Utf.h"

#include <nfd.h>

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

namespace bsl::dialogs {

namespace {

struct Filter
{
    std::string name;
    std::string spec;
};

struct NfdScope
{
    bool ok = NFD_Init() == NFD_OKAY;
    ~NfdScope()
    {
        if (ok)
            NFD_Quit();
    }
};

std::string convertSpec(const std::wstring& spec)
{
    std::string out;
    size_t pos = 0;
    while (pos <= spec.size()) {
        const size_t semi = spec.find(L';', pos);
        std::wstring one = spec.substr(pos, semi == std::wstring::npos ? std::wstring::npos : semi - pos);
        pos = semi == std::wstring::npos ? spec.size() + 1 : semi + 1;
        if (one.rfind(L"*.", 0) == 0)
            one.erase(0, 2);
        if (one.empty() || one == L"*")
            continue;
        if (!out.empty())
            out += ',';
        out += bslplat::wideToUtf8(one);
    }
    return out;
}

std::vector<Filter> makeFilters(const std::vector<std::pair<std::wstring, std::wstring>>& in)
{
    std::vector<Filter> out;
    for (const auto& [name, spec] : in) {
        const std::string converted = convertSpec(spec);
        if (!converted.empty())
            out.push_back({bslplat::wideToUtf8(name), converted});
    }
    return out;
}

std::filesystem::path takePath(nfdu8char_t* raw)
{
    std::filesystem::path result{std::u8string(reinterpret_cast<const char8_t*>(raw))};
    NFD_FreePathU8(raw);
    return result;
}

std::string pathUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::optional<std::filesystem::path> show(bool save, const std::vector<Filter>& filters, const std::wstring& defaultExt,
                                          const std::filesystem::path& initialDir, const std::wstring& fileName)
{
    const FrameWatch::Blocking blocking;
    const NfdScope nfd;
    if (!nfd.ok)
        return std::nullopt;

    std::vector<nfdu8filteritem_t> items;
    items.reserve(filters.size());
    for (const Filter& f : filters)
        items.push_back({f.name.c_str(), f.spec.c_str()});

    const std::string dir = initialDir.empty() ? std::string() : pathUtf8(initialDir);
    const nfdu8char_t* dirPtr = dir.empty() ? nullptr : dir.c_str();
    nfdu8char_t* raw = nullptr;
    nfdresult_t r;
    if (save) {
        const std::string name = bslplat::wideToUtf8(fileName);
        r = NFD_SaveDialogU8(&raw, items.data(), static_cast<nfdfiltersize_t>(items.size()), dirPtr,
                             name.empty() ? nullptr : name.c_str());
    } else {
        r = NFD_OpenDialogU8(&raw, items.data(), static_cast<nfdfiltersize_t>(items.size()), dirPtr);
    }
    if (r != NFD_OKAY || !raw)
        return std::nullopt;

    std::filesystem::path result = takePath(raw);
    if (save && !defaultExt.empty() && !result.has_extension())
        result += L"." + defaultExt;
    return result;
}

}

std::optional<std::filesystem::path> openProject(void*, const std::filesystem::path& initialDir)
{
    return show(false, makeFilters({{L"Projeto BSL Stage (*.bslstage)", L"*.bslstage"}}), L"", initialDir, {});
}

std::optional<std::filesystem::path> saveProject(void*, const std::filesystem::path& currentFile)
{
    const std::wstring name = currentFile.empty() ? L"Sem t\u00edtulo.bslstage" : currentFile.filename().wstring();
    return show(true, makeFilters({{L"Projeto BSL Stage (*.bslstage)", L"*.bslstage"}}), L"bslstage",
                currentFile.parent_path(), name);
}

std::optional<std::filesystem::path> openMedia(void*, const std::filesystem::path& initialDir)
{
    std::wstring spec;
    auto add = [&](std::span<const std::string_view> exts) {
        for (const std::string_view e : exts) {
            if (!spec.empty())
                spec += L';';
            spec += L"*.";
            spec.append(e.begin(), e.end());
        }
    };
    add(media::videoExtensions());
    add(media::imageExtensions());
    return show(false, makeFilters({{L"V\u00eddeos e imagens", spec}}), L"", initialDir, {});
}

std::optional<std::filesystem::path> openFile(void*, const std::wstring& filterName, const std::wstring& spec,
                                              const std::filesystem::path& initialDir)
{
    return show(false, makeFilters({{filterName, spec}}), L"", initialDir, {});
}

std::optional<std::filesystem::path> saveFile(void*, const std::wstring&, const std::wstring& filterName,
                                              const std::wstring& spec, const std::wstring& defaultName,
                                              const std::wstring& defaultExt, const std::filesystem::path& initialDir)
{
    return show(true, makeFilters({{filterName, spec}}), defaultExt, initialDir, defaultName);
}

std::optional<std::filesystem::path> pickFolder(void*, const std::filesystem::path& initialDir)
{
    const FrameWatch::Blocking blocking;
    const NfdScope nfd;
    if (!nfd.ok)
        return std::nullopt;
    const std::string dir = initialDir.empty() ? std::string() : pathUtf8(initialDir);
    nfdu8char_t* raw = nullptr;
    if (NFD_PickFolderU8(&raw, dir.empty() ? nullptr : dir.c_str()) != NFD_OKAY || !raw)
        return std::nullopt;
    return takePath(raw);
}

}
