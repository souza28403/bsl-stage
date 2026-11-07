#include "app/Paths.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace bsl::paths {

namespace {

std::filesystem::path knownFolder(const KNOWNFOLDERID& id)
{
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw)
        result = raw;
    CoTaskMemFree(raw);
    return result;
}

std::filesystem::path g_appDataOverride;

}

void setAppDataOverride(const std::filesystem::path& dir)
{
    g_appDataOverride = dir;
}

std::filesystem::path appDataDir()
{
    std::filesystem::path dir = g_appDataOverride.empty() ? knownFolder(FOLDERID_RoamingAppData) / L"BSL Stage"
                                                          : g_appDataOverride;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path desktopDir()
{
    return knownFolder(FOLDERID_Desktop);
}

std::filesystem::path documentsDir()
{
    return knownFolder(FOLDERID_Documents) / L"BSL Stage";
}

std::filesystem::path isfDir()
{
    if (!g_appDataOverride.empty())
        return g_appDataOverride / L"ISF";
    return documentsDir() / L"ISF";
}

std::filesystem::path localDataDir()
{
    std::filesystem::path dir = g_appDataOverride.empty() ? knownFolder(FOLDERID_LocalAppData) / L"BSL Stage"
                                                          : g_appDataOverride;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path pluginsDir()
{
    if (!g_appDataOverride.empty())
        return g_appDataOverride / L"Plugins";
    return documentsDir() / L"Plugins";
}

std::filesystem::path assetsDir()
{
    static const std::filesystem::path cached = [] {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path dir = std::filesystem::path(std::wstring(buf, n)).parent_path();
        std::error_code ec;
        for (int i = 0; i < 4 && !dir.empty(); ++i) {
            if (std::filesystem::is_directory(dir / L"assets" / L"fonts", ec))
                return dir / L"assets";
            dir = dir.parent_path();
        }
        return std::filesystem::path();
    }();
    return cached;
}

void revealInExplorer(const std::filesystem::path& file)
{
    std::filesystem::path native = file;
    native.make_preferred();
    const std::wstring args = L"/select,\"" + native.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

}
