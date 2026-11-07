#include "core/Composition.h"
#include "media/ISource.h"
#include "media/NdiSource.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiRuntime.h"
#include "output/NdiOutput.h"
#include "output/OutputRenderer.h"
#include "render/Compositor.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace bsl;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

int g_checks = 0;
int g_failures = 0;

void report(bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHOU: %s\n", what.c_str());
    }
}

#define CHECK(cond) report((cond), std::string(#cond) + " (linha " + std::to_string(__LINE__) + ")")

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Rgb
{
    int r = 0, g = 0, b = 0;
};

Rgb quad(int x, int y, int w, int h)
{
    const bool right = x >= w / 2;
    const bool bottom = y >= h / 2;
    if (!bottom)
        return right ? Rgb{0, 255, 0} : Rgb{255, 0, 0};
    return right ? Rgb{255, 255, 0} : Rgb{0, 0, 255};
}

bool closeTo(Rgb a, Rgb b, int tol)
{
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}

std::string str(Rgb p)
{
    return "(" + std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b) + ")";
}

std::vector<std::pair<int, int>> samplePoints(int w, int h)
{
    return {{w / 4, h / 4}, {3 * w / 4, h / 4}, {w / 4, 3 * h / 4}, {3 * w / 4, 3 * h / 4}};
}

bool defaultBt709(int h)
{
    (void)h;
    return true;
}

void rgbToYcc(Rgb p, bool bt709, int& y, int& cb, int& cr)
{
    const double r = p.r, g = p.g, b = p.b;
    if (bt709) {
        y = static_cast<int>(std::lround(16 + (46.559 * r + 156.629 * g + 15.812 * b) / 255.0));
        cb = static_cast<int>(std::lround(128 + (-25.664 * r - 86.336 * g + 112.0 * b) / 255.0));
        cr = static_cast<int>(std::lround(128 + (112.0 * r - 101.730 * g - 10.270 * b) / 255.0));
    } else {
        y = static_cast<int>(std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255.0));
        cb = static_cast<int>(std::lround(128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255.0));
        cr = static_cast<int>(std::lround(128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255.0));
    }
}

Rgb yccToRgb(int y, int cb, int cr, bool bt709)
{
    const double yy = (y - 16) * 255.0 / 219.0, u = (cb - 128) * 255.0 / 224.0, v = (cr - 128) * 255.0 / 224.0;
    double r, g, b;
    if (bt709) {
        r = yy + 1.5748 * v;
        g = yy - 0.187324 * u - 0.468124 * v;
        b = yy + 1.8556 * u;
    } else {
        r = yy + 1.402 * v;
        g = yy - 0.344136 * u - 0.714136 * v;
        b = yy + 1.772 * u;
    }
    auto c = [](double x) { return static_cast<int>(std::lround(std::clamp(x, 0.0, 255.0))); };
    return {c(r), c(g), c(b)};
}

struct Image
{
    int w = 0, h = 0;
    uint32_t fourcc = 0;
    bool bt709 = false;
    int stride = 0;
    std::vector<uint8_t> data;

    Rgb at(int x, int y) const
    {
        const uint8_t* row = data.data() + static_cast<size_t>(y) * static_cast<size_t>(stride);
        if (fourcc == NDIlib_FourCC_video_type_UYVY) {
            const uint8_t* q = row + (x / 2) * 4;
            return yccToRgb((x & 1) ? q[3] : q[1], q[0], q[2], bt709);
        }
        const uint8_t* p = row + x * 4;
        if (fourcc == NDIlib_FourCC_video_type_RGBA || fourcc == NDIlib_FourCC_video_type_RGBX)
            return {p[0], p[1], p[2]};
        return {p[2], p[1], p[0]};
    }
};

Image makeBgra(int w, int h)
{
    Image im;
    im.w = w;
    im.h = h;
    im.fourcc = NDIlib_FourCC_video_type_BGRA;
    im.stride = w * 4;
    im.data.resize(static_cast<size_t>(im.stride) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Rgb c = quad(x, y, w, h);
            uint8_t* p = &im.data[static_cast<size_t>(y) * static_cast<size_t>(im.stride) + static_cast<size_t>(x) * 4];
            p[0] = static_cast<uint8_t>(c.b);
            p[1] = static_cast<uint8_t>(c.g);
            p[2] = static_cast<uint8_t>(c.r);
            p[3] = 255;
        }
    return im;
}

