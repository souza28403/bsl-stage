#include "app/Log.h"
#include "app/Paths.h"
#include "core/Clip.h"
#include "core/Composition.h"
#include "media/ISource.h"
#include "output/OutputRenderer.h"
#include "output/OutputSetup.h"
#include "render/Compositor.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stb_image_write.h>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace bsl;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kW = 1280;
constexpr int kH = 720;

class BarsSource final : public media::ISource
{
public:
    BarsSource()
    {
        std::vector<unsigned char> px(static_cast<size_t>(kW) * kH * 4);
        static const unsigned char colors[6][3] = {{230, 40, 40}, {40, 200, 60}, {50, 90, 230},
                                                    {240, 220, 40}, {40, 220, 220}, {220, 60, 220}};
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                unsigned char* p = &px[(static_cast<size_t>(y) * kW + x) * 4];
                const unsigned char* c = colors[(x * 6 / kW)];
                const int shade = 120 + 135 * y / kH;
                p[0] = static_cast<unsigned char>(c[0] * shade / 255);
                p[1] = static_cast<unsigned char>(c[1] * shade / 255);
                p[2] = static_cast<unsigned char>(c[2] * shade / 255);
                p[3] = 255;
            }
        }
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    ~BarsSource() override { glDeleteTextures(1, &tex_); }
    bool open(const fs::path&, std::string*) override { return open_ = true; }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    void update(double) override {}
    GLuint texture() const override { return tex_; }
    int width() const override { return kW; }
    int height() const override { return kH; }
    bool hasAlpha() const override { return false; }

private:
    GLuint tex_ = 0;
    bool open_ = true;
};

struct Stats
{
    double maxMs = 0.0;
    double sumMs = 0.0;
    int frames = 0;
    int glErrors = 0;
};

size_t workingSetMb()
{
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.WorkingSetSize / (1024 * 1024);
}

double captureBrightness(const MonitorInfo& m, const std::string& pngPath)
{
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    const int w = m.width, h = m.height;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, m.x, m.y, SRCCOPY | CAPTUREBLT);
    double sum = 0.0;
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    const unsigned char* src = static_cast<const unsigned char*>(bits);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        rgba[i * 4 + 0] = src[i * 4 + 2];
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
        sum += src[i * 4 + 0] + src[i * 4 + 1] + src[i * 4 + 2];
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!pngPath.empty())
        stbi_write_png(pngPath.c_str(), w, h, 4, rgba.data(), w * 4);
    return sum / (static_cast<double>(w) * h * 3.0);
}

}

