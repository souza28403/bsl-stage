#include "audio/AudioAnalyzer.h"
#include "core/Clip.h"
#include "media/CaptureDeviceSource.h"
#include "media/CaptureTargets.h"
#include "media/DeviceCatalog.h"
#include "media/FontCatalog.h"
#ifdef _WIN32
#include "media/ScreenCaptureSource.h"
#endif
#include "media/TextRaster.h"
#include "media/TextSource.h"
#ifdef _WIN32
#include "render/D3D11Interop.h"
#endif
#include "render/SolidColorSource.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace bsl;
using namespace bsl::media;
using Clock = std::chrono::steady_clock;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skipped = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);              \
        }                                                                           \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

struct Pixels
{
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    const unsigned char* at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4]; }
};

Pixels readTexture(GLuint tex, int w, int h)
{
    Pixels p;
    p.w = w;
    p.h = h;
    p.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return p;
}

long countVisible(const Pixels& p, int x0, int y0, int x1, int y1)
{
    long n = 0;
    for (int y = std::max(0, y0); y < std::min(p.h, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(p.w, x1); ++x) {
            const unsigned char* px = p.at(x, y);
            if (px[3] > 0 && (px[0] > 8 || px[1] > 8 || px[2] > 8))
                ++n;
        }
    return n;
}

long countAlpha(const Pixels& p, int x0, int y0, int x1, int y1)
{
    long n = 0;
    for (int y = std::max(0, y0); y < std::min(p.h, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(p.w, x1); ++x)
            if (p.at(x, y)[3] > 0)
                ++n;
    return n;
}

double meanAbsDiff(const Pixels& a, const Pixels& b)
{
    if (a.rgba.size() != b.rgba.size() || a.rgba.empty())
        return 1e9;
    double sum = 0.0;
    for (size_t i = 0; i < a.rgba.size(); ++i)
        sum += std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i]));
    return sum / static_cast<double>(a.rgba.size());
}

ParamValues textParams(const std::string& text, int size = 120)
{
    ParamValues v;
    v.fillDefaults(TextSource::describe(v));
    v.set("text", text);
    v.set("size", size);
    return v;
}

void settle(ISource& s, int frames = 3)
{
    for (int i = 0; i < frames; ++i)
        s.update(1.0 / 60.0);
}

void testText()
{
    section("texto: pixels e acentos");
    const auto cps = decodeUtf8("A\xC3\xA7\xC3\xA3o \xC3\x89");
    CHECK(cps.size() == 6 && cps[1] == 0xE7 && cps[2] == 0xE3 && cps[5] == 0xC9);

    TextSource src(1280, 720);
    src.setParams(textParams("Olá, ação"));
    CHECK(src.open({}));
    settle(src);
    const Pixels p = readTexture(src.texture(), 1280, 720);
    const long middle = countVisible(p, 0, 240, 1280, 480);
    const long top = countVisible(p, 0, 0, 1280, 120);
    std::printf("  pixels visiveis: meio=%ld topo=%ld\n", middle, top);
    CHECK(middle > 1000);
    CHECK(top == 0);
    CHECK(countVisible(p, 0, 0, 100, 720) == 0 && countVisible(p, 1180, 0, 1280, 720) == 0);

    TextStyle st;
    st.fontPath = FontCatalog::instance().resolve(FontCatalog::instance().defaultFile());
    st.sizePx = 100;
    auto visible = [&](const char* text) {
        st.text = text;
        TextBitmap bmp;
        rasterizeText(st, 400, 300, 4096, bmp);
        Pixels q;
        q.w = bmp.width;
        q.h = bmp.height;
        q.rgba = bmp.rgba;
        return countVisible(q, 0, 0, q.w, q.h);
    };
    const long e = visible("E"), eAcute = visible("\xC3\x89");
    const long c = visible("c"), cCedilla = visible("\xC3\xA7");
    const long a = visible("a"), aTilde = visible("\xC3\xA3");
    std::printf("  E=%ld É=%ld c=%ld ç=%ld a=%ld ã=%ld\n", e, eAcute, c, cCedilla, a, aTilde);
    CHECK(eAcute > e && cCedilla > c && aTilde > a);

    section("texto: so rasteriza quando um parametro muda");
    const int before = src.rasterCount();
    settle(src, 10);
    CHECK(src.rasterCount() == before);
    ParamValues v = textParams("Outro texto");
    src.setParams(v);
    src.update(1.0 / 60.0);
    CHECK(src.rasterCount() == before + 1);
    const Pixels p2 = readTexture(src.texture(), 1280, 720);
    CHECK(meanAbsDiff(p, p2) > 0.01);

    section("texto: contorno, sombra e fundo");
    v = textParams("Efeitos");
    v.set("outline", true);
    v.set("shadow", true);
    v.set("bg", true);
    v.set("bgMode", "full");
    src.setParams(v);
    settle(src);
    const Pixels fx = readTexture(src.texture(), 1280, 720);
    CHECK(countAlpha(fx, 0, 0, 20, 20) == 400);
    v.set("bgMode", "box");
    src.setParams(v);
    settle(src);
    const Pixels box = readTexture(src.texture(), 1280, 720);
    CHECK(countAlpha(box, 0, 0, 20, 20) == 0);
    CHECK(countAlpha(box, 640 - 40, 360 - 20, 640 + 40, 360 + 20) > 0);
    v.set("bg", false);
    src.setParams(v);
    settle(src);
    const Pixels nobg = readTexture(src.texture(), 1280, 720);
    CHECK(countAlpha(box, 0, 0, 1280, 720) > countAlpha(nobg, 0, 0, 1280, 720));
    src.close();
}

