#include "worker/WorkerUtil.h"

#include "media/WorkerProtocol.h"
#include "platform/Env.h"
#include "platform/Paths.h"
#include "platform/TimeUtil.h"
#include "platform/Utf.h"

#ifdef _WIN32
#include <windows.h>

#include <shlobj.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace bsl::worker {

std::string toUtf8(const std::wstring& w)
{
    return bslplat::wideToUtf8(w);
}

std::wstring fromUtf8(const std::string& s)
{
    return bslplat::utf8ToWide(s);
}

std::string pathToUtf8(const std::filesystem::path& p)
{
#ifdef _WIN32
    return toUtf8(p.wstring());
#else
    return p.string();
#endif
}

std::filesystem::path pathFromUtf8(const std::string& s)
{
#ifdef _WIN32
    return std::filesystem::path(fromUtf8(s));
#else
    return std::filesystem::path(s);
#endif
}

std::string envUtf8(const char* name)
{
#ifdef _WIN32
    return toUtf8(bslplat::envWide(bslplat::utf8ToWide(name).c_str()));
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

std::string lowerExt(const std::filesystem::path& p)
{
    std::string ext = pathToUtf8(p.extension());
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::filesystem::path localDataDir()
{
    std::filesystem::path dir;
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw)
        dir = std::filesystem::path(raw) / L"BSL Stage";
    CoTaskMemFree(raw);
#else
    dir = bslplat::dataDir();
#endif
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
    std::ofstream f(dir / "worker.log", std::ios::app);
    if (f) {
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
        bslplat::localTime(std::chrono::system_clock::to_time_t(now), tm);
        char stamp[40];
        std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d [%lu] ", tm.tm_hour, tm.tm_min, tm.tm_sec,
                      static_cast<int>(ms), bslplat::currentProcessId());
        f << stamp << line << "\n";
    }
}

std::vector<unsigned long> processIds(const wchar_t* exeName)
{
    std::vector<unsigned long> ids;
#ifdef _WIN32
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
#else
    const std::string wanted = bslplat::wideToUtf8(exeName);
    DIR* dir = opendir("/proc");
    if (!dir)
        return ids;
    while (const dirent* e = readdir(dir)) {
        const std::string name = e->d_name;
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            continue;
        std::error_code ec;
        const std::filesystem::path target = std::filesystem::read_symlink("/proc/" + name + "/exe", ec);
        if (!ec && target.filename().string() == wanted)
            ids.push_back(std::strtoul(name.c_str(), nullptr, 10));
    }
    closedir(dir);
#endif
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

ParentWatch::ParentWatch(unsigned long pid) : pid_(pid)
{
#ifdef _WIN32
    handle_ = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
#endif
}

ParentWatch::~ParentWatch()
{
#ifdef _WIN32
    if (handle_)
        CloseHandle(static_cast<HANDLE>(handle_));
#endif
}

bool ParentWatch::alive() const
{
#ifdef _WIN32
    return !handle_ || WaitForSingleObject(static_cast<HANDLE>(handle_), 0) != WAIT_OBJECT_0;
#else
    return pid_ == 0 || getppid() == static_cast<pid_t>(pid_);
#endif
}

}
