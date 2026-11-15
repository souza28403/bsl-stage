#include "app/Log.h"

#include "app/Paths.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace bsl::log {

namespace {

std::mutex g_mutex;
std::string g_lastWarning;
std::chrono::steady_clock::time_point g_lastWarningTime;
bool g_fileStarted = false;

void write(const char* level, const std::string& message)
{
    const std::string line = std::string("[") + level + "] " + message + "\n";
    std::fputs(line.c_str(), stderr);
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#endif

    std::ofstream file(paths::appDataDir() / L"bslstage.log",
                       g_fileStarted ? std::ios::app : std::ios::trunc);
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
