#include "media/DeviceCatalog.h"

#include "app/Log.h"
#include "media/DeviceBackend.h"
#include "platform/ScanUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace bsl::media {

DeviceCatalog& DeviceCatalog::instance()
{
    static DeviceCatalog* catalog = new DeviceCatalog();
    return *catalog;
}

void DeviceCatalog::ensureListed()
{
    {
        std::lock_guard lk(m_);
        if (started_)
            return;
        started_ = true;
        listing_ = true;
    }
    std::thread([this] { readDevices(); }).detach();
}

void DeviceCatalog::refresh()
{
    {
        std::lock_guard lk(m_);
        started_ = true;
        if (listing_)
            return;
        listing_ = true;
        for (auto& [name, caps] : caps_)
            if (!caps.pending)
                caps.ready = false;
    }
    std::thread([this] { readDevices(); }).detach();
}

bool DeviceCatalog::listing() const
{
    std::lock_guard lk(m_);
    return listing_;
}

bool DeviceCatalog::listed() const
{
    std::lock_guard lk(m_);
    return listed_;
}

std::vector<std::string> DeviceCatalog::deviceNames() const
{
    std::lock_guard lk(m_);
    return names_;
}

void DeviceCatalog::readDevices()
{
    std::vector<std::string> names = devbackend::listVideoDevices();
    {
        std::lock_guard lk(m_);
        names_ = std::move(names);
        listing_ = false;
        listed_ = true;
    }
    log::info("Dispositivos de captura encontrados: " + std::to_string(deviceNames().size()));
}

void DeviceCatalog::ensureFormats(const std::string& device)
{
    if (device.empty())
        return;
    {
        std::lock_guard lk(m_);
        Caps& c = caps_[device];
        if (c.ready || c.pending)
            return;
        c.pending = true;
    }
    std::thread([this, device] { readFormats(device); }).detach();
}

bool DeviceCatalog::formatsReady(const std::string& device) const
{
    std::lock_guard lk(m_);
    const auto it = caps_.find(device);
    return it != caps_.end() && it->second.ready;
}

std::vector<CaptureFormat> DeviceCatalog::formats(const std::string& device) const
{
    std::lock_guard lk(m_);
    const auto it = caps_.find(device);
    return it == caps_.end() ? std::vector<CaptureFormat>{} : it->second.formats;
}

std::vector<CaptureFormat> DeviceCatalog::formatsBlocking(const std::string& device)
{
    if (device.empty())
        return {};
    {
        std::lock_guard lk(m_);
        const auto it = caps_.find(device);
        if (it != caps_.end() && it->second.ready)
            return it->second.formats;
        caps_[device].pending = true;
    }
    readFormats(device);
    return formats(device);
}

void DeviceCatalog::readFormats(const std::string& device)
{
    std::vector<CaptureFormat> formats = devbackend::listVideoFormats(device);
    {
        std::lock_guard lk(m_);
        Caps& c = caps_[device];
        c.formats = std::move(formats);
        c.ready = true;
        c.pending = false;
    }
}

std::vector<CaptureFormat> DeviceCatalog::parseFormats(const std::string& log)
{
    std::vector<CaptureFormat> out;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        const size_t pf = line.find("pixel_format=");
        const size_t vc = line.find("vcodec=");
        if (pf == std::string::npos && vc == std::string::npos)
            continue;
        const bool isCodec = pf == std::string::npos;
        const size_t keyPos = isCodec ? vc : pf;
        const size_t valStart = keyPos + (isCodec ? 7 : 13);
        size_t valEnd = line.find_first_of(" \t\r[", valStart);
        if (valEnd == std::string::npos)
            valEnd = line.size();
        CaptureFormat f;
        (isCodec ? f.codec : f.pixelFormat) = line.substr(valStart, valEnd - valStart);

        const size_t minPos = line.find("min s=", valEnd);
        if (minPos == std::string::npos)
            continue;
        int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
        double f1 = 0.0, f2 = 0.0;
        {
            const char* c = line.c_str() + minPos;
            if (!(bslplat::scan::text(c, "min s=") && bslplat::scan::integer(c, w1) && bslplat::scan::text(c, "x") && bslplat::scan::integer(c, h1) &&
                  bslplat::scan::text(c, " fps=") && bslplat::scan::real(c, f1) && bslplat::scan::text(c, " max s=") && bslplat::scan::integer(c, w2) &&
                  bslplat::scan::text(c, "x") && bslplat::scan::integer(c, h2) && bslplat::scan::text(c, " fps=") && bslplat::scan::real(c, f2)))
                continue;
        }
        f.width = w2;
        f.height = h2;
        f.fpsMin = std::min(f1, f2);
        f.fpsMax = std::max(f1, f2);
        if (f.width > 0 && f.height > 0 && f.fpsMax > 0.0)
            out.push_back(std::move(f));
    }
    return out;
}

std::string DeviceCatalog::formatValue(const CaptureFormat& f, double fps)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%dx%d@%d:", f.width, f.height, static_cast<int>(fps + 0.5));
    return std::string(buf) + (f.codec.empty() ? f.pixelFormat : f.codec);
}

ChosenFormat DeviceCatalog::parseValue(const std::string& value)
{
    ChosenFormat c;
    int w = 0, h = 0, fps = 0;
    std::string fmt;
    const char* cur = value.c_str();
    if (bslplat::scan::integer(cur, w) && bslplat::scan::text(cur, "x") && bslplat::scan::integer(cur, h) && bslplat::scan::text(cur, "@") &&
        bslplat::scan::integer(cur, fps) && bslplat::scan::text(cur, ":") && bslplat::scan::word(cur, fmt, 63) && w > 0 && h > 0 && fps > 0) {
        c.automatic = false;
        c.width = w;
        c.height = h;
        c.fps = fps;
        c.format = fmt;
        c.isCodec = c.format == "mjpeg" || c.format == "h264" || c.format == "hevc";
    }
    return c;
}

ChosenFormat DeviceCatalog::pickBest(const std::vector<CaptureFormat>& formats)
{
    const CaptureFormat* best = nullptr;
    auto better = [](const CaptureFormat& a, const CaptureFormat& b) {
        const bool okA = a.fpsMax >= 25.0, okB = b.fpsMax >= 25.0;
        if (okA != okB)
            return okA;
        const bool smallA = a.height <= 1080, smallB = b.height <= 1080;
        if (smallA != smallB)
            return smallA;
        const long areaA = static_cast<long>(a.width) * a.height, areaB = static_cast<long>(b.width) * b.height;
        if (areaA != areaB)
            return smallA ? areaA > areaB : areaA < areaB;
        const bool mjA = a.codec == "mjpeg", mjB = b.codec == "mjpeg";
        if (mjA != mjB)
            return mjA;
        return a.fpsMax > b.fpsMax;
    };
    for (const CaptureFormat& f : formats)
        if (!best || better(f, *best))
            best = &f;
    ChosenFormat c;
    if (!best)
        return c;
    c.automatic = false;
    c.width = best->width;
    c.height = best->height;
    double fps = std::min(best->fpsMax, 30.0);
    fps = std::max(fps, best->fpsMin);
    c.fps = static_cast<int>(fps + 0.5);
    c.format = best->codec.empty() ? best->pixelFormat : best->codec;
    c.isCodec = !best->codec.empty();
    return c;
}

}