void testMarquee()
{
    section("letreiro horizontal: desloca, loop sem emenda, sem re-rasterizar");
    TextSource src(1280, 720);
    ParamValues v = textParams("Letreiro de teste: ação e emoção");
    v.set("marquee", "horizontal");
    v.set("marqueeSpeed", 400.0);
    v.set("outline", true);
    src.setParams(v);
    CHECK(src.open({}));
    CHECK(src.scrolling());
    const int raster = src.rasterCount();
    const int period = src.stripWidth();
    std::printf("  periodo = %d px\n", period);

    src.update(0.0);
    const Pixels f0 = readTexture(src.texture(), 1280, 720);
    src.update(0.25);
    const Pixels f1 = readTexture(src.texture(), 1280, 720);
    CHECK(meanAbsDiff(f0, f1) > 0.001);
    CHECK(std::abs(src.scrollOffset() - std::fmod(static_cast<double>(period - 1280 > 0 ? period - 1280 : 0) + 100.0, period)) < 1.0);

    double shifted = 0.0;
    long n = 0;
    for (int y = 0; y < 720; y += 3)
        for (int x = 0; x < 1100; x += 3) {
            shifted += std::abs(static_cast<int>(f1.at(x, y)[3]) - static_cast<int>(f0.at(x + 100, y)[3]));
            ++n;
        }
    std::printf("  erro medio do deslocamento (alpha) = %.3f\n", shifted / static_cast<double>(n));
    CHECK(shifted / static_cast<double>(n) < 1.0);

    src.update(static_cast<double>(period) / 400.0);
    const Pixels f2 = readTexture(src.texture(), 1280, 720);
    const double loopDiff = meanAbsDiff(f1, f2);
    std::printf("  diferenca apos 1 periodo = %.4f\n", loopDiff);
    CHECK(loopDiff < 0.5);
    CHECK(src.rasterCount() == raster);

    section("letreiro: pausa e velocidade negativa");
    src.pause();
    const double off = src.scrollOffset();
    src.update(0.5);
    CHECK(src.scrollOffset() == off);
    src.play();
    v.set("marqueeSpeed", -400.0);
    src.setParams(v);
    src.update(0.0);
    const double o1 = src.scrollOffset();
    src.update(0.1);
    CHECK(src.scrollOffset() < o1 || o1 < 50.0);
    src.close();

    section("letreiro vertical");
    TextSource vs(1280, 720);
    v = textParams("Linha 1\nLinha 2\nLinha 3\nLinha 4\nLinha 5\nLinha 6", 90);
    v.set("marquee", "vertical");
    v.set("marqueeSpeed", 300.0);
    vs.setParams(v);
    CHECK(vs.open({}));
    CHECK(vs.scrolling() && vs.stripHeight() > 720);
    vs.update(0.0);
    const Pixels v0 = readTexture(vs.texture(), 1280, 720);
    vs.update(0.5);
    const Pixels v1 = readTexture(vs.texture(), 1280, 720);
    CHECK(meanAbsDiff(v0, v1) > 0.001);
    vs.close();
}

