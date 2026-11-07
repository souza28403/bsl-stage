#include "io/FileDialogs.h"

#include "app/FrameWatch.h"
#include "io/ProjectFile.h"
#include "media/MediaFormats.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <vector>

namespace bsl::dialogs {

namespace {

using Microsoft::WRL::ComPtr;

struct Filter
{
    std::wstring name;
    std::wstring spec;
};

struct ComScope
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool owned = SUCCEEDED(hr);
    ~ComScope()
    {
        if (owned)
            CoUninitialize();
    }
};

std::optional<std::filesystem::path> show(HWND owner, bool save, const wchar_t* title,
                                          const std::vector<Filter>& filters, const wchar_t* defaultExt,
                                          const std::filesystem::path& initialDir, const std::wstring& fileName)
{
    const FrameWatch::Blocking blocking;
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE)
        return std::nullopt;

    ComPtr<IFileDialog> dlg;
    const HRESULT created = save ? CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(&dlg))
                                 : CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(&dlg));
    if (FAILED(created))
        return std::nullopt;

    std::vector<COMDLG_FILTERSPEC> specs;
    specs.reserve(filters.size());
    for (const Filter& f : filters)
        specs.push_back({f.name.c_str(), f.spec.c_str()});
    dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    dlg->SetFileTypeIndex(1);
    if (defaultExt)
        dlg->SetDefaultExtension(defaultExt);
    dlg->SetTitle(title);

    DWORD options = 0;
    dlg->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    options |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
    dlg->SetOptions(options);

    if (!fileName.empty())
        dlg->SetFileName(fileName.c_str());
    if (!initialDir.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            dlg->SetFolder(folder.Get());
    }

    if (FAILED(dlg->Show(owner)))
        return std::nullopt;

    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item)))
        return std::nullopt;
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || !raw)
        return std::nullopt;
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::vector<Filter> projectFilters()
{
    return {{L"Projeto BSL Stage (*.bslstage)", L"*.bslstage"}, {L"Todos os arquivos (*.*)", L"*.*"}};
}

}

std::optional<std::filesystem::path> openProject(HWND owner, const std::filesystem::path& initialDir)
{
    return show(owner, false, L"Abrir projeto", projectFilters(), L"bslstage", initialDir, {});
}

std::optional<std::filesystem::path> saveProject(HWND owner, const std::filesystem::path& currentFile)
{
    const std::wstring name = currentFile.empty() ? L"Sem t\u00edtulo.bslstage" : currentFile.filename().wstring();
    return show(owner, true, L"Salvar projeto", projectFilters(), L"bslstage", currentFile.parent_path(), name);
}

std::optional<std::filesystem::path> openMedia(HWND owner, const std::filesystem::path& initialDir)
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
    return show(owner, false, L"Localizar arquivo",
                {{L"V\u00eddeos e imagens", spec}, {L"Todos os arquivos (*.*)", L"*.*"}}, nullptr, initialDir, {});
}

std::optional<std::filesystem::path> openFile(HWND owner, const std::wstring& filterName, const std::wstring& spec,
                                              const std::filesystem::path& initialDir)
{
    std::vector<Filter> filters;
    if (!spec.empty())
        filters.push_back({filterName, spec});
    filters.push_back({L"Todos os arquivos (*.*)", L"*.*"});
    return show(owner, false, L"Escolher arquivo", filters, nullptr, initialDir, {});
}

std::optional<std::filesystem::path> saveFile(HWND owner, const std::wstring& title, const std::wstring& filterName,
                                              const std::wstring& spec, const std::wstring& defaultName,
                                              const std::wstring& defaultExt, const std::filesystem::path& initialDir)
{
    std::vector<Filter> filters;
    if (!spec.empty())
        filters.push_back({filterName, spec});
    filters.push_back({L"Todos os arquivos (*.*)", L"*.*"});
    return show(owner, true, title.c_str(), filters, defaultExt.empty() ? nullptr : defaultExt.c_str(), initialDir,
                defaultName);
}

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initialDir)
{
    ComScope com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE)
        return std::nullopt;
    ComPtr<IFileDialog> dlg;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return std::nullopt;
    DWORD options = 0;
    dlg->GetOptions(&options);
    dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    dlg->SetTitle(L"Escolher pasta");
    if (!initialDir.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            dlg->SetFolder(folder.Get());
    }
    if (FAILED(dlg->Show(owner)))
        return std::nullopt;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item)))
        return std::nullopt;
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || !raw)
        return std::nullopt;
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

}