Image makeUyvy(int w, int h, bool bt709)
{
    Image im;
    im.w = w;
    im.h = h;
    im.bt709 = bt709;
    im.fourcc = NDIlib_FourCC_video_type_UYVY;
    im.stride = w * 2;
    im.data.resize(static_cast<size_t>(im.stride) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; x += 2) {
            int y0, cb, cr, y1, cb1, cr1;
            rgbToYcc(quad(x, y, w, h), bt709, y0, cb, cr);
            rgbToYcc(quad(x + 1, y, w, h), bt709, y1, cb1, cr1);
            uint8_t* q = &im.data[static_cast<size_t>(y) * static_cast<size_t>(im.stride) + static_cast<size_t>(x) * 2];
            q[0] = static_cast<uint8_t>((cb + cb1) / 2);
            q[1] = static_cast<uint8_t>(y0);
            q[2] = static_cast<uint8_t>((cr + cr1) / 2);
            q[3] = static_cast<uint8_t>(y1);
        }
    return im;
}

struct RawSender
{
    const NDIlib_v5_t* api = nullptr;
    NDIlib_send_instance_t inst = nullptr;

    RawSender(ndi::Runtime& rt, const char* name) : api(rt.api())
    {
        NDIlib_send_create_t c{};
        c.p_ndi_name = name;
        c.clock_video = false;
        c.clock_audio = false;
        inst = api->send_create(&c);
    }
    ~RawSender()
    {
        if (inst)
            api->send_destroy(inst);
    }
    void send(const Image& im)
    {
        NDIlib_video_frame_v2_t f{};
        f.xres = im.w;
        f.yres = im.h;
        f.FourCC = static_cast<NDIlib_FourCC_video_type_e>(im.fourcc);
        f.frame_rate_N = 30;
        f.frame_rate_D = 1;
        f.picture_aspect_ratio = static_cast<float>(im.w) / static_cast<float>(im.h);
        f.frame_format_type = NDIlib_frame_format_type_progressive;
        f.timecode = NDIlib_send_timecode_synthesize;
        f.p_data = const_cast<uint8_t*>(im.data.data());
        f.line_stride_in_bytes = im.stride;
        api->send_send_video_v2(inst, &f);
    }
};

struct RawReceiver
{
    const NDIlib_v5_t* api = nullptr;
    NDIlib_recv_instance_t inst = nullptr;

    RawReceiver(ndi::Runtime& rt, const std::string& source, NDIlib_recv_color_format_e fmt) : api(rt.api())
    {
        NDIlib_recv_create_v3_t c{};
        c.source_to_connect_to.p_ndi_name = source.c_str();
        c.color_format = fmt;
        c.bandwidth = NDIlib_recv_bandwidth_highest;
        c.allow_video_fields = false;
        c.p_ndi_recv_name = "ndittest";
        inst = api->recv_create_v3(&c);
    }
    ~RawReceiver()
    {
        if (inst)
            api->recv_destroy(inst);
    }
    bool capture(Image& out, int timeoutMs)
    {
        NDIlib_video_frame_v2_t f{};
        if (api->recv_capture_v3(inst, &f, nullptr, nullptr, static_cast<uint32_t>(timeoutMs)) != NDIlib_frame_type_video)
            return false;
        out.w = f.xres;
        out.h = f.yres;
        out.fourcc = f.FourCC;
        out.bt709 = defaultBt709(f.yres);
        out.stride = f.line_stride_in_bytes;
        out.data.assign(f.p_data, f.p_data + static_cast<size_t>(f.line_stride_in_bytes) * static_cast<size_t>(f.yres));
        api->recv_free_video_v2(inst, &f);
        return true;
    }
};

const char* fourccName(uint32_t f)
{
    switch (f) {
    case NDIlib_FourCC_video_type_UYVY: return "UYVY";
    case NDIlib_FourCC_video_type_UYVA: return "UYVA";
    case NDIlib_FourCC_video_type_BGRA: return "BGRA";
    case NDIlib_FourCC_video_type_BGRX: return "BGRX";
    case NDIlib_FourCC_video_type_RGBA: return "RGBA";
    case NDIlib_FourCC_video_type_RGBX: return "RGBX";
    default: return "?";
    }
}

