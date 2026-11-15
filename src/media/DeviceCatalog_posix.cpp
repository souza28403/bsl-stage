#include "media/DeviceBackend.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace bsl::media::devbackend {

namespace {

int xioctl(int fd, unsigned long request, void* arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int openNode(const std::string& path)
{
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    return fd;
}

struct Fd
{
    int fd = -1;
    explicit Fd(int f) : fd(f) {}
    ~Fd()
    {
        if (fd >= 0)
            ::close(fd);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

struct PixInfo
{
    const char* name;
    bool codec;
};

bool pixInfo(std::uint32_t fourcc, PixInfo& out)
{
    switch (fourcc) {
    case V4L2_PIX_FMT_YUYV: out = {"yuyv422", false}; return true;
    case V4L2_PIX_FMT_UYVY: out = {"uyvy422", false}; return true;
    case V4L2_PIX_FMT_NV12: out = {"nv12", false}; return true;
    case V4L2_PIX_FMT_YUV420: out = {"yuv420p", false}; return true;
    case V4L2_PIX_FMT_RGB24: out = {"rgb24", false}; return true;
    case V4L2_PIX_FMT_BGR24: out = {"bgr24", false}; return true;
    case V4L2_PIX_FMT_GREY: out = {"gray", false}; return true;
    case V4L2_PIX_FMT_MJPEG: out = {"mjpeg", true}; return true;
    case V4L2_PIX_FMT_H264: out = {"h264", true}; return true;
#ifdef V4L2_PIX_FMT_HEVC
    case V4L2_PIX_FMT_HEVC: out = {"hevc", true}; return true;
#endif
    default: return false;
    }
}

bool bufTypeFor(int fd, std::uint32_t& type, std::string* card)
{
    v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0)
        return false;
    const std::uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (caps & V4L2_CAP_VIDEO_CAPTURE)
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    else
        return false;
    if (card)
        *card = reinterpret_cast<const char*>(cap.card);
    return true;
}

std::vector<int> videoNodeNumbers()
{
    std::vector<int> nums;
    if (DIR* dir = opendir("/dev")) {
        while (const dirent* e = readdir(dir)) {
            if (std::strncmp(e->d_name, "video", 5) != 0 || e->d_name[5] == '\0')
                continue;
            char* end = nullptr;
            const long n = std::strtol(e->d_name + 5, &end, 10);
            if (end && *end == '\0' && n >= 0)
                nums.push_back(static_cast<int>(n));
        }
        closedir(dir);
    }
    std::sort(nums.begin(), nums.end());
    return nums;
}

std::string pathFromName(const std::string& device)
{
    if (device.rfind("/dev/", 0) == 0)
        return device;
    const size_t open = device.rfind("(/dev/");
    if (open == std::string::npos || device.back() != ')')
        return {};
    return device.substr(open + 1, device.size() - open - 2);
}

double fractFps(const v4l2_fract& f)
{
    return f.numerator ? static_cast<double>(f.denominator) / f.numerator : 0.0;
}

void fpsRange(int fd, std::uint32_t fourcc, int w, int h, double& lo, double& hi)
{
    lo = 0.0;
    hi = 0.0;
    auto widen = [&](double a, double b) {
        const double mn = std::min(a, b), mx = std::max(a, b);
        if (mx <= 0.0)
            return;
        lo = lo > 0.0 ? std::min(lo, mn) : mn;
        hi = std::max(hi, mx);
    };
    for (std::uint32_t i = 0; i < 64; ++i) {
        v4l2_frmivalenum iv;
        std::memset(&iv, 0, sizeof(iv));
        iv.index = i;
        iv.pixel_format = fourcc;
        iv.width = static_cast<std::uint32_t>(w);
        iv.height = static_cast<std::uint32_t>(h);
        if (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv) != 0)
            break;
        if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            const double v = fractFps(iv.discrete);
            widen(v, v);
        } else {
            widen(fractFps(iv.stepwise.min), fractFps(iv.stepwise.max));
            break;
        }
    }
    if (hi <= 0.0) {
        lo = 30.0;
        hi = 30.0;
    }
}

}

std::vector<std::string> listVideoDevices()
{
    std::vector<std::string> names;
    for (const int n : videoNodeNumbers()) {
        const std::string path = "/dev/video" + std::to_string(n);
        Fd f(openNode(path));
        if (f.fd < 0)
            continue;
        std::uint32_t type = 0;
        std::string card;
        if (!bufTypeFor(f.fd, type, &card))
            continue;
        if (card.empty())
            card = "Câmera";
        names.push_back(card + " (" + path + ")");
    }
    return names;
}

std::vector<CaptureFormat> listVideoFormats(const std::string& device)
{
    std::vector<CaptureFormat> out;
    const std::string path = pathFromName(device);
    if (path.empty())
        return out;
    Fd f(openNode(path));
    if (f.fd < 0)
        return out;
    std::uint32_t type = 0;
    if (!bufTypeFor(f.fd, type, nullptr))
        return out;

    static const int kStd[][2] = {{320, 240},  {640, 480},   {800, 600},   {1024, 768},  {1280, 720},
                                  {1280, 1024}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    for (std::uint32_t fi = 0; fi < 64; ++fi) {
        v4l2_fmtdesc desc;
        std::memset(&desc, 0, sizeof(desc));
        desc.index = fi;
        desc.type = type;
        if (xioctl(f.fd, VIDIOC_ENUM_FMT, &desc) != 0)
            break;
        PixInfo info{};
        if (!pixInfo(desc.pixelformat, info))
            continue;

        std::vector<std::pair<int, int>> sizes;
        for (std::uint32_t si = 0; si < 128; ++si) {
            v4l2_frmsizeenum fs;
            std::memset(&fs, 0, sizeof(fs));
            fs.index = si;
            fs.pixel_format = desc.pixelformat;
            if (xioctl(f.fd, VIDIOC_ENUM_FRAMESIZES, &fs) != 0)
                break;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                sizes.emplace_back(static_cast<int>(fs.discrete.width), static_cast<int>(fs.discrete.height));
                continue;
            }
            const int minW = static_cast<int>(fs.stepwise.min_width), maxW = static_cast<int>(fs.stepwise.max_width);
            const int minH = static_cast<int>(fs.stepwise.min_height), maxH = static_cast<int>(fs.stepwise.max_height);
            for (const auto& s : kStd)
                if (s[0] >= minW && s[0] <= maxW && s[1] >= minH && s[1] <= maxH)
                    sizes.emplace_back(s[0], s[1]);
            if (sizes.empty() && maxW > 0 && maxH > 0)
                sizes.emplace_back(maxW, maxH);
            break;
        }
        for (const auto& [w, h] : sizes) {
            if (w <= 0 || h <= 0)
                continue;
            CaptureFormat cf;
            cf.width = w;
            cf.height = h;
            fpsRange(f.fd, desc.pixelformat, w, h, cf.fpsMin, cf.fpsMax);
            (info.codec ? cf.codec : cf.pixelFormat) = info.name;
            out.push_back(std::move(cf));
        }
    }
    return out;
}

OpenSpec makeOpenSpec(const std::string& device, const ChosenFormat& c)
{
    OpenSpec spec;
    spec.inputFormat = "video4linux2";
    const std::string path = pathFromName(device);
    spec.url = path.empty() ? device : path;
    if (!c.automatic) {
        spec.options.emplace_back("video_size", std::to_string(c.width) + "x" + std::to_string(c.height));
        spec.options.emplace_back("framerate", std::to_string(c.fps));
        spec.options.emplace_back("input_format", c.format);
    }
    return spec;
}

}
