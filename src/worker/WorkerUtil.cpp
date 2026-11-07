#include "worker/WorkerUtil.h"

#include "media/WorkerProtocol.h"

#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace bsl::worker {

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string lowerExt(const std::filesystem::path& p)
{
    std::string ext = toUtf8(p.extension().wstring());
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::filesystem::path localDataDir()
{
    PWSTR raw = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw)
        dir = std::filesystem::path(raw) / L"BSL Stage";
    CoTaskMemFree(raw);
    std::error_code ec;
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);
    return dir;
}

void fileLog(const std::string& line)
{
    static std::mutex m;
    std::lock_guard lk(m);
    const std::filesystem::path dir = localDataDir();
    if (dir.empty())
        return;
    std::ofstream f(dir / L"worker.log", std::ios::app);
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char stamp[40];
        std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d [%lu] ", st.wHour, st.wMinute, st.wSecond,
                      st.wMilliseconds, GetCurrentProcessId());
        f << stamp << line << "\n";
    }
}

std::vector<DWORD> processIds(const wchar_t* exeName)
{
    std::vector<DWORD> ids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return ids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0)
                ids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return ids;
}

std::string fnv1aHex(const std::string& data)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool isOfficeExtension(const std::string& ext)
{
    for (const char* e : wp::kOfficeExtensions)
        if (ext == e)
            return true;
    return false;
}

}