void testSolid()
{
    section("cor solida");
    SolidColorSource src(320, 180);
    ParamValues v;
    v.fillDefaults(SolidColorSource::describe(v));
    v.set("color", param::colorJson({1.0f, 0.0f, 0.0f, 1.0f}));
    src.setParams(v);
    CHECK(src.open({}));
    settle(src, 1);
    Pixels p = readTexture(src.texture(), 320, 180);
    CHECK(p.at(160, 90)[0] == 255 && p.at(160, 90)[1] == 0 && p.at(160, 90)[2] == 0 && p.at(160, 90)[3] == 255);
    v.set("color", param::colorJson({0.0f, 0.0f, 1.0f, 0.5f}));
    src.setParams(v);
    settle(src, 1);
    p = readTexture(src.texture(), 320, 180);
    CHECK(p.at(5, 5)[2] == 255 && std::abs(p.at(5, 5)[3] - 128) <= 1);
    CHECK(src.hasAlpha());
    src.close();
}

#ifdef _WIN32
void testInteropTexture(bool forceCpu)
{
    section(forceCpu ? "D3D11 -> GL: copia por CPU" : "D3D11 -> GL: caminho automatico");
    std::string err;
    auto interop = render::D3D11Interop::shared(&err);
    CHECK(interop != nullptr);
    if (!interop) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    std::printf("  %s\n", interop->describe().c_str());
    auto tex = interop->createTexture(64, 64, forceCpu);
    CHECK(tex != nullptr);
    if (!tex)
        return;
    std::printf("  textura usa: %s\n", tex->usesInterop() ? "interop (GPU)" : "copia por CPU");
    if (forceCpu)
        CHECK(!tex->usesInterop());

    std::vector<unsigned char> data(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            unsigned char* px = &data[(static_cast<size_t>(y) * 64 + static_cast<size_t>(x)) * 4];
            const bool top = y < 32;
            px[0] = top ? 0 : 255;
            px[1] = 0;
            px[2] = top ? 255 : 0;
            px[3] = 7;
        }
    tex->beginD3D();
    interop->context()->UpdateSubresource(tex->d3d(), 0, nullptr, data.data(), 64 * 4, 0);
    tex->endD3D();
    const Pixels p = readTexture(tex->gl(), 64, 64);
    const unsigned char* top = p.at(32, 8);
    const unsigned char* bottom = p.at(32, 56);
    std::printf("  topo=(%d,%d,%d,%d) base=(%d,%d,%d,%d)\n", top[0], top[1], top[2], top[3], bottom[0], bottom[1], bottom[2],
                bottom[3]);
    CHECK(top[0] == 255 && top[1] == 0 && top[2] == 0);
    CHECK(bottom[0] == 0 && bottom[1] == 0 && bottom[2] == 255);
    GLint swizzleA = 0;
    glBindTexture(GL_TEXTURE_2D, tex->gl());
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, &swizzleA);
    glBindTexture(GL_TEXTURE_2D, 0);
    CHECK(swizzleA == static_cast<GLint>(GL_ONE));

    tex->clear(0.0f, 1.0f, 0.0f, 1.0f);
    const Pixels g = readTexture(tex->gl(), 64, 64);
    CHECK(g.at(10, 10)[1] == 255 && g.at(10, 10)[0] == 0);
}

struct CaptureResult
{
    bool frames = false;
    unsigned long long count = 0;
    int w = 0, h = 0;
    long visible = 0;
    std::string backend, path;
};

