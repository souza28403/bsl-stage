#include "app/Paths.h"

#include "platform/ExePath.h"
#include "platform/OpenUrl.h"
#include "platform/Paths.h"

namespace bsl::paths {

namespace {

std::filesystem::path g_appDataOverride;

}

void setAppDataOverride(const std::filesystem::path& dir)
{
    g_appDataOverride = dir;
}

std::filesystem::path appDataDir()
{
    std::filesystem::path dir = g_appDataOverride.empty() ? bslplat::configDir() : g_appDataOverride;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path desktopDir()
{
    return bslplat::desktopDir();
}

std::filesystem::path documentsDir()
{
    return bslplat::documentsDir();
}

std::filesystem::path isfDir()
{
    if (!g_appDataOverride.empty())
        return g_appDataOverride / L"ISF";
    return documentsDir() / L"ISF";
}

std::filesystem::path localDataDir()
{
    std::filesystem::path dir = g_appDataOverride.empty() ? bslplat::dataDir() : g_appDataOverride;
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
        std::filesystem::path dir = bslplat::exeDir();
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
    bslplat::revealFile(file);
}

std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

}
