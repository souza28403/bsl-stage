#include "ai/AiEngine.h"
#include "ai/AiRuntime.h"
#include "fx/EffectRegistry.h"
#include "fx/bsl/BslCommon.h"
#include "render/FboPool.h"
#include "render/ZoneMasks.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
using Json = media::Json;
namespace bl = bsl::fx::bsllight;

namespace {

int g_failures = 0;
int g_checks = 0;

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

void report(bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHOU: %s\n", what.c_str());
    }
}

#define CHECK(cond) report((cond), std::string(#cond) + " (linha " + std::to_string(__LINE__) + ")")
#define CHECK_MSG(cond, msg) report((cond), std::string(msg) + " (linha " + std::to_string(__LINE__) + ")")

using Clock = std::chrono::steady_clock;

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Rgb
{
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;
    bool empty() const { return px.empty(); }
};

Rgb loadRgb(const fs::path& path)
{
    Rgb img;
    int c = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &img.w, &img.h, &c, 3);
    if (!data)
        return {};
    img.px.assign(data, data + static_cast<size_t>(img.w) * static_cast<size_t>(img.h) * 3);
    stbi_image_free(data);
    return img;
}

Rgb resizeTo(const Rgb& s, int w, int h)
{
    Rgb o;
    o.w = w;
    o.h = h;
    o.px.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(s.w) / static_cast<float>(w) - 0.5f;
            const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(s.h) / static_cast<float>(h) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, s.w - 1), y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, s.h - 1);
            const int x1 = std::min(x0 + 1, s.w - 1), y1 = std::min(y0 + 1, s.h - 1);
            const float ax = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f), ay = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
            for (int c = 0; c < 3; ++c) {
                auto at = [&](int xx, int yy) { return static_cast<float>(s.px[(static_cast<size_t>(yy) * static_cast<size_t>(s.w) + static_cast<size_t>(xx)) * 3 + static_cast<size_t>(c)]); };
                const float v = (at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay;
                o.px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3 + static_cast<size_t>(c)] = static_cast<std::uint8_t>(std::lround(v));
            }
        }
    return o;
}

Rgb syntheticScene(int w, int h)
{
    Rgb o;
    o.w = w;
    o.h = h;
    o.px.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool box = x > w * 3 / 10 && x < w * 7 / 10 && y > h * 3 / 10 && y < h * 9 / 10;
            std::uint8_t* p = &o.px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3];
            const int stripe = ((x / 12) + (y / 12)) & 1;
            p[0] = static_cast<std::uint8_t>(box ? 200 : 40 + 30 * y / h + stripe * 14);
            p[1] = static_cast<std::uint8_t>(box ? 150 : 60 + 50 * y / h + stripe * 14);
            p[2] = static_cast<std::uint8_t>(box ? 90 : 110 + 60 * y / h);
        }
    return o;
}

