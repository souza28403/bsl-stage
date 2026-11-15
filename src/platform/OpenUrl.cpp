#include "platform/OpenUrl.h"

#include "platform/Utf.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace bslplat {

#ifdef _WIN32

bool openUrl(const std::string& urlUtf8)
{
    const std::wstring w = utf8ToWide(urlUtf8);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool openFolder(const std::filesystem::path& dir)
{
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool revealFile(const std::filesystem::path& file)
{
    std::filesystem::path native = file;
    native.make_preferred();
    const std::wstring args = L"/select,\"" + native.wstring() + L"\"";
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL))
        > 32;
}

#else

namespace {

#ifdef __APPLE__
constexpr const char* kOpener = "open";
#else
constexpr const char* kOpener = "xdg-open";
#endif

bool spawnDetached(const std::string& arg)
{
    const pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        const pid_t inner = fork();
        if (inner != 0)
            _exit(inner < 0 ? 1 : 0);
        execlp(kOpener, kOpener, arg.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}

bool openUrl(const std::string& urlUtf8)
{
    return !urlUtf8.empty() && spawnDetached(urlUtf8);
}

bool openFolder(const std::filesystem::path& dir)
{
    return spawnDetached(dir.string());
}

bool revealFile(const std::filesystem::path& file)
{
    return spawnDetached(file.parent_path().string());
}

#endif

}
