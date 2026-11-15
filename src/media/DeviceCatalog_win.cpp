#include "media/DeviceBackend.h"

#include <algorithm>
#include <cstdarg>
#include <mutex>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/log.h>
}

namespace bsl::media::devbackend {

namespace {

std::mutex g_avMutex;
std::mutex g_sinkMutex;
std::string* g_sink = nullptr;

void captureLog(void* ptr, int level, const char* fmt, va_list vl)
{
    if (level > AV_LOG_INFO)
        return;
    char line[2048];
    std::lock_guard lk(g_sinkMutex);
    static int printPrefix = 1;
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

}

std::vector<std::string> listVideoDevices()
{
    std::vector<std::string> names;
    std::lock_guard av(g_avMutex);
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
                    continue;
                const std::string name = d->device_description;
                if (std::find(names.begin(), names.end(), name) == names.end())
                    names.push_back(name);
            }
        }
        if (list)
            avdevice_free_list_devices(&list);
    }
    return names;
}

std::vector<CaptureFormat> listVideoFormats(const std::string& device)
{
    std::string text;
    {
        std::lock_guard av(g_avMutex);
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
            avformat_open_input(&ctx, url.c_str(), ifmt, &opts);
            av_dict_free(&opts);
            if (ctx)
                avformat_close_input(&ctx);

            av_log_set_level(oldLevel);
            av_log_set_callback(av_log_default_callback);
            std::lock_guard lk(g_sinkMutex);
            g_sink = nullptr;
        }
    }
    return DeviceCatalog::parseFormats(text);
}

OpenSpec makeOpenSpec(const std::string& device, const ChosenFormat& c)
{
    OpenSpec spec;
    spec.inputFormat = "dshow";
    spec.url = "video=" + device;
    spec.options.emplace_back("rtbufsize", "32M");
    if (!c.automatic) {
        spec.options.emplace_back("video_size", std::to_string(c.width) + "x" + std::to_string(c.height));
        spec.options.emplace_back("framerate", std::to_string(c.fps));
        spec.options.emplace_back(c.isCodec ? "vcodec" : "pixel_format", c.format);
    }
    return spec;
}

}
