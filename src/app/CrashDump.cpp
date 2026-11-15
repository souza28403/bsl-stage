#include "app/CrashDump.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <cwchar>
#endif

namespace bsl::crash {

namespace {

std::filesystem::path g_folder;
constexpr size_t kKeep = 5;

#ifdef _WIN32
wchar_t g_dir[MAX_PATH * 2] = {};

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* info)
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t file[MAX_PATH * 2 + 64] = {};
    std::swprintf(file, sizeof(file) / sizeof(file[0]), L"%ls\\falha-%04u%02u%02u-%02u%02u%02u-%lu.dmp", g_dir, t.wYear,
                  t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory
                                                     | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, info ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(h);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void prune(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> dumps;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".dmp")
            dumps.push_back(e);
    }
    if (dumps.size() < kKeep)
        return;
    std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b) {
        std::error_code e1, e2;
        return a.last_write_time(e1) > b.last_write_time(e2);
    });
    for (size_t i = kKeep - 1; i < dumps.size(); ++i)
        std::filesystem::remove(dumps[i].path(), ec);
}

}

void install(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    g_folder = dir;
    prune(dir);
#ifdef _WIN32
    const std::wstring w = dir.wstring();
    if (w.size() >= sizeof(g_dir) / sizeof(g_dir[0]))
        return;
    std::copy(w.begin(), w.end(), g_dir);
    g_dir[w.size()] = L'\0';
    SetUnhandledExceptionFilter(onUnhandled);
#endif
}

std::filesystem::path folder()
{
    return g_folder;
}

}
