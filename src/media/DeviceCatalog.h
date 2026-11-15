#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bsl::media {

struct CaptureFormat
{
    int width = 0;
    int height = 0;
    double fpsMin = 0.0;
    double fpsMax = 0.0;
    std::string pixelFormat;
    std::string codec;
};

struct ChosenFormat
{
    bool automatic = true;
    int width = 0, height = 0;
    int fps = 0;
    std::string format;
    bool isCodec = false;
};

class DeviceCatalog
{
public:
    static DeviceCatalog& instance();
    ~DeviceCatalog() = default;

    void ensureListed();
    void refresh();
    bool listing() const;
    bool listed() const;
    std::vector<std::string> deviceNames() const;

    void ensureFormats(const std::string& device);
    bool formatsReady(const std::string& device) const;
    std::vector<CaptureFormat> formats(const std::string& device) const;
    std::vector<CaptureFormat> formatsBlocking(const std::string& device);

    static std::vector<CaptureFormat> parseFormats(const std::string& log);
    static std::string formatValue(const CaptureFormat& f, double fps);
    static ChosenFormat parseValue(const std::string& value);
    static ChosenFormat pickBest(const std::vector<CaptureFormat>& formats);

private:
    DeviceCatalog() = default;
    void readDevices();
    void readFormats(const std::string& device);

    mutable std::mutex m_;
    bool started_ = false;
    bool listing_ = false;
    bool listed_ = false;
    std::vector<std::string> names_;
    struct Caps
    {
        bool ready = false;
        bool pending = false;
        std::vector<CaptureFormat> formats;
    };
    std::map<std::string, Caps> caps_;
};

}
