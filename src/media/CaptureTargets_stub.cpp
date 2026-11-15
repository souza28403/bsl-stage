#include "media/CaptureTargets.h"

namespace bsl::media {

std::vector<MonitorTarget> listMonitors()
{
    return {};
}

std::vector<WindowTarget> listWindows()
{
    return {};
}

bool grabMonitorThumb(const MonitorTarget&, int, int, ThumbRgba&)
{
    return false;
}

bool grabWindowThumb(const WindowTarget&, int, int, ThumbRgba&)
{
    return false;
}

}