CaptureResult runCapture(const ParamValues& values, double seconds, unsigned long long wantFrames)
{
    CaptureResult r;
    ScreenCaptureSource src;
    src.setParams(values);
    std::string err;
    if (!src.open({}, &err)) {
        std::printf("  open falhou: %s\n", err.c_str());
        return r;
    }
    const auto t0 = Clock::now();
    while (std::chrono::duration<double>(Clock::now() - t0).count() < seconds && src.frameCount() < wantFrames) {
        src.update(1.0 / 60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    r.count = src.frameCount();
    r.frames = r.count > 0;
    r.w = src.width();
    r.h = src.height();
    r.backend = src.backendName();
    r.path = src.pathName();
    if (src.texture() && r.w > 0 && r.h > 0) {
        const Pixels p = readTexture(src.texture(), r.w, r.h);
        r.visible = countVisible(p, 0, 0, p.w, p.h);
    }
    const SourceStatus st = src.status();
    std::printf("  status: %s\n", st.text.c_str());
    src.close();
    return r;
}

ParamValues captureParams()
{
    ParamValues v;
    v.fillDefaults(ScreenCaptureSource::describe(v));
    v.set("target", monitorTargetJson(1));
    v.set("fps", "60");
    return v;
}

void testScreenCapture()
{
    section("captura de tela: monitor 1 (automatico)");
    const auto monitors = listMonitors();
    CHECK(!monitors.empty());
    if (monitors.empty())
        return;
    std::printf("  monitores: %zu (1 = %dx%d)\n", monitors.size(), monitors[0].w, monitors[0].h);

    ScreenCaptureSource::forceBackend(CaptureBackend::Auto);
    ScreenCaptureSource::forceCpuPath(false);
    CaptureResult r = runCapture(captureParams(), 5.0, 5);
    std::printf("  RESULTADO: %s | caminho: %s | %llu quadros | %dx%d | %ld pixels nao pretos\n", r.backend.c_str(),
                r.path.c_str(), r.count, r.w, r.h, r.visible);
    CHECK(r.frames);
    CHECK(r.w == monitors[0].w && r.h == monitors[0].h);
    CHECK(r.visible > 0);

    section("captura de tela: copia por CPU forcada");
    ScreenCaptureSource::forceCpuPath(true);
    r = runCapture(captureParams(), 5.0, 5);
    std::printf("  RESULTADO: %s | caminho: %s | %llu quadros | %ld pixels nao pretos\n", r.backend.c_str(), r.path.c_str(),
                r.count, r.visible);
    CHECK(r.frames && r.visible > 0);
    CHECK(r.path == "cópia por CPU");
    ScreenCaptureSource::forceCpuPath(false);

    section("captura de tela: DXGI Desktop Duplication (fallback)");
    ScreenCaptureSource::forceBackend(CaptureBackend::DesktopDuplication);
    r = runCapture(captureParams(), 5.0, 1);
    std::printf("  RESULTADO: %s | caminho: %s | %llu quadros | %ld pixels nao pretos\n", r.backend.c_str(), r.path.c_str(),
                r.count, r.visible);
    if (r.frames) {
        CHECK(r.backend == "DXGI Desktop Duplication");
        CHECK(r.visible > 0);
    } else {
        std::printf("  AVISO: Desktop Duplication nao entregou quadros (tela parada ou sem acesso)\n");
        ++g_skipped;
    }
    ScreenCaptureSource::forceBackend(CaptureBackend::Auto);

    section("captura de tela: regiao");
    ParamValues v = captureParams();
    v.set("regionX", 100);
    v.set("regionY", 50);
    v.set("regionW", 400);
    v.set("regionH", 200);
    r = runCapture(v, 5.0, 2);
    std::printf("  regiao: %dx%d\n", r.w, r.h);
    CHECK(r.frames && r.w == 400 && r.h == 200);

    section("captura de tela: janela");
    const auto windows = listWindows();
    std::printf("  janelas na lista: %zu\n", windows.size());
    CHECK(true);
    bool anyOk = false;
    int tried = 0;
    for (const WindowTarget& w : windows) {
        if (tried >= 4)
            break;
        ++tried;
        ParamValues wv = captureParams();
        wv.set("target", windowTargetJson(w));
        const CaptureResult wr = runCapture(wv, 3.0, 2);
        std::printf("  [%s | %s] quadros=%llu %dx%d visiveis=%ld\n", w.exe.c_str(), w.title.substr(0, 40).c_str(), wr.count, wr.w,
                    wr.h, wr.visible);
        if (wr.frames && wr.visible > 0) {
            anyOk = true;
            break;
        }
    }
    if (windows.empty()) {
        std::printf("  AVISO: nenhuma janela para capturar\n");
        ++g_skipped;
    } else {
        CHECK(anyOk);
    }

    section("captura de tela: janela ausente (Aguardando)");
    {
        ScreenCaptureSource src;
        ParamValues wv = captureParams();
        wv.set("target", Json{{"kind", "window"}, {"exe", "naoexiste.exe"}, {"title", "Nada"}});
        src.setParams(wv);
        CHECK(src.open({}));
        for (int i = 0; i < 5; ++i)
            src.update(0.016);
        CHECK(src.status().level == SourceStatus::Level::Warning);
        std::printf("  status: %s\n", src.status().text.c_str());
        src.close();
    }

    section("captura de tela: titulo parecido e lista sem janelas proprias");
    CHECK(titleSimilarity("Documento1 - Word", "Documento1 - Word") == 1.0);
    CHECK(titleSimilarity("Apresentação.pptx - PowerPoint", "Apresentação2.pptx - PowerPoint") >= 0.5);
    CHECK(titleSimilarity("Chrome", "Calculadora") < 0.5);
    for (const WindowTarget& w : windows)
        CHECK(w.pid != static_cast<unsigned long>(GetCurrentProcessId()));
}
#endif

void testWebcam()
{
    section("webcam: formatos (interpretacao do log do dshow)");
    const char* log =
        "[in#0 @ 0000] DirectShow video device options (from video devices)\n"
        "[in#0 @ 0000]  Pin \"Capturar\" (alternative pin name \"Capturar\")\n"
        "[in#0 @ 0000]   pixel_format=yuyv422  min s=1280x720 fps=10 max s=1280x720 fps=30\n"
        "[in#0 @ 0000]   vcodec=mjpeg  min s=1920x1080 fps=5 max s=1920x1080 fps=30\n"
        "[in#0 @ 0000]   pixel_format=yuyv422  min s=640x480 fps=30 max s=640x480 fps=30\n";
    const auto fmts = DeviceCatalog::parseFormats(log);
    CHECK(fmts.size() == 3);
    if (fmts.size() == 3) {
        CHECK(fmts[0].width == 1280 && fmts[0].fpsMax == 30.0 && fmts[0].pixelFormat == "yuyv422");
        CHECK(fmts[1].codec == "mjpeg" && fmts[1].width == 1920);
        const ChosenFormat best = DeviceCatalog::pickBest(fmts);
        CHECK(!best.automatic && best.width == 1920 && best.height == 1080 && best.fps == 30 && best.isCodec);
        const ChosenFormat parsed = DeviceCatalog::parseValue(DeviceCatalog::formatValue(fmts[0], 30.0));
        CHECK(!parsed.automatic && parsed.width == 1280 && parsed.fps == 30 && parsed.format == "yuyv422" && !parsed.isCodec);
    }
    CHECK(DeviceCatalog::parseValue("auto").automatic);

    section("webcam: dispositivos");
    DeviceCatalog& cat = DeviceCatalog::instance();
    cat.ensureListed();
    for (int i = 0; i < 100 && !cat.listed(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const std::vector<std::string> names = cat.deviceNames();
    CHECK(cat.listed() && !cat.listing());
    CHECK(cat.formatsBlocking("Dispositivo Que Nao Existe").empty());
#ifndef _WIN32
    for (const std::string& n : names)
        CHECK(n.find("(/dev/video") != std::string::npos);
#endif
    std::printf("  dispositivos:");
    for (const std::string& n : names)
        std::printf(" [%s]", n.c_str());
    std::printf("\n");

    if (names.empty()) {
        std::printf("  AVISO: nenhuma camera encontrada; teste da webcam pulado\n");
        ++g_skipped;
    }

    bool anyLive = false;
    for (const std::string& name : names) {
        const auto formats = cat.formatsBlocking(name);
        std::printf("  \"%s\": %zu formato(s)\n", name.c_str(), formats.size());

        CaptureDeviceSource src;
        ParamValues v;
        v.fillDefaults(CaptureDeviceSource::describe(v));
        v.set("device", name);
        src.setParams(v);
        CHECK(src.open({}));
        const auto t0 = Clock::now();
        double worstUpdate = 0.0;
        while (std::chrono::duration<double>(Clock::now() - t0).count() < 5.0 && src.frameCount() < 10) {
            const auto u0 = Clock::now();
            src.update(1.0 / 60.0);
            worstUpdate = std::max(worstUpdate, std::chrono::duration<double>(Clock::now() - u0).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
        std::printf("  \"%s\": %llu quadros em %.2f s | pior update() = %.1f ms | status: %s\n", name.c_str(),
                    src.frameCount(), elapsed, worstUpdate * 1000.0, src.status().text.c_str());
        CHECK(worstUpdate < 0.1);
        if (src.frameCount() >= 10) {
            anyLive = true;
            CHECK(src.state() == CaptureDeviceSource::State::Live);
            const Pixels p = readTexture(src.texture(), src.width(), src.height());
            std::printf("  imagem %dx%d, %ld pixels nao pretos\n", p.w, p.h, countVisible(p, 0, 0, p.w, p.h));
        } else {
            std::printf("  AVISO: \"%s\" nao entregou 10 quadros (camera virtual ociosa ou em uso)\n", name.c_str());
            CHECK(src.status().level == SourceStatus::Level::Warning || src.state() == CaptureDeviceSource::State::Connecting);
        }
        src.close();
    }
    if (!names.empty())
        CHECK(anyLive);

    section("webcam: dispositivo inexistente (Sem sinal, sem travar)");
    {
        CaptureDeviceSource src;
        ParamValues v;
        v.fillDefaults(CaptureDeviceSource::describe(v));
        v.set("device", "Dispositivo Que Nao Existe");
        src.setParams(v);
        CHECK(src.open({}));
        double worst = 0.0;
        const auto t0 = Clock::now();
        while (std::chrono::duration<double>(Clock::now() - t0).count() < 5.0 &&
               src.state() != CaptureDeviceSource::State::NoSignal) {
            const auto u0 = Clock::now();
            src.update(0.016);
            worst = std::max(worst, std::chrono::duration<double>(Clock::now() - u0).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        std::printf("  estado final: %s | pior update() = %.1f ms\n", src.status().text.c_str(), worst * 1000.0);
        CHECK(src.state() == CaptureDeviceSource::State::NoSignal);
        CHECK(src.status().text.rfind("Sem sinal", 0) == 0);
        CHECK(worst < 0.1);
        const Pixels p = readTexture(src.texture(), src.width(), src.height());
        CHECK(countVisible(p, 0, 0, p.w, p.h) == 0 && p.at(5, 5)[3] == 255);
        src.close();
    }
}
void testAudio()
{
    using namespace bsl::audio;
    section("audio: dispositivos e captura");
    const auto inputs = AudioAnalyzer::listDevices(AudioSource::Input);
    const auto outputs = AudioAnalyzer::listDevices(AudioSource::Loopback);
    std::printf("  entradas: %zu | saidas (som do PC): %zu\n", inputs.size(), outputs.size());
    for (const AudioDeviceInfo& d : inputs) {
        CHECK(!d.id.empty() && !d.name.empty());
        std::printf("    entrada [%s]\n", d.name.c_str());
    }
    for (const AudioDeviceInfo& d : outputs) {
        CHECK(!d.id.empty() && !d.name.empty());
        std::printf("    saida [%s]\n", d.name.c_str());
    }

    AudioAnalyzer& an = AudioAnalyzer::instance();
    for (const AudioSource source : {AudioSource::Input, AudioSource::Loopback}) {
        const bool isInput = source == AudioSource::Input;
        const auto& devs = isInput ? inputs : outputs;
        AudioConfig cfg;
        cfg.enabled = true;
        cfg.source = source;
        an.setConfig(cfg);
        an.start();
        CHECK(an.running());
        const char* prefix = isInput ? "Entrada: " : "Som do PC: ";
        AudioSnapshot snap;
        for (int i = 0; i < 60; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            snap = an.snapshot();
            if (snap.active && snap.sampleRate > 0)
                break;
        }
        const std::string status = an.statusText();
        std::printf("  %s: status \"%s\" | taxa %d Hz\n", isInput ? "entrada" : "som do PC", status.c_str(), snap.sampleRate);
        CHECK(!status.empty());
        if (!devs.empty() && status.rfind(prefix, 0) == 0) {
            CHECK(snap.active && snap.sampleRate >= 8000);
            CHECK(snap.rms >= 0.0f && snap.rms <= 1.0f);
        } else {
            std::printf("  AVISO: sem dispositivo utilizavel; leitura pulada\n");
            ++g_skipped;
        }
        an.stop();
        CHECK(!an.running());
        CHECK(an.statusText() == "Parado");
    }

    AudioSnapshot fake;
    fake.active = true;
    fake.rms = 0.5f;
    an.setOverride(fake);
    CHECK(an.snapshot().rms == 0.5f);
    an.setOverride(std::nullopt);
}


}

int main()
{
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "livetest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VENDOR)));

    FontCatalog::instance().ensureScanned();

    testText();
    testMarquee();
    testSolid();
#ifdef _WIN32
    testInteropTexture(false);
    testInteropTexture(true);
    testScreenCapture();
#endif
    testWebcam();
    testAudio();

    glfwDestroyWindow(win);
    glfwTerminate();
    std::printf("\n%d verificacoes, %d falha(s), %d aviso(s)/pulo(s)\n", g_checks, g_failures, g_skipped);
    std::printf(g_failures == 0 ? "LIVETEST OK\n" : "LIVETEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
