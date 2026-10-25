#include "media/DeviceCatalog.h"

#include "app/Log.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sstream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/log.h>
}

namespace bsl::media {

namespace {

std::mutex g_sinkMutex;
std::string* g_sink = nullptr;

void captureLog(void* ptr, int level, const char* fmt, va_list vl)
{
    if (level > AV_LOG_INFO)
        return;
    char line[2048];
    std::lock_guard lk(g_sinkMutex);
    static int printPrefix = 1; // continua a linha entre chamadas (o dshow monta uma linha em varios av_log)
    av_log_format_line(ptr, level, fmt, vl, line, static_cast<int>(sizeof(line)), &printPrefix);
    if (g_sink)
        g_sink->append(line);
}

const AVInputFormat* dshow()
{
    static const AVInputFormat* fmt = [] {
        avdevice_register_all();
        return av_find_input_format("dshow");
    }();
    return fmt;
}

} // namespace

DeviceCatalog& DeviceCatalog::instance()
{
    static DeviceCatalog* catalog = new DeviceCatalog(); // nunca destruido: as threads de leitura sao soltas
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
                caps.ready = false; // formatos podem mudar (outro modo, dispositivo reconectado)
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
    std::vector<std::string> names;
    {
        std::lock_guard av(avMutex_);
        if (const AVInputFormat* ifmt = dshow()) {
            AVDeviceInfoList* list = nullptr;
            const int n = avdevice_list_input_sources(ifmt, nullptr, nullptr, &list);
            if (n >= 0 && list) {
                for (int i = 0; i < list->nb_devices; ++i) {
                    const AVDeviceInfo* d = list->devices[i];
                    if (!d || !d->device_description)
                        continue;
                    bool hasVideo = d->nb_media_types == 0, any = false;
                    for (int t = 0; t < d->nb_media_types; ++t) {
                        any = true;
                        if (d->media_types[t] == AVMEDIA_TYPE_VIDEO)
                            hasVideo = true;
                    }
                    if (any && !hasVideo)
                        continue; // microfones
                    const std::string name = d->device_description;
                    if (std::find(names.begin(), names.end(), name) == names.end())
                        names.push_back(name);
                }
            }
            if (list)
                avdevice_free_list_devices(&list);
        }
    }
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
    std::string text;
    {
        std::lock_guard av(avMutex_);
        if (const AVInputFormat* ifmt = dshow()) {
            {
                std::lock_guard lk(g_sinkMutex);
                g_sink = &text;
            }
            av_log_set_callback(&captureLog);
            const int oldLevel = av_log_get_level();
            av_log_set_level(AV_LOG_INFO);

            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "list_options", "true", 0);
            AVFormatContext* ctx = nullptr;
            const std::string url = "video=" + device;
            avformat_open_input(&ctx, url.c_str(), ifmt, &opts); // sempre "falha": so queremos o log
            av_dict_free(&opts);
            if (ctx)
                avformat_close_input(&ctx);

            av_log_set_level(oldLevel);
            av_log_set_callback(av_log_default_callback);
            std::lock_guard lk(g_sinkMutex);
            g_sink = nullptr;
        }
    }
    std::vector<CaptureFormat> formats = parseFormats(text);
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
        if (sscanf_s(line.c_str() + minPos, "min s=%dx%d fps=%lf max s=%dx%d fps=%lf", &w1, &h1, &f1, &w2, &h2, &f2) != 6)
            continue;
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
    char fmt[64] = {};
    if (sscanf_s(value.c_str(), "%dx%d@%d:%63s", &w, &h, &fps, fmt, static_cast<unsigned>(sizeof(fmt))) == 4 && w > 0 && h > 0 && fps > 0) {
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
    auto better = [](const CaptureFormat& a, const CaptureFormat& b) { // a melhor que b?
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

} // namespace bsl::media
