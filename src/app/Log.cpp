#include "app/Log.h"

#include "app/Paths.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "platform/TimeUtil.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace bsl::log {

namespace {

std::mutex g_mutex;
std::string g_lastWarning;
std::chrono::steady_clock::time_point g_lastWarningTime;
bool g_fileStarted = false;

std::string clock()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    bslplat::localTime(t, tm);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

void write(const char* level, const std::string& message)
{
    const std::string line = std::string("[") + level + "] " + clock() + " " + message + "\n";
    std::fputs(line.c_str(), stderr);
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#endif

    const std::filesystem::path path = paths::appDataDir() / L"bslstage.log";
    if (!g_fileStarted) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            std::filesystem::rename(path, paths::appDataDir() / L"bslstage-anterior.log", ec);
    }
    std::ofstream file(path, std::ios::app);
    g_fileStarted = true;
    if (file)
        file << line;
}

}

void info(const std::string& message)
{
    std::lock_guard lk(g_mutex);
    write("info", message);
}

void warn(const std::string& message)
{
    std::lock_guard lk(g_mutex);
    write("aviso", message);
    g_lastWarning = message;
    g_lastWarningTime = std::chrono::steady_clock::now();
}

std::string recentWarning(double seconds)
{
    std::lock_guard lk(g_mutex);
    if (g_lastWarning.empty())
        return {};
    const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_lastWarningTime).count();
    return age <= seconds ? g_lastWarning : std::string();
}

}