int main(int argc, char** argv)
{
    double seconds = 60.0;
    int toggles = 10;
    std::string shotDir;
    fs::path appData = fs::temp_directory_path() / "bslstage_outputstress";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--toggles") == 0 && i + 1 < argc)
            toggles = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--shots") == 0 && i + 1 < argc)
            shotDir = argv[++i];
        else if (std::strcmp(argv[i], "--appdata") == 0 && i + 1 < argc)
            appData = argv[++i];
    }
    std::error_code ec;
    fs::create_directories(appData, ec);
    paths::setAppDataOverride(appData);

    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 360, "outputstress", nullptr, nullptr);
    if (!window)
        return 2;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;

    int failures = 0;
    auto check =[&](bool ok, const std::string& what) {
        std::printf("  %s: %s\n", ok ? "ok" : "FALHOU", what.c_str());
        if (!ok)
            ++failures;
    };

    {
        Composition comp = Composition::createDefault(1, 1);
        comp.width = kW;
        comp.height = kH;
        Compositor compositor(kW, kH);
        Clip& clip = comp.clip(0, 0);
        clip.type = Clip::Type::File;
        clip.name = "barras";
        clip.fit = FitMode::Stretch;
        clip.source = std::make_unique<BarsSource>();
        comp.layers[0].activeDeck = 0;
        comp.layers[0].activeColumn = 0;

        OutputRenderer renderer(window);
        const std::vector<MonitorInfo> monitors = renderer.monitors();
        std::printf("monitores: %zu\n", monitors.size());
        const int target = monitors.size() > 1 ? (monitors[0].primary ? 1 : 0) : 0;
        const int other = monitors.size() > 1 ? 1 - target : 0;
        OutputSetup setup = OutputSetup::createDefault(kW, kH, monitors);
        Screen& screen = setup.screens.front();
        screen.kind = monitors.size() > 1 ? OutputKind::Monitor : OutputKind::Window;
        screen.monitorIndex = target;
        screen.monitorName = monitors[static_cast<size_t>(target)].name;
        screen.slices.front() = makeFullSlice("Slice 1", kW, kH, screen.width, screen.height);
        const int screenId = screen.id;

        auto runFrames = [&](double secs, int minFrames, Stats& st) {
            const auto end = Clock::now() + std::chrono::duration<double>(secs);
            int n = 0;
            while (n < minFrames || Clock::now() < end) {
                const auto t0 = Clock::now();
                glfwPollEvents();
                compositor.render(comp, 1.0f);
                renderer.render(setup, compositor, screenId);
                int fw = 0, fh = 0;
                glfwGetFramebufferSize(window, &fw, &fh);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, fw, fh);
                glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glfwSwapBuffers(window);
                while (glGetError() != GL_NO_ERROR)
                    ++st.glErrors;
                const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                if (n > 5) {
                    st.maxMs = std::max(st.maxMs, ms);
                    st.sumMs += ms;
                    ++st.frames;
                }
                ++n;
                const auto target16 = t0 + std::chrono::microseconds(16667);
                if (Clock::now() < target16)
                    std::this_thread::sleep_until(target16);
            }
        };
        auto report = [&](const char* name, const Stats& st) {
            std::printf("%s: %d quadros, medio %.2f ms, pior %.1f ms, erros GL %d, memoria %zu MB\n", name, st.frames,
                        st.frames ? st.sumMs / st.frames : 0.0, st.maxMs, st.glErrors, workingSetMb());
        };

        std::printf("[regime]\n");
        Stats warm;
        runFrames(3.0, 30, warm);
        check(renderer.status(screenId) == ScreenStatus::Active, "saida ativa");
        const size_t memStart = workingSetMb();
        Stats regime;
        runFrames(seconds, 30, regime);
        report("regime", regime);
        const double bright = captureBrightness(monitors[static_cast<size_t>(target)],
                                                shotDir.empty() ? std::string() : shotDir + "\\saida_regime.png");
        std::printf("brilho medio da saida: %.1f\n", bright);
        check(bright > 20.0, "a janela de saida mostra imagem (nao preta)");
        check(regime.glErrors == 0, "sem erro GL");
        check(regime.maxMs < 250.0, "UI sem travar (pior quadro < 250 ms)");
        const size_t memEnd = workingSetMb();
        std::printf("working set: %zu MB -> %zu MB\n", memStart, memEnd);
        check(memEnd < memStart + 150, "sem vazamento obvio no regime");

        std::printf("[abrir e fechar x%d]\n", toggles);
        Stats toggle;
        const size_t memBeforeToggle = workingSetMb();
        for (int i = 0; i < toggles; ++i) {
            setup.screens.front().kind = OutputKind::Disabled;
            runFrames(0.3, 10, toggle);
            setup.screens.front().kind = OutputKind::Monitor;
            setup.screens.front().monitorIndex = target;
            setup.screens.front().monitorName = monitors[static_cast<size_t>(target)].name;
            runFrames(0.8, 20, toggle);
            if (renderer.status(screenId) != ScreenStatus::Active) {
                check(false, "saida reativada na volta " + std::to_string(i));
                break;
            }
        }
        report("toggle", toggle);
        const double brightToggle = captureBrightness(monitors[static_cast<size_t>(target)],
                                                      shotDir.empty() ? std::string() : shotDir + "\\saida_toggle.png");
        std::printf("brilho medio apos toggles: %.1f\n", brightToggle);
        check(brightToggle > 20.0, "saida com imagem depois de abrir/fechar");
        check(toggle.glErrors == 0, "sem erro GL nos toggles");
        std::printf("working set: %zu MB -> %zu MB\n", memBeforeToggle, workingSetMb());

        if (monitors.size() > 1) {
            std::printf("[trocar de monitor]\n");
            Stats swap;
            for (int i = 0; i < 3; ++i) {
                setup.screens.front().monitorIndex = other;
                setup.screens.front().monitorName = monitors[static_cast<size_t>(other)].name;
                runFrames(1.0, 20, swap);
                const std::string otherShot = shotDir.empty() ? std::string() : shotDir + "\\saida_outro_" + std::to_string(i) + ".png";
                const double b1 = captureBrightness(monitors[static_cast<size_t>(other)], otherShot);
                check(b1 > 20.0, "saida visivel no outro monitor (" + std::to_string(b1) + ")");
                setup.screens.front().monitorIndex = target;
                setup.screens.front().monitorName = monitors[static_cast<size_t>(target)].name;
                runFrames(1.0, 20, swap);
                const double b2 = captureBrightness(monitors[static_cast<size_t>(target)], std::string());
                check(b2 > 20.0, "saida de volta ao monitor original (" + std::to_string(b2) + ")");
            }
            report("troca", swap);
            check(swap.glErrors == 0, "sem erro GL na troca");
        }

        std::printf("[janela]\n");
        Stats win;
        setup.screens.front().kind = OutputKind::Window;
        runFrames(2.0, 30, win);
        check(renderer.status(screenId) == ScreenStatus::Active, "saida em janela ativa");
        setup.screens.front().kind = OutputKind::Monitor;
        runFrames(1.5, 20, win);
        report("janela", win);
        check(win.glErrors == 0, "sem erro GL no modo janela");
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf("%s (%d falha(s))\n", failures ? "FALHOU" : "OK", failures);
    return failures ? 1 : 0;
}
