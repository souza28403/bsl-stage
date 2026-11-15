#include "update/SystemProbe.h"

#if BSL_WITH_UPDATER
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

#include <cstdio>

#if BSL_WITH_UPDATER

namespace bsl::update {

namespace {

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string envVar(const wchar_t* name)
{
    wchar_t buf[1024];
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    return n > 0 && n < std::size(buf) ? narrow(std::wstring(buf, n)) : std::string();
}

}

std::string windowsVersionText()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))))
            fn(&vi);
    }
    if (vi.dwMajorVersion == 0)
        return "Windows (versão desconhecida)";
    const char* name = vi.dwMajorVersion >= 10 && vi.dwBuildNumber >= 22000 ? "Windows 11"
                       : vi.dwMajorVersion >= 10                           ? "Windows 10"
                                                                           : "Windows";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s (build %lu)", name, static_cast<unsigned long>(vi.dwBuildNumber));
    return buf;
}

std::string totalRamText()
{
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return "desconhecida";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(ms.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0));
    for (char* p = buf; *p; ++p)
        if (*p == '.')
            *p = ',';
    return buf;
}

std::string userProfileDir()
{
    return envVar(L"USERPROFILE");
}

std::string userNameText()
{
    return envVar(L"USERNAME");
}

std::string computerNameText()
{
    return envVar(L"COMPUTERNAME");
}

bool openGithubUrl(const std::string& url)
{
    constexpr const char* prefix = "https://github.com/";
    if (url.rfind(prefix, 0) != 0 || url.size() > 8000)
        return false;
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), w.data(), n);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}

#else

namespace bsl::update {

namespace {

std::string envVar(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

}

std::string windowsVersionText()
{
    return "Sistema não Windows";
}

std::string totalRamText()
{
    return "desconhecida";
}

std::string userProfileDir()
{
    return envVar("HOME");
}

std::string userNameText()
{
    return envVar("USER");
}

std::string computerNameText()
{
    return envVar("HOSTNAME");
}

bool openGithubUrl(const std::string&)
{
    return false;
}

}

#endif