std::string waitForSource(ndi::Finder& finder, const std::string& name, int timeoutMs = 15000,
                          const std::function<void()>& pump = {})
{
    const std::string suffix = "(" + name + ")";
    const auto t0 = Clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < timeoutMs) {
        for (const ndi::SourceInfo& s : finder.sources())
            if (s.name.size() >= suffix.size() && s.name.compare(s.name.size() - suffix.size(), suffix.size(), suffix) == 0)
                return s.name;
        if (pump)
            pump();
        sleepMs(50);
    }
    return {};
}

void checkQuadrants(const Image& im, int tol, const std::string& tag)
{
    for (auto [x, y] : samplePoints(im.w, im.h)) {
        const Rgb got = im.at(x, y), want = quad(x, y, im.w, im.h);
        const bool ok = closeTo(got, want, tol);
        report(ok, tag + ": pixel (" + std::to_string(x) + "," + std::to_string(y) + ") veio " + str(got) + ", esperado " + str(want));
    }
}

Image readTexture(GLuint tex, int w, int h)
{
    Image im;
    im.w = w;
    im.h = h;
    im.fourcc = NDIlib_FourCC_video_type_RGBA;
    im.stride = w * 4;
    im.data.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return im;
}

bool pumpSource(media::NdiSource& src, int frames, int timeoutMs, const std::function<void()>& pump)
{
    const auto t0 = Clock::now();
    unsigned long long base = src.frameCount();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < timeoutMs) {
        if (pump)
            pump();
        src.update(0.016);
        if (src.state() == media::NdiSource::State::Live && src.frameCount() >= base + static_cast<unsigned long long>(frames))
            return true;
        sleepMs(15);
    }
    return false;
}

fs::path g_lib;

void testRuntime()
{
    section("runtime: carga, versao e tabela de funcoes");
    ndi::Runtime& rt = ndi::Runtime::instance();
    CHECK(rt.available());
    if (!rt.available()) {
        std::printf("  erro: %s\n", rt.error().c_str());
        return;
    }
    std::printf("  runtime: %s\n  versao: %s\n", rt.libraryPath().string().c_str(), rt.version().c_str());
    CHECK(!rt.version().empty());
    CHECK(rt.libraryPath() == g_lib);

    HMODULE mod = GetModuleHandleW(g_lib.filename().c_str());
    CHECK(mod != nullptr);
    const NDIlib_v5_t* api = rt.api();
    struct Slot
    {
        const char* symbol;
        const void* ptr;
    };
    const Slot slots[] = {
        {"NDIlib_initialize", reinterpret_cast<const void*>(api->initialize)},
        {"NDIlib_destroy", reinterpret_cast<const void*>(api->destroy)},
        {"NDIlib_version", reinterpret_cast<const void*>(api->version)},
        {"NDIlib_is_supported_CPU", reinterpret_cast<const void*>(api->is_supported_CPU)},
        {"NDIlib_find_create_v2", reinterpret_cast<const void*>(api->find_create_v2)},
        {"NDIlib_find_destroy", reinterpret_cast<const void*>(api->find_destroy)},
        {"NDIlib_find_wait_for_sources", reinterpret_cast<const void*>(api->find_wait_for_sources)},
        {"NDIlib_find_get_current_sources", reinterpret_cast<const void*>(api->find_get_current_sources)},
        {"NDIlib_recv_create_v3", reinterpret_cast<const void*>(api->recv_create_v3)},
        {"NDIlib_recv_connect", reinterpret_cast<const void*>(api->recv_connect)},
        {"NDIlib_recv_destroy", reinterpret_cast<const void*>(api->recv_destroy)},
        {"NDIlib_recv_capture_v3", reinterpret_cast<const void*>(api->recv_capture_v3)},
        {"NDIlib_recv_free_video_v2", reinterpret_cast<const void*>(api->recv_free_video_v2)},
        {"NDIlib_recv_get_queue", reinterpret_cast<const void*>(api->recv_get_queue)},
        {"NDIlib_recv_get_no_connections", reinterpret_cast<const void*>(api->recv_get_no_connections)},
        {"NDIlib_send_create", reinterpret_cast<const void*>(api->send_create)},
        {"NDIlib_send_destroy", reinterpret_cast<const void*>(api->send_destroy)},
        {"NDIlib_send_send_video_v2", reinterpret_cast<const void*>(api->send_send_video_v2)},
        {"NDIlib_send_send_video_async_v2", reinterpret_cast<const void*>(api->send_send_video_async_v2)},
        {"NDIlib_send_get_no_connections", reinterpret_cast<const void*>(api->send_get_no_connections)},
        {"NDIlib_send_get_source_name", reinterpret_cast<const void*>(api->send_get_source_name)},
    };
    for (const Slot& s : slots) {
        const void* exported = mod ? reinterpret_cast<const void*>(GetProcAddress(mod, s.symbol)) : nullptr;
        report(exported != nullptr && exported == s.ptr, std::string("tabela == exportacao: ") + s.symbol);
    }

    SetEnvironmentVariableW(L"NDI_RUNTIME_DIR_V6", nullptr);
    SetEnvironmentVariableW(L"NDI_RUNTIME_DIR_V5", g_lib.parent_path().c_str());
    const auto cands = ndi::Runtime::candidates({});
    bool hasEnv = false;
    for (const fs::path& c : cands)
        hasEnv = hasEnv || c == g_lib.parent_path() / ndi::kRuntimeDllName;
    CHECK(hasEnv);
    {
        ndi::Runtime viaEnv;
        CHECK(viaEnv.load());
        CHECK(viaEnv.available());
    }
    SetEnvironmentVariableW(L"NDI_RUNTIME_DIR_V5", nullptr);
    CHECK(ndi::Runtime::candidates(g_lib).size() == 1);
}

