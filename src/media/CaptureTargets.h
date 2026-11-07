#pragma once

#include "media/SourceParams.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::media {

struct MonitorTarget
{
    int index = 1;
    std::uintptr_t handle = 0;
    int x = 0, y = 0, w = 0, h = 0;
    bool primary = false;
};

struct WindowTarget
{
    std::uintptr_t hwnd = 0;
    std::string title;
    std::string exe;
    unsigned long pid = 0;
    int w = 0, h = 0;
};

std::vector<MonitorTarget> listMonitors();
std::vector<WindowTarget> listWindows();

std::uintptr_t findWindow(const std::string& exe, const std::string& title);
double titleSimilarity(const std::string& a, const std::string& b);

Json monitorTargetJson(int index);
Json windowTargetJson(const WindowTarget& w);
std::string targetLabel(const Json& target);

struct ThumbRgba
{
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};
bool grabMonitorThumb(const MonitorTarget& m, int w, int h, ThumbRgba& out);
bool grabWindowThumb(const WindowTarget& win, int w, int h, ThumbRgba& out);

}