double regionMean(const std::vector<float>& v, int w, int h, double x0, double x1, double y0, double y1)
{
    double sum = 0.0;
    size_t n = 0;
    for (int y = static_cast<int>(y0 * h); y < static_cast<int>(y1 * h); ++y)
        for (int x = static_cast<int>(x0 * w); x < static_cast<int>(x1 * w); ++x) {
            sum += v[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            ++n;
        }
    return n ? sum / static_cast<double>(n) : 0.0;
}

struct Regions
{
    double fx0 = 0.38, fx1 = 0.48, fy0 = 0.30, fy1 = 0.42;
    double bx0 = 0.40, bx1 = 0.60, by0 = 0.02, by1 = 0.12;
};
constexpr Regions kReg;

double pearson(const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min(a.size(), b.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(std::max(saa * sbb, 1e-12));
}

GLuint uploadRgba(const Rgb& img, bool flip)
{
    std::vector<std::uint8_t> rgba(static_cast<size_t>(img.w) * static_cast<size_t>(img.h) * 4);
    for (int y = 0; y < img.h; ++y) {
        const int sy = flip ? img.h - 1 - y : y;
        for (int x = 0; x < img.w; ++x) {
            const std::uint8_t* s = &img.px[(static_cast<size_t>(sy) * static_cast<size_t>(img.w) + static_cast<size_t>(x)) * 3];
            std::uint8_t* d = &rgba[(static_cast<size_t>(y) * static_cast<size_t>(img.w) + static_cast<size_t>(x)) * 4];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

std::vector<float> readFloatTex(GLuint tex, int w, int h)
{
    std::vector<float> v(static_cast<size_t>(w) * static_cast<size_t>(h));
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, v.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return v;
}

std::vector<float> readMaskTex(GLuint tex, int w, int h)
{
    std::vector<std::uint8_t> b(static_cast<size_t>(w) * static_cast<size_t>(h));
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, b.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    std::vector<float> v(b.size());
    for (size_t i = 0; i < b.size(); ++i)
        v[i] = static_cast<float>(b[i]) / 255.0f;
    return v;
}

std::vector<std::uint8_t> readRgba(GLuint tex, int w, int h)
{
    std::vector<std::uint8_t> v(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, v.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return v;
}

double meanDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b)
{
    if (a.size() != b.size() || a.empty())
        return 1e9;
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i)
        s += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    return s / static_cast<double>(a.size());
}

struct Bench
{
    double firstMs = 0, avgMs = 0;
    std::string device;
    bool ok = false;
};

Bench benchModel(ai::ModelKind kind, ai::DevicePref pref, const Rgb& in, std::vector<float>& out, int n = 20)
{
    Bench b;
    std::string err;
    const auto t0 = Clock::now();
    std::unique_ptr<ai::Session> s = ai::Session::create(kind, pref, err);
    if (!s) {
        std::printf("    (sessao nao criada: %s)\n", err.c_str());
        return b;
    }
    const double createMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (!s->run(in.px.data(), out, err)) {
        std::printf("    (run falhou: %s)\n", err.c_str());
        return b;
    }
    b.firstMs = s->lastMs();
    double total = 0;
    for (int i = 0; i < n; ++i) {
        if (!s->run(in.px.data(), out, err)) {
            std::printf("    (run falhou: %s)\n", err.c_str());
            return b;
        }
        total += s->lastMs();
    }
    b.avgMs = total / n;
    b.device = s->deviceName();
    b.ok = true;
    std::printf("    %-7s %-42s criar %.0f ms | 1a inferencia %.1f ms | media %.1f ms\n", ai::modelSpec(kind).key, b.device.c_str(),
                createMs, b.firstMs, b.avgMs);
    return b;
}

fs::path personPath()
{
    for (const char* p : {"testmedia/person.jpg", "../testmedia/person.jpg", "../../testmedia/person.jpg"})
        if (fs::exists(p))
            return p;
    return {};
}

struct Summary
{
    double depthGpu = -1, depthCpu = -1, personGpu = -1, personCpu = -1;
    std::string gpuName;
} g_sum;

void testRuntime()
{
    section("ONNX Runtime: carrega, DirectML e CPU, modelos");
    const ai::RuntimeInfo ri = ai::runtimeInfo();
    CHECK_MSG(ri.dllFound, "onnxruntime.dll ao lado do exe");
    CHECK_MSG(ri.loaded, "runtime carregou: " + ri.error);
    std::printf("  ONNX Runtime %s\n", ri.version.c_str());
    for (const ai::AdapterInfo& a : ai::listAdapters())
        std::printf("  adaptador DXGI: %s (%.1f GB%s%s)\n", a.name.c_str(), static_cast<double>(a.vramBytes) / 1073741824.0,
                    a.software ? ", software" : "", a.usable ? ", D3D12 ok" : "");
    std::printf("  DirectML: %s\n", ri.gpuAvailable ? ri.gpuName.c_str() : ("indisponivel (" + ri.gpuError + ")").c_str());
    g_sum.gpuName = ri.gpuName;
    CHECK_MSG(ri.gpuAvailable, "DirectML disponivel: " + ri.gpuError);
    for (int k = 0; k < ai::kModelKindCount; ++k) {
        const auto kind = static_cast<ai::ModelKind>(k);
        CHECK_MSG(ai::modelPresent(kind), std::string("modelo presente: ") + ai::modelSpec(kind).fileName);
    }
    bool hasNvidia = false;
    for (const ai::AdapterInfo& a : ai::listAdapters())
        hasNvidia = hasNvidia || (a.usable && a.name.find("NVIDIA") != std::string::npos);
    if (hasNvidia && ri.gpuAvailable)
        CHECK_MSG(ri.gpuName.find("NVIDIA") != std::string::npos, "o DirectML usa a NVIDIA: " + ri.gpuName);
}

void testModels(bool bench)
{
    section("modelos: profundidade e pessoa (GPU e CPU)");
    const fs::path pp = personPath();
    Rgb photo = pp.empty() ? Rgb{} : loadRgb(pp);
    const bool havePhoto = !photo.empty();
    if (!havePhoto)
        std::printf("  testmedia/person.jpg ausente: so cena sintetica e o formato da saida (mascara nao validada)\n");
    const int iters = bench ? 60 : 20;

    std::vector<float> depthGpu, depthCpu;
    for (int k = 0; k < ai::kModelKindCount; ++k) {
        const auto kind = static_cast<ai::ModelKind>(k);
        const ai::ModelSpec& spec = ai::modelSpec(kind);
        const Rgb in = havePhoto ? resizeTo(photo, spec.inputW, spec.inputH) : syntheticScene(spec.inputW, spec.inputH);
        std::vector<float> outGpu, outCpu;
        const Bench g = benchModel(kind, ai::DevicePref::Gpu, in, outGpu, iters);
        const Bench c = benchModel(kind, ai::DevicePref::Cpu, in, outCpu, std::max(5, iters / 4));
        CHECK_MSG(g.ok, std::string("modelo ") + spec.key + " roda na GPU (DirectML)");
        CHECK_MSG(c.ok, std::string("modelo ") + spec.key + " roda na CPU");
        if (g.ok)
            CHECK_MSG(g.device.find("DirectML") != std::string::npos, "sessao GPU usa DirectML: " + g.device);
        if (c.ok)
            CHECK(c.device == "CPU");
        (kind == ai::ModelKind::Depth ? g_sum.depthGpu : g_sum.personGpu) = g.avgMs;
        (kind == ai::ModelKind::Depth ? g_sum.depthCpu : g_sum.personCpu) = c.avgMs;

        for (const std::vector<float>* out : {&outGpu, &outCpu}) {
            if (out->empty())
                continue;
            const char* dev = out == &outGpu ? "GPU" : "CPU";
            CHECK_MSG(out->size() == static_cast<size_t>(spec.inputW) * static_cast<size_t>(spec.inputH), "formato da saida (WxH)");
            bool finite = true;
            float lo = 1e30f, hi = -1e30f;
            for (float v : *out) {
                finite = finite && std::isfinite(v);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            CHECK_MSG(finite, "saida finita");
            if (kind == ai::ModelKind::Person)
                CHECK_MSG(lo >= -0.05f && hi <= 1.05f, std::string("mascara em 0..1 (") + dev + ")");
            if (havePhoto) {
                const double face = regionMean(*out, spec.inputW, spec.inputH, kReg.fx0, kReg.fx1, kReg.fy0, kReg.fy1);
                const double bg = regionMean(*out, spec.inputW, spec.inputH, kReg.bx0, kReg.bx1, kReg.by0, kReg.by1);
                std::printf("    %s %s: rosto %.3f | fundo %.3f\n", spec.key, dev, face, bg);
                if (kind == ai::ModelKind::Depth)
                    CHECK_MSG(face > bg + 0.1 * (hi - lo), std::string("profundidade do rosto (perto) > fundo, ") + dev);
                else
                    CHECK_MSG(face > 0.7 && bg < 0.2, std::string("mascara: rosto ~1, fundo ~0, ") + dev);
            } else if (kind == ai::ModelKind::Depth) {
                const double box = regionMean(*out, spec.inputW, spec.inputH, 0.40, 0.60, 0.45, 0.80);
                const double bg = regionMean(*out, spec.inputW, spec.inputH, 0.02, 0.15, 0.02, 0.30);
                std::printf("    depth %s sintetica: caixa %.3f | fundo %.3f\n", dev, box, bg);
                CHECK_MSG(box > bg, std::string("profundidade da caixa em primeiro plano > fundo, ") + dev);
            }
        }
        if (kind == ai::ModelKind::Depth) {
            depthGpu = outGpu;
            depthCpu = outCpu;
        }
    }
    if (!depthGpu.empty() && !depthCpu.empty()) {
        const double r = pearson(depthGpu, depthCpu);
        std::printf("  correlacao profundidade GPU x CPU: %.3f\n", r);
        CHECK_MSG(r > 0.9, "GPU (fp16 DirectML) e CPU concordam");
    }
}

ai::AiFrame waitReady(ai::AiEngine& eng, std::uint64_t src, GLuint tex, int w, int h, bool top, unsigned need, double seconds,
                      std::uint64_t minRuns = 6, ai::ModelKind watch = ai::ModelKind::Depth)
{
    const auto t0 = Clock::now();
    ai::AiFrame f;
    const std::uint64_t base = eng.stats().models[static_cast<int>(watch)].runs;
    std::uint64_t frame = 0;
    while (std::chrono::duration<double>(Clock::now() - t0).count() < seconds) {
        f = eng.acquire(src, frame++, tex, w, h, top, need, false);
        if (f.state == ai::AiState::Unavailable)
            break;
        if (f.state == ai::AiState::Ready && eng.stats().models[static_cast<int>(watch)].runs >= base + minRuns)
            break;
        sleepMs(16);
    }
    return f;
}

void testEngine(const Rgb& photo)
{
    section("AiEngine: PBO, thread propria, texturas, orientacao, cache, taxa");
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.setProfileRate(15);
    eng.setRateOverride(0);
    eng.setDevicePref(ai::DevicePref::Auto);
    CHECK(eng.enabled() && eng.rate() == 15);

    const GLuint top = uploadRgba(photo, false);
    const GLuint bottom = uploadRgba(photo, true);
    const unsigned need = ai::kNeedDepth | ai::kNeedPerson;

    {
        const auto t0 = Clock::now();
        const ai::AiFrame first = eng.acquire(1001, 0, top, photo.w, photo.h, true, need, false);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        std::printf("  1a chamada de acquire(): %.2f ms (estado %d)\n", ms, static_cast<int>(first.state));
        CHECK_MSG(ms < 100.0, "acquire() nao bloqueia (carregar a DLL/modelos fica na thread da IA)");
        CHECK(first.state == ai::AiState::Warming || first.state == ai::AiState::Ready);
    }

    for (int variant = 0; variant < 2; ++variant) {
        const bool topOrigin = variant == 0;
        const std::uint64_t src = topOrigin ? 1001 : 1002;
        const ai::AiFrame f = waitReady(eng, src, topOrigin ? top : bottom, photo.w, photo.h, topOrigin, need, 40.0, 8, ai::ModelKind::Person);
        CHECK_MSG(f.state == ai::AiState::Ready, std::string("IA pronta (") + (topOrigin ? "topOrigin" : "base primeiro") + "): " + f.message);
        if (f.state != ai::AiState::Ready)
            continue;
        CHECK(f.depth != 0 && f.mask != 0);
        const ai::ModelSpec& ds = ai::modelSpec(ai::ModelKind::Depth);
        const ai::ModelSpec& ps = ai::modelSpec(ai::ModelKind::Person);
        CHECK(f.depthW == ds.inputW && f.depthH == ds.inputH && f.maskW == ps.inputW && f.maskH == ps.inputH);
        const std::vector<float> depth = readFloatTex(f.depth, f.depthW, f.depthH);
        const std::vector<float> mask = readMaskTex(f.mask, f.maskW, f.maskH);
        float lo = 1e9f, hi = -1e9f;
        for (float v : depth) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        CHECK_MSG(lo >= -0.01f && hi <= 1.01f && hi - lo > 0.3f, "profundidade R16F normalizada em 0..1");
        const double dFace = regionMean(depth, f.depthW, f.depthH, kReg.fx0, kReg.fx1, kReg.fy0, kReg.fy1);
        const double dBg = regionMean(depth, f.depthW, f.depthH, kReg.bx0, kReg.bx1, kReg.by0, kReg.by1);
        const double mFace = regionMean(mask, f.maskW, f.maskH, kReg.fx0, kReg.fx1, kReg.fy0, kReg.fy1);
        const double mBg = regionMean(mask, f.maskW, f.maskH, kReg.bx0, kReg.bx1, kReg.by0, kReg.by1);
        std::printf("  %s: profundidade rosto %.2f fundo %.2f | mascara rosto %.2f fundo %.2f\n", topOrigin ? "topOrigin " : "base-first", dFace, dBg, mFace, mBg);
        CHECK_MSG(dFace > dBg + 0.2, "profundidade das texturas: rosto > fundo (orientacao certa)");
        CHECK_MSG(mFace > 0.7 && mBg < 0.2, "mascara das texturas: rosto ~1, fundo ~0 (orientacao certa)");
    }

    {
        auto count = [&](bool two, std::uint64_t src) {
            const std::uint64_t before = eng.stats().models[0].runs;
            const auto t0 = Clock::now();
            std::uint64_t frame = 100000;
            while (std::chrono::duration<double>(Clock::now() - t0).count() < 2.0) {
                eng.acquire(src, frame, top, photo.w, photo.h, true, ai::kNeedDepth, false);
                if (two)
                    eng.acquire(src, frame, bottom, photo.w, photo.h, false, ai::kNeedDepth, false);
                ++frame;
                sleepMs(8);
            }
            return eng.stats().models[0].runs - before;
        };
        const std::uint64_t one = count(false, 2001);
        const std::uint64_t two = count(true, 2002);
        std::printf("  inferencias em 2 s: 1 efeito = %llu, 2 efeitos na mesma fonte = %llu\n", static_cast<unsigned long long>(one),
                    static_cast<unsigned long long>(two));
        CHECK_MSG(one >= 5, "a inferencia roda");
        CHECK_MSG(two <= one + one / 3 + 2, "2 efeitos na mesma fonte usam a mesma inferencia");
    }

    {
        eng.setRateOverride(5);
        CHECK(eng.rate() == 5);
        const std::uint64_t before = eng.stats().models[0].runs;
        const auto t0 = Clock::now();
        std::uint64_t frame = 200000;
        while (std::chrono::duration<double>(Clock::now() - t0).count() < 2.5) {
            eng.acquire(3001, frame++, top, photo.w, photo.h, true, ai::kNeedDepth, false);
            sleepMs(8);
        }
        const std::uint64_t runs = eng.stats().models[0].runs - before;
        std::printf("  taxa 5 fps: %llu inferencias em 2,5 s\n", static_cast<unsigned long long>(runs));
        CHECK_MSG(runs >= 6 && runs <= 15, "a taxa de inferencia configurada e respeitada");
        eng.setRateOverride(0);
    }

    const ai::AiStats st = eng.stats();
    std::printf("  stats: %d fonte(s), %.0f inferencias/s, depth %.1f ms (%s), person %.1f ms (%s)\n", st.activeSources, static_cast<double>(st.inferencesPerSec),
                st.models[0].avgMs, st.models[0].device.c_str(), st.models[1].avgMs, st.models[1].device.c_str());
    CHECK(st.runtimeLoaded && st.models[0].loaded && st.models[1].loaded);
    CHECK_MSG(st.models[0].onGpu && st.models[1].onGpu, "modelos na GPU no modo Auto");

    glDeleteTextures(1, &top);
    glDeleteTextures(1, &bottom);
}

void testCpuFallbackEngine(const Rgb& photo)
{
    section("AiEngine: preferencia de CPU");
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.setDevicePref(ai::DevicePref::Cpu);
    const GLuint tex = uploadRgba(photo, false);
    const ai::AiFrame f = waitReady(eng, 4001, tex, photo.w, photo.h, true, ai::kNeedDepth, 60.0, 3);
    CHECK_MSG(f.state == ai::AiState::Ready, "IA pronta na CPU: " + f.message);
    const ai::AiStats st = eng.stats();
    CHECK_MSG(!st.models[0].onGpu && st.models[0].device == "CPU", "profundidade na CPU: " + st.models[0].device);
    std::printf("  CPU: profundidade %.1f ms\n", st.models[0].avgMs);
    eng.setDevicePref(ai::DevicePref::Auto);
    glDeleteTextures(1, &tex);
}

void testEco(const Rgb& photo)
{
    section("perfil Eco: a IA nao roda e os efeitos passam a imagem");
    ai::AiEngine& eng = ai::AiEngine::instance();
    const GLuint tex = uploadRgba(photo, false);
    const std::uint64_t runs0 = eng.stats().models[0].runs + eng.stats().models[1].runs;

    eng.setProfileRate(0);
    CHECK(!eng.enabled() && eng.rate() == 0);
    ai::AiFrame f;
    for (int i = 0; i < 40; ++i) {
        f = eng.acquire(5001, static_cast<std::uint64_t>(i), tex, photo.w, photo.h, true, ai::kNeedDepth | ai::kNeedPerson, false);
        sleepMs(10);
    }
    CHECK(f.state == ai::AiState::Off && f.depth == 0 && f.mask == 0);
    CHECK(ai::aiStatusText(f) == "IA desligada no perfil Eco");
    eng.setProfileRate(15);
    f = eng.acquire(5002, 0, tex, photo.w, photo.h, true, ai::kNeedDepth, true);
    CHECK(f.state == ai::AiState::Off);
    eng.setProfileRate(0);
    sleepMs(300);
    const std::uint64_t runs1 = eng.stats().models[0].runs + eng.stats().models[1].runs;
    CHECK_MSG(runs1 == runs0, "nenhuma inferencia nova no Eco");

    FboPool pool;
    for (const char* id : {bl::kRelightId, bl::kBodyGlitchId, bl::kAuraId, bl::kZSliceId}) {
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(id);
        CHECK_MSG(e != nullptr, id);
        if (!e)
            continue;
        e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
        Fbo out(photo.w, photo.h);
        fx::EffectContext ctx;
        ctx.pool = &pool;
        ctx.eco = true;
        ctx.sourceId = 5003;
        ctx.dt = 1.0 / 60.0;
        ctx.chain = fx::ChainKind::Layer;
        ctx.layerIndex = 0;
        ctx.topOrigin = false;
        e->process(ctx, tex, photo.w, photo.h, out);
        glFinish();
        const double d = meanDiff(readRgba(out.texture(), photo.w, photo.h), readRgba(tex, photo.w, photo.h));
        CHECK_MSG(d < 0.01, std::string(id) + ": no Eco a imagem passa direto (dif " + std::to_string(d) + ")");
        CHECK_MSG(!ZoneMasks::instance().available(0), std::string(id) + ": no Eco nenhuma mascara de zonas e publicada");
        CHECK_MSG(e->status().text == "IA desligada no perfil Eco", std::string(id) + ": status \"" + e->status().text + "\"");
    }
    const std::uint64_t runs2 = eng.stats().models[0].runs + eng.stats().models[1].runs;
    CHECK_MSG(runs2 == runs0, "os efeitos no Eco nao disparam inferencia");
    eng.setProfileRate(15);
    glDeleteTextures(1, &tex);
}

void testMissingModel(const Rgb& photo)
{
    section("modelo ausente: sem crash, IA indisponivel");
    ai::AiEngine& eng = ai::AiEngine::instance();
    const fs::path empty = fs::temp_directory_path() / "bslstage_aitest_semmodelos";
    fs::create_directories(empty);
    ai::setModelsDirOverride(empty);
    eng.refreshModelPresence();

    CHECK(!ai::modelPresent(ai::ModelKind::Depth) && !ai::modelPresent(ai::ModelKind::Person));
    std::string err;
    std::unique_ptr<ai::Session> s = ai::Session::create(ai::ModelKind::Depth, ai::DevicePref::Auto, err);
    CHECK(s == nullptr);
    CHECK_MSG(err.find("ausente") != std::string::npos, "erro explica: " + err);
    s = ai::Session::create(ai::ModelKind::Person, ai::DevicePref::Cpu, err);
    CHECK(s == nullptr);

    const GLuint tex = uploadRgba(photo, false);
    const ai::AiFrame f = eng.acquire(6001, 0, tex, photo.w, photo.h, true, ai::kNeedDepth | ai::kNeedPerson, false);
    CHECK(f.state == ai::AiState::Unavailable);
    CHECK_MSG(ai::aiStatusText(f).find("modelo ausente") != std::string::npos, "status: " + ai::aiStatusText(f));
    CHECK(f.depth == 0 && f.mask == 0);
    const ai::AiStats st = eng.stats();
    CHECK(!st.models[0].present && !st.models[1].present);

    FboPool pool;
    std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kRelightId);
    e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
    Fbo out(photo.w, photo.h);
    fx::EffectContext ctx;
    ctx.pool = &pool;
    ctx.sourceId = 6002;
    e->process(ctx, tex, photo.w, photo.h, out);
    glFinish();
    CHECK_MSG(meanDiff(readRgba(out.texture(), photo.w, photo.h), readRgba(tex, photo.w, photo.h)) < 0.01, "sem modelo: a imagem passa direto");
    CHECK_MSG(e->status().level == media::SourceStatus::Level::Warning && e->status().text.find("modelo ausente") != std::string::npos,
              "status do efeito: " + e->status().text);

    ai::setModelsDirOverride({}, false);
    eng.refreshModelPresence();
    CHECK(ai::modelPresent(ai::ModelKind::Depth));
    std::error_code ec;
    fs::remove_all(empty, ec);
    glDeleteTextures(1, &tex);
}

void testSelfTestButton()
{
    section("botao Testar");
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.requestSelfTest();
    const auto t0 = Clock::now();
    ai::AiStats st;
    do {
        sleepMs(100);
        st = eng.stats();
    } while (!st.testDone && std::chrono::duration<double>(Clock::now() - t0).count() < 60.0);
    std::printf("%s", st.testReport.c_str());
    CHECK(st.testDone);
    CHECK_MSG(st.testReport.find("FALHOU") == std::string::npos && st.testReport.find("OK") != std::string::npos, "relatorio sem falhas");
}

void testEffectsWithRealAi(const Rgb& photo)
{
    section("efeitos com a IA de verdade (Relight e Glitch na foto)");
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.setProfileRate(15);
    eng.setRateOverride(0);
    eng.setDevicePref(ai::DevicePref::Auto);
    const GLuint tex = uploadRgba(photo, false);
    FboPool pool;
    auto runUntilReady = [&](const char* id, const std::vector<std::pair<std::string, Json>>& values, std::vector<std::uint8_t>& result) {
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(id);
        media::ParamValues v;
        for (const auto& [k, val] : values)
            v.set(k, val);
        e->setParams(media::ParamSet(e->paramDescs(), v));
        Fbo out(photo.w, photo.h);
        const auto t0 = Clock::now();
        std::uint64_t frame = 0;
        const std::uint64_t base = eng.stats().models[0].runs;
        bool ready = false;
        while (std::chrono::duration<double>(Clock::now() - t0).count() < 40.0) {
            fx::EffectContext ctx;
            ctx.pool = &pool;
            ctx.sourceId = 7001;
            ctx.frameIndex = frame++;
            ctx.time = static_cast<double>(frame) / 60.0;
            ctx.dt = 1.0 / 60.0;
            e->process(ctx, tex, photo.w, photo.h, out);
            if (e->status().text.empty() && eng.stats().models[0].runs >= base + 6 && eng.stats().models[1].runs >= 6) {
                ready = true;
                break;
            }
            sleepMs(16);
        }
        glFinish();
        result = readRgba(out.texture(), photo.w, photo.h);
        return ready;
    };

    std::vector<std::uint8_t> depthView;
    const std::vector<std::uint8_t> original = readRgba(tex, photo.w, photo.h);
    const bool ok1 = runUntilReady(bl::kRelightId, {{"debug", Json("depth")}}, depthView);
    CHECK_MSG(ok1, "Relight: IA pronta");
    auto lum = [&](const std::vector<std::uint8_t>& img, double x0, double x1, double y0, double y1) {
        double s = 0;
        size_t n = 0;
        for (int y = static_cast<int>(y0 * photo.h); y < static_cast<int>(y1 * photo.h); ++y)
            for (int x = static_cast<int>(x0 * photo.w); x < static_cast<int>(x1 * photo.w); ++x) {
                s += img[(static_cast<size_t>(y) * static_cast<size_t>(photo.w) + static_cast<size_t>(x)) * 4];
                ++n;
            }
        return s / static_cast<double>(n);
    };
    const double face = lum(depthView, kReg.fx0, kReg.fx1, kReg.fy0, kReg.fy1);
    const double bg = lum(depthView, kReg.bx0, kReg.bx1, kReg.by0, kReg.by1);
    std::printf("  Relight (ver profundidade): rosto %.0f | fundo %.0f\n", face, bg);
    CHECK_MSG(face > bg + 40.0, "Relight, modo profundidade: rosto mais claro que o fundo");

    std::vector<std::uint8_t> lit;
    CHECK(runUntilReady(bl::kRelightId, {}, lit));
    CHECK_MSG(meanDiff(lit, original) > 2.0, "Relight muda a imagem");

    std::vector<std::uint8_t> glitch;
    CHECK(runUntilReady(bl::kBodyGlitchId, {{"mode", Json("slicer")}, {"near", Json(55.0)}, {"sliceShift", Json(20.0)}, {"tint", Json(0.0)}}, glitch));
    double outsideBg = 0;
    {
        size_t n = 0;
        for (int y = 0; y < photo.h / 10; ++y)
            for (int x = photo.w * 4 / 10; x < photo.w * 6 / 10; ++x)
                for (int c = 0; c < 3; ++c) {
                    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(photo.w) + static_cast<size_t>(x)) * 4 + static_cast<size_t>(c);
                    outsideBg += std::abs(static_cast<int>(glitch[i]) - static_cast<int>(original[i]));
                    ++n;
                }
        outsideBg /= static_cast<double>(n);
    }
    std::printf("  Glitch (slicer): fundo (fora da regiao) dif %.2f | imagem toda dif %.2f\n", outsideBg, meanDiff(glitch, original));
    CHECK_MSG(outsideBg < 1.0, "Glitch: o fundo (fora da regiao) fica igual a entrada");
    CHECK_MSG(meanDiff(glitch, original) > 0.5, "Glitch: a regiao (pessoa perto) muda");
    glDeleteTextures(1, &tex);
}


void testAuraAndZSliceRealAi(const Rgb& photo)
{
    section("Aura e Z-Slice com a IA de verdade (foto de pessoa)");
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.setProfileRate(15);
    eng.setRateOverride(0);
    eng.setDevicePref(ai::DevicePref::Auto);
    FboPool pool;

    {
        const GLuint tex = uploadRgba(photo, true);
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kZSliceId);
        media::ParamValues v;
        v.set("feather", Json(8.0));
        v.set("t1", Json(30.0));
        v.set("t2", Json(10.0));
        e->setParams(media::ParamSet(e->paramDescs(), v));
        Fbo out(photo.w, photo.h);
        const auto t0 = Clock::now();
        std::uint64_t frame = 0;
        const std::uint64_t base = eng.stats().models[0].runs;
        bool ready = false;
        while (std::chrono::duration<double>(Clock::now() - t0).count() < 40.0) {
            ZoneMasks::instance().beginFrame();
            fx::EffectContext ctx;
            ctx.pool = &pool;
            ctx.sourceId = 7101;
            ctx.frameIndex = frame++;
            ctx.dt = 1.0 / 60.0;
            ctx.topOrigin = false;
            ctx.chain = fx::ChainKind::Layer;
            ctx.layerIndex = 0;
            e->process(ctx, tex, photo.w, photo.h, out);
            if (e->status().text.empty() && eng.stats().models[0].runs >= base + 6 && ZoneMasks::instance().available(0)) {
                ready = true;
                break;
            }
            sleepMs(16);
        }
        CHECK_MSG(ready, "Z-Slice: IA pronta e mascara publicada");
        ZoneMask zm;
        if (ready && ZoneMasks::instance().find(0, zm)) {
            const std::vector<std::uint8_t> m = readRgba(zm.texture, zm.width, zm.height);
            auto zoneMean = [&](int channel, double x0, double x1, double y0, double y1) {
                double s = 0;
                size_t n = 0;
                for (int y = static_cast<int>((1.0 - y1) * zm.height); y < static_cast<int>((1.0 - y0) * zm.height); ++y)
                    for (int x = static_cast<int>(x0 * zm.width); x < static_cast<int>(x1 * zm.width); ++x) {
                        s += m[(static_cast<size_t>(y) * static_cast<size_t>(zm.width) + static_cast<size_t>(x)) * 4 + static_cast<size_t>(channel)];
                        ++n;
                    }
                return n ? s / static_cast<double>(n) : 0.0;
            };
            const double faceFront = zoneMean(0, kReg.fx0, kReg.fx1, kReg.fy0, kReg.fy1);
            const double bgFront = zoneMean(0, kReg.bx0, kReg.bx1, kReg.by0, kReg.by1);
            const double bgBack = zoneMean(2, kReg.bx0, kReg.bx1, kReg.by0, kReg.by1);
            std::printf("  Z-Slice: peso da Frente no rosto %.0f | no fundo %.0f ; peso do Fundo no fundo %.0f\n", faceFront, bgFront, bgBack);
            CHECK_MSG(faceFront > bgFront + 80.0, "Z-Slice: o rosto esta mais na Frente que o fundo");
            CHECK_MSG(bgBack > 100.0, "Z-Slice: o fundo do estudio cai na zona Fundo");
        }
        glDeleteTextures(1, &tex);
    }

    {
        Rgb shifted = photo;
        const GLuint tex = uploadRgba(photo, false);
        auto shiftTo = [&](int dx) {
            for (int y = 0; y < photo.h; ++y)
                for (int x = 0; x < photo.w; ++x) {
                    const int sx = std::clamp(x - dx, 0, photo.w - 1);
                    std::memcpy(&shifted.px[(static_cast<size_t>(y) * static_cast<size_t>(photo.w) + static_cast<size_t>(x)) * 3],
                                &photo.px[(static_cast<size_t>(y) * static_cast<size_t>(photo.w) + static_cast<size_t>(sx)) * 3], 3);
                }
            std::vector<std::uint8_t> rgba(static_cast<size_t>(photo.w) * static_cast<size_t>(photo.h) * 4, 255);
            for (size_t i = 0; i < static_cast<size_t>(photo.w) * static_cast<size_t>(photo.h); ++i)
                std::memcpy(&rgba[i * 4], &shifted.px[i * 3], 3);
            glBindTexture(GL_TEXTURE_2D, tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, photo.w, photo.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        };
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kAuraId);
        media::ParamValues v;
        v.set("debug", Json("density"));
        e->setParams(media::ParamSet(e->paramDescs(), v));
        Fbo out(photo.w, photo.h);
        const auto t0 = Clock::now();
        std::uint64_t frame = 0;
        const std::uint64_t base = eng.stats().models[1].runs;
        double mean = 0.0;
        while (std::chrono::duration<double>(Clock::now() - t0).count() < 40.0) {
            shiftTo(static_cast<int>(std::lround(std::sin(static_cast<double>(frame) * 0.1) * photo.w * 0.03)));
            fx::EffectContext ctx;
            ctx.pool = &pool;
            ctx.sourceId = 7102;
            ctx.frameIndex = frame++;
            ctx.dt = 1.0 / 60.0;
            e->process(ctx, tex, photo.w, photo.h, out);
            if (frame % 20 == 0 && e->status().text.empty() && eng.stats().models[1].runs >= base + 12) {
                glFinish();
                const std::vector<std::uint8_t> img = readRgba(out.texture(), photo.w, photo.h);
                double s = 0;
                for (size_t i = 0; i < img.size(); i += 4)
                    s += img[i];
                mean = s / static_cast<double>(img.size() / 4);
                if (mean > 2.0)
                    break;
            }
            sleepMs(16);
        }
        std::printf("  Aura: densidade media %.1f (0..255) depois de %llu quadros, mascara real da IA\n", mean, static_cast<unsigned long long>(frame));
        CHECK_MSG(mean > 2.0, "Aura: a mascara real da IA em movimento gera densidade");
        CHECK_MSG(e->status().text.empty(), "Aura: status vazio com a IA pronta (" + e->status().text + ")");
        glDeleteTextures(1, &tex);
    }
}

}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool bench = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--bench")
            bench = true;

    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "aitest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    testRuntime();
    testModels(bench);

    const fs::path pp = personPath();
    const Rgb photo = pp.empty() ? syntheticScene(640, 360) : loadRgb(pp);
    if (pp.empty()) {
        std::printf("[AiEngine com GL] sem testmedia/person.jpg: usando a cena sintetica; os testes de semantica da IA no motor foram pulados\n");
    } else {
        testEngine(photo);
        testCpuFallbackEngine(photo);
        testEco(photo);
        testEffectsWithRealAi(photo);
        testAuraAndZSliceRealAi(photo);
    }
    testMissingModel(photo);
    testSelfTestButton();

    ai::AiEngine::instance().shutdown();
    GLenum err;
    int glErrors = 0;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::printf("  erro de GL pendente: 0x%04X\n", err);
        ++glErrors;
    }
    report(glErrors == 0, "nenhum erro de GL");

    std::printf("\nRESUMO ms por inferencia (media): profundidade GPU %.1f | CPU %.1f ; pessoa GPU %.1f | CPU %.1f (GPU: %s)\n", g_sum.depthGpu,
                g_sum.depthCpu, g_sum.personGpu, g_sum.personCpu, g_sum.gpuName.c_str());
    std::printf("%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "AITEST OK\n" : "AITEST FALHOU\n");
    const int rc = g_failures == 0 ? 0 : 1;
    glfwDestroyWindow(win);
    glfwTerminate();
    return rc;
}