void testRawLoopback()
{
    section("sender \"BSL Stage Test\" -> Finder -> receptor (quadro conhecido)");
    ndi::Runtime& rt = ndi::Runtime::instance();
    if (!rt.available())
        return;
    ndi::Finder finder(rt);
    finder.start();
    CHECK(finder.running());

    struct Case
    {
        const char* name;
        Image image;
        NDIlib_recv_color_format_e fmt;
        int tol;
    };
    Case cases[] = {
        {"BSL Stage Test", makeBgra(64, 32), NDIlib_recv_color_format_BGRX_BGRA, 2},
        {"BSL Stage Test UYVY", makeUyvy(64, 32, defaultBt709(32)), NDIlib_recv_color_format_UYVY_BGRA, 14},
        {"BSL Stage Test UYVY2BGRA", makeUyvy(64, 32, defaultBt709(32)), NDIlib_recv_color_format_BGRX_BGRA, 14},
    };
    for (Case& c : cases) {
        RawSender sender(rt, c.name);
        CHECK(sender.inst != nullptr);
        if (!sender.inst)
            continue;
        const std::string full = waitForSource(finder, c.name, 15000, [&] { sender.send(c.image); });
        report(!full.empty(), std::string("o Finder achou \"") + c.name + "\"");
        if (full.empty())
            continue;
        std::printf("  achou: %s\n", full.c_str());

        RawReceiver rx(rt, full, c.fmt);
        CHECK(rx.inst != nullptr);
        Image got;
        bool have = false;
        const auto t0 = Clock::now();
        while (!have && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 10000) {
            sender.send(c.image);
            have = rx.capture(got, 40);
        }
        report(have, std::string("receptor recebeu quadro de \"") + c.name + "\"");
        if (!have)
            continue;
        std::printf("  recebido: %dx%d %s\n", got.w, got.h, fourccName(got.fourcc));
        CHECK(got.w == 64 && got.h == 32);
        checkQuadrants(got, c.tol, c.name);
    }
    finder.stop();
}

