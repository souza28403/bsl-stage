#include "platform/Paths.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace bslplat {

std::filesystem::path xdgDir(const std::string& envValue, const std::filesystem::path& home, const char* fallbackRelative)
{
    if (!envValue.empty() && envValue.front() == '/')
        return std::filesystem::path(envValue);
    return home / fallbackRelative;
}

#ifdef _WIN32

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

}

std::filesystem::path homeDir()
{
    return knownFolder(FOLDERID_Profile);
}

std::filesystem::path configDir()
{
    return knownFolder(FOLDERID_RoamingAppData) / L"BSL Stage";
}

std::filesystem::path dataDir()
{
    return knownFolder(FOLDERID_LocalAppData) / L"BSL Stage";
}

std::filesystem::path cacheDir()
{
    return knownFolder(FOLDERID_LocalAppData) / L"BSL Stage" / L"Cache";
}

std::filesystem::path desktopDir()
{
    return knownFolder(FOLDERID_Desktop);
}

std::filesystem::path documentsDir()
{
    return knownFolder(FOLDERID_Documents) / L"BSL Stage";
}

#else

namespace {

std::string envOrEmpty(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

}

std::filesystem::path homeDir()
{
    const std::string h = envOrEmpty("HOME");
    return h.empty() ? std::filesystem::path("/tmp") : std::filesystem::path(h);
}

#ifdef __APPLE__

std::filesystem::path configDir()
{
    return homeDir() / "Library" / "Application Support" / "BSL Stage";
}

std::filesystem::path dataDir()
{
    return configDir();
}

std::filesystem::path cacheDir()
{
    return homeDir() / "Library" / "Caches" / "BSL Stage";
}

#else

std::filesystem::path configDir()
{
    return xdgDir(envOrEmpty("XDG_CONFIG_HOME"), homeDir(), ".config") / "bsl-stage";
}

std::filesystem::path dataDir()
{
    return xdgDir(envOrEmpty("XDG_DATA_HOME"), homeDir(), ".local/share") / "bsl-stage";
}

std::filesystem::path cacheDir()
{
    return xdgDir(envOrEmpty("XDG_CACHE_HOME"), homeDir(), ".cache") / "bsl-stage";
}

#endif

namespace {

std::filesystem::path userDir(const char* key, const char* fallback)
{
    const std::filesystem::path home = homeDir();
#ifndef __APPLE__
    const std::filesystem::path cfg = xdgDir(envOrEmpty("XDG_CONFIG_HOME"), home, ".config") / "user-dirs.dirs";
    std::ifstream in(cfg);
    const std::string prefix = std::string(key) + "=\"";
    for (std::string line; std::getline(in, line);) {
        if (line.rfind(prefix, 0) != 0)
            continue;
        std::string value = line.substr(prefix.size());
        const size_t end = value.find('"');
        if (end == std::string::npos)
            break;
        value.resize(end);
        std::filesystem::path p;
        if (value.rfind("$HOME", 0) == 0)
            p = home / std::filesystem::path(value.substr(value.size() > 5 && value[5] == '/' ? 6 : 5));
        else if (!value.empty() && value.front() == '/')
            p = value;
        std::error_code ec;
        if (!p.empty() && std::filesystem::is_directory(p, ec))
            return p;
        break;
    }
#else
    (void)key;
#endif
    std::error_code ec;
    const std::filesystem::path p = home / fallback;
    return std::filesystem::is_directory(p, ec) ? p : home;
}

}

std::filesystem::path desktopDir()
{
    return userDir("XDG_DESKTOP_DIR", "Desktop");
}

std::filesystem::path documentsDir()
{
    return userDir("XDG_DOCUMENTS_DIR", "Documents") / "BSL Stage";
}

#endif

}
