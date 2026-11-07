#include "media/ImageSource.h"
#include "media/VideoSource.h"
#include "media/YuvConverter.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

using namespace bsl::media;
namespace fs = std::filesystem;

namespace {

int failures = 0;

struct Px { int r, g, b, a; };

Px readPixel(GLuint tex, int w, int h, int x, int y)
{
    std::vector<unsigned char> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
    return {buf[i], buf[i + 1], buf[i + 2], buf[i + 3]};
}

void expect(const char* name, Px got, Px want, int tol)
{
    const bool ok = std::abs(got.r - want.r) <= tol && std::abs(got.g - want.g) <= tol &&
                    std::abs(got.b - want.b) <= tol && std::abs(got.a - want.a) <= tol;
    std::printf("%-34s %s  got=(%d,%d,%d,%d) want=(%d,%d,%d,%d)\n", name, ok ? "OK  " : "FAIL", got.r, got.g,
                got.b, got.a, want.r, want.g, want.b, want.a);
    if (!ok)
        ++failures;
}

Frame solid(int y, int u, int v, ColorSpace cs, ColorRange range, int pad, int alpha = -1)
{
    Frame f;
    f.allocate(4, 4, alpha >= 0 ? PixelFormat::YUVA420P : PixelFormat::YUV420P);
    f.colorSpace = cs;
    f.range = range;
    const int vals[4] = {y, u, v, alpha};
    for (int p = 0; p < Frame::planeCount(f.format); ++p) {
        const size_t s = static_cast<size_t>(p);
        const int stride = f.strides[s] + pad;
        f.strides[s] = stride;
        f.planes[s].assign(static_cast<size_t>(stride) * static_cast<size_t>(f.planeHeight(p)),
                           static_cast<unsigned char>(vals[p]));
    }
    return f;
}

void testYuvMath()
{
    YuvConverter c;
    auto run = [&](const char* name, const Frame& f, Px want) {
        c.convert(f);
        expect(name, readPixel(c.texture(), c.width(), c.height(), 1, 1), want, 4);
    };
    run("601 limited red", solid(81, 90, 240, ColorSpace::BT601, ColorRange::Limited, 0), {255, 0, 0, 255});
    run("709 limited red", solid(63, 102, 240, ColorSpace::BT709, ColorRange::Limited, 0), {255, 0, 0, 255});
    run("601 full red", solid(76, 85, 255, ColorSpace::BT601, ColorRange::Full, 0), {255, 0, 0, 255});
    run("limited white", solid(235, 128, 128, ColorSpace::BT709, ColorRange::Limited, 0), {255, 255, 255, 255});
    run("limited black", solid(16, 128, 128, ColorSpace::BT709, ColorRange::Limited, 0), {0, 0, 0, 255});
    run("full white", solid(255, 128, 128, ColorSpace::BT709, ColorRange::Full, 0), {255, 255, 255, 255});
    run("stride padded (+12) 709 red", solid(63, 102, 240, ColorSpace::BT709, ColorRange::Limited, 12), {255, 0, 0, 255});
    run("yuva alpha=100", solid(235, 128, 128, ColorSpace::BT709, ColorRange::Limited, 4, 100), {255, 255, 255, 100});
}

void testFile(const fs::path& p, bool wantAlpha)
{
    const std::string name = p.filename().string();
    std::unique_ptr<ISource> src;
    if (p.extension() == ".webm" || p.extension() == ".mp4" || p.extension() == ".mkv" || p.extension() == ".mov")
        src = std::make_unique<VideoSource>();
    else
        src = std::make_unique<ImageSource>();
    std::string err;
    if (!src->open(p, &err)) {
        std::printf("%-34s FAIL  open: %s\n", name.c_str(), err.c_str());
        ++failures;
        return;
    }
    for (int i = 0; i < 40; ++i) {
        src->update(1.0 / 30.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const int w = src->width(), h = src->height();
    const Px corner = readPixel(src->texture(), w, h, 2, 2);
    const Px center = readPixel(src->texture(), w, h, w / 2, h / 2);
    const bool alphaOk = !wantAlpha || (corner.a == 0 && center.a == 255);
    std::printf("%-34s %s  %dx%d pos=%.2f dur=%.2f corner=(%d,%d,%d,%d) center=(%d,%d,%d,%d)\n", name.c_str(),
                (w > 1 && alphaOk) ? "OK  " : "FAIL", w, h, src->position(), src->duration(), corner.r, corner.g,
                corner.b, corner.a, center.r, center.g, center.b, center.a);
    if (w <= 1 || !alphaOk)
        ++failures;
}

}

int main(int argc, char** argv)
{
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "gltest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    testYuvMath();
    if (argc > 1) {
        for (const char* n : {"vp9_alpha.webm", "vp8.webm", "h264_1080p.mp4", "prores.mov", "hevc.mkv", "alpha.png",
                              "photo.jpg", "anim.gif", "anim.webp", "static.webp"}) {
            const fs::path p = fs::path(argv[1]) / n;
            if (fs::exists(p))
                testFile(p, std::string(n).find("alpha") != std::string::npos);
        }
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    std::printf("\n%d falha(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