void testUyvyMatrix()
{
    section("matriz do UYVY no SDK por altura (BT.601 x BT.709)");
    ndi::Runtime& rt = ndi::Runtime::instance();
    if (!rt.available())
        return;
    ndi::Finder finder(rt);
    finder.start();
    struct Size
    {
        int w, h;
    };
    int n = 0;
    for (const Size sz : {Size{64, 32}, Size{720, 480}, Size{1280, 720}, Size{1920, 1080}}) {
        for (const bool enc709 : {false, true}) {
            const std::string name = "BSL Stage Test Matriz " + std::to_string(n++);
            Image im = makeUyvy(sz.w, sz.h, enc709);
            RawSender sender(rt, name.c_str());
            const std::string full = waitForSource(finder, name, 15000, [&] { sender.send(im); });
            if (full.empty()) {
                report(false, "Finder achou " + name);
                continue;
            }
            RawReceiver rx(rt, full, NDIlib_recv_color_format_BGRX_BGRA);
            Image got;
            bool have = false;
            const auto t0 = Clock::now();
            while (!have && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 10000) {
                sender.send(im);
                have = rx.capture(got, 40);
            }
            if (!have) {
                report(false, "receptor recebeu " + name);
                continue;
            }
            const Rgb px = got.at(got.w / 4, got.h / 4), want = quad(got.w / 4, got.h / 4, got.w, got.h);
            const bool same = closeTo(px, want, 6);
            std::printf("  %dx%d codificado em BT.%s -> SDK devolveu %s (esperado %s) %s\n", sz.w, sz.h, enc709 ? "709" : "601",
                        str(px).c_str(), str(want).c_str(), same ? "= mesma matriz" : "matriz diferente");
            if (enc709)
                report(same, "o SDK decodifica UYVY como BT.709 em " + std::to_string(sz.h) + " linhas");
        }
    }
    finder.stop();
}

void testNdiSource()
{
    section("fonte NDI do app (NdiSource): orientacao e cores");
    ndi::Runtime& rt = ndi::Runtime::instance();
    if (!rt.available())
        return;
    ndi::Finder finder(rt);
    finder.start();

    struct Case
    {
        const char* name;
        Image image;
        int tol;
        bool lowLatency;
        bool lowBandwidth;
    };
    Case cases[] = {
        {"BSL Stage Test SrcUYVY", makeUyvy(64, 32, defaultBt709(32)), 14, false, false},
        {"BSL Stage Test SrcBGRA", makeBgra(64, 32), 2, true, false},
        {"BSL Stage Test SrcUYVYhd", makeUyvy(1280, 720, true), 40, false, true},
    };
    for (Case& c : cases) {
        RawSender sender(rt, c.name);
        const std::string full = waitForSource(finder, c.name, 15000, [&] { sender.send(c.image); });
        report(!full.empty(), std::string("achou \"") + c.name + "\"");
        if (full.empty())
            continue;
        media::NdiSource src;
        media::ParamValues pv;
        pv.set("source", full);
        pv.set("lowLatency", c.lowLatency);
        pv.set("lowBandwidth", c.lowBandwidth);
        src.setParams(pv);
        CHECK(src.open({}));
        CHECK(src.isLive());
        const bool live = pumpSource(src, 3, 15000, [&] { sender.send(c.image); });
        report(live, std::string("NdiSource ficou Live com \"") + c.name + "\"");
        if (live) {
            if (!c.lowBandwidth)
                CHECK(src.width() == c.image.w && src.height() == c.image.h);
            CHECK(src.status().level == media::SourceStatus::Level::Info);
            std::printf("  status: %s\n", src.status().text.c_str());
            const Image tex = readTexture(src.texture(), src.width(), src.height());
            checkQuadrants(tex, c.tol, std::string("NdiSource ") + c.name);
        }
        src.close();
    }
    finder.stop();
}

GLuint makeQuadrantTexture(int w, int h)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int row = 0; row < h; ++row)
        for (int x = 0; x < w; ++x) {
            const Rgb c = quad(x, h - 1 - row, w, h);
            uint8_t* p = &px[(static_cast<size_t>(row) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
            p[0] = static_cast<uint8_t>(c.r);
            p[1] = static_cast<uint8_t>(c.g);
            p[2] = static_cast<uint8_t>(c.b);
            p[3] = 255;
        }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void testNdiOutput()
{
    section("NdiOutput (GL -> PBO -> NDI): UYVY e BGRA lidos por um receptor do proprio NDI");
    ndi::Runtime& rt = ndi::Runtime::instance();
    if (!rt.available())
        return;
    ndi::Finder finder(rt);
    finder.start();
    constexpr int W = 64, H = 32;
    const GLuint tex = makeQuadrantTexture(W, H);

    for (const bool bgra : {false, true}) {
        const std::string name = bgra ? "BSL Stage Test OutBGRA" : "BSL Stage Test OutUYVY";
        NdiOutput out;
        out.setName(name);
        out.setBgra(bgra);
        out.setFrameRate(30.0f);
        CHECK(out.runtimeAvailable());
        CHECK(out.send(tex, W, H));
        const std::string full = waitForSource(finder, name, 15000, [&] { out.send(tex, W, H); });
        report(!full.empty(), "o Finder achou " + name);
        if (full.empty())
            continue;
        std::printf("  sender: %s (nome real: %s)\n", full.c_str(), out.actualName().c_str());

        RawReceiver rx(rt, full, NDIlib_recv_color_format_BGRX_BGRA);
        Image got;
        bool have = false;
        const auto t0 = Clock::now();
        while (!have && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 10000) {
            out.send(tex, W, H);
            have = rx.capture(got, 20);
            sleepMs(5);
        }
        report(have, "receptor recebeu quadro de " + name);
        if (have) {
            std::printf("  recebido: %dx%d %s; receptores: %d; enviados: %llu\n", got.w, got.h, fourccName(got.fourcc),
                        out.connections(), out.framesSent());
            CHECK(got.w == W && got.h == H);
            CHECK(out.connections() >= 1);
            checkQuadrants(got, bgra ? 2 : 16, name);
        }
        out.release();
    }

    {
        NdiOutput out;
        out.setName("BSL Stage Test Ritmo");
        out.setFrameRate(10.0f);
        out.send(tex, W, H);
        const std::string full = waitForSource(finder, "BSL Stage Test Ritmo", 15000, [&] { out.send(tex, W, H); });
        if (!full.empty()) {
            RawReceiver rx(rt, full, NDIlib_recv_color_format_UYVY_BGRA);
            Image got;
            for (int i = 0; i < 40; ++i) {
                out.send(tex, W, H);
                rx.capture(got, 10);
                sleepMs(8);
            }
            const unsigned long long before = out.framesSent();
            const auto t0 = Clock::now();
            int sends = 0;
            while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 2000) {
                out.send(tex, W, H);
                ++sends;
                sleepMs(16);
            }
            const unsigned long long sent = out.framesSent() - before;
            std::printf("  10 fps: %llu quadros em ~2 s (%d chamadas)\n", sent, sends);
            CHECK(sent >= 14 && sent <= 26);
        } else {
            report(false, "Finder achou o sender do teste de ritmo");
        }
    }
    glDeleteTextures(1, &tex);
    finder.stop();
}

class QuadrantSource final : public media::ISource
{
public:
    QuadrantSource(int w, int h) : w_(w), h_(h)
    {
        std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Rgb c = quad(x, y, w, h);
                uint8_t* p = &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
                p[0] = static_cast<uint8_t>(c.r);
                p[1] = static_cast<uint8_t>(c.g);
                p[2] = static_cast<uint8_t>(c.b);
                p[3] = 255;
            }
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
    ~QuadrantSource() override { glDeleteTextures(1, &tex_); }
    bool open(const fs::path&, std::string*) override
    {
        open_ = true;
        return true;
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    void update(double) override {}
    GLuint texture() const override { return tex_; }
    int width() const override { return w_; }
    int height() const override { return h_; }
    bool hasAlpha() const override { return false; }

private:
    int w_, h_;
    GLuint tex_ = 0;
    bool open_ = true;
};

void testScreenOutput(GLFWwindow* window)
{
    section("saida NDI de uma Screen (OutputRenderer) vista por uma NdiSource");
    ndi::Runtime& rt = ndi::Runtime::instance();
    if (!rt.available())
        return;
    constexpr int W = 64, H = 32;

    Composition comp = Composition::createDefault(1, 1);
    comp.width = W;
    comp.height = H;
    Compositor compositor(W, H);
    Clip& clip = comp.clip(0, 0);
    clip.type = Clip::Type::File;
    clip.name = "teste";
    clip.fit = FitMode::Stretch;
    clip.source = std::make_unique<QuadrantSource>(W, H);
    comp.layers[0].activeDeck = 0;
    comp.layers[0].activeColumn = 0;

    OutputRenderer renderer(window);
    renderer.setFrameRate(60.0f);
    OutputSetup setup = OutputSetup::createDefault(W, H, {});
    Screen& screen = setup.screens.front();
    screen.kind = OutputKind::Ndi;
    screen.ndiName = "BSL Stage Test Screen";
    screen.width = W;
    screen.height = H;
    screen.slices.front() = makeFullSlice("Slice 1", W, H, W, H);
    const int screenId = screen.id;

    auto frame = [&] {
        compositor.render(comp, 1.0f);
        renderer.render(setup, compositor, -1);
    };
    frame();
    CHECK(renderer.status(screenId) == ScreenStatus::Active);

    ndi::Finder finder(rt);
    finder.start();
    const std::string full = waitForSource(finder, "BSL Stage Test Screen", 15000, [&] { frame(); });
    report(!full.empty(), "o Finder achou a Screen \"BSL Stage Test Screen\"");
    if (!full.empty()) {
        media::NdiSource src;
        media::ParamValues pv;
        pv.set("source", full);
        src.setParams(pv);
        CHECK(src.open({}));
        const bool live = pumpSource(src, 3, 15000, [&] {
            frame();
            sleepMs(8);
        });
        report(live, "a fonte NDI recebeu a Screen");
        if (live) {
            CHECK(renderer.ndiConnections(screenId) >= 1);
            CHECK(src.width() == W && src.height() == H);
            std::vector<unsigned char> truth;
            int tw = 0, th = 0;
            CHECK(renderer.readScreen(screenId, truth, tw, th));
            const Image got = readTexture(src.texture(), src.width(), src.height());
            checkQuadrants(got, 14, "Screen -> NDI -> NdiSource");
            if (tw == W && th == H) {
                for (auto [x, y] : samplePoints(W, H)) {
                    const unsigned char* t = &truth[(static_cast<size_t>(y) * W + static_cast<size_t>(x)) * 4];
                    const Rgb want{t[0], t[1], t[2]};
                    const Rgb have = got.at(x, y);
                    report(closeTo(have, want, 14), "mesmo conteudo do readScreen em (" + std::to_string(x) + "," + std::to_string(y)
                                                     + "): " + str(have) + " x " + str(want));
                }
            }
        }
        src.close();
    }

    screen.kind = OutputKind::Disabled;
    frame();
    CHECK(renderer.status(screenId) == ScreenStatus::Idle);
    finder.stop();
}

void testNoRuntime()
{
    section("sem runtime (caminho invalido): falha graciosa");
    ndi::Runtime bad;
    CHECK(!bad.load(fs::path(L"C:\\nao\\existe\\Processing.NDI.Lib.x64.dll")));
    CHECK(!bad.available());
    CHECK(bad.api() == nullptr);
    CHECK(!bad.error().empty());
    std::printf("  erro: %s\n", bad.error().c_str());
    CHECK(bad.error().find(ndi::kMissingText) != std::string::npos);

    ndi::Runtime notNdi;
    CHECK(!notNdi.load(fs::path(L"C:\\Windows\\System32\\kernel32.dll")));
    CHECK(!notNdi.available());

    ndi::Finder finder(bad);
    finder.start();
    CHECK(!finder.running());
    CHECK(finder.sources().empty());

    NdiOutput out(&bad);
    out.setName("nao deve existir");
    CHECK(!out.runtimeAvailable());
    CHECK(!out.send(1, 64, 32));
    out.release();

    media::NdiSource src(&bad);
    media::ParamValues pv;
    pv.set("source", "QUALQUER (fonte)");
    src.setParams(pv);
    CHECK(src.open({}));
    for (int i = 0; i < 5; ++i)
        src.update(0.016);
    CHECK(src.state() == media::NdiSource::State::NoRuntime);
    CHECK(src.status().text == ndi::kMissingText);
    CHECK(src.status().level == media::SourceStatus::Level::Warning);
    CHECK(src.texture() != 0);
    src.close();
}

}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--ndi-lib") == 0 && i + 1 < argc)
            g_lib = fs::absolute(fs::path(argv[++i]));

    if (!glfwInit()) {
        std::printf("FAIL: glfwInit\n");
        return 2;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(256, 256, "ndittest", nullptr, nullptr);
    if (!window) {
        std::printf("FAIL: janela GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::printf("FAIL: glad\n");
        return 2;
    }

    testNoRuntime();
    if (g_lib.empty()) {
        std::printf("\nSem --ndi-lib: testes com o runtime ignorados.\n");
    } else {
        ndi::Runtime::setLibraryOverride(g_lib);
        testRuntime();
        testRawLoopback();
        testUyvyMatrix();
        testNdiSource();
        testNdiOutput();
        testScreenOutput(window);
    }

    ndi::Finder::shutdownShared();
    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "NDI: OK\n" : "NDI: FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
