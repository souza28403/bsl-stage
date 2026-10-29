// bslstage_bsltest: plugins BSL Light nativos (efeitos e fontes "bsl:") e modulo de audio, com janela oculta (GL 3.3 core).
// Uso: bslstage_bsltest                       testes automaticos
//      bslstage_bsltest --compare <pasta> [--src <ffgl-master/source/plugins>]
//                                             compara com os originais (somente leitura): LEDMapperPro e SliceRefiner2 pelo host FFGL
//                                             (Extra Effects do Resolume) e o shader original, lido do codigo-fonte, dos demais;
//                                             grava PNGs lado a lado (original | nativo) em <pasta>
//      bslstage_bsltest --dump <pasta>        grava imagens dos plugins nativos para conferir a olho
//      bslstage_bsltest --compare-motion <pasta>  o WebMotion original sozinho (no Resolume ele falha junto do WebBackgrounds)
//      bslstage_bsltest --only-b              so a etapa B: Slice Refiner 3, PPT Manager, WebBackgrounds e WebMotion
//
// Cobre: DSP de audio (FFT, graves/agudos, espectro, batida, silencio), snapshot sem dispositivo, cada plugin instancia e
// processa 60 quadros com saida nao vazia, os parametros principais mudam a saida, os efeitos de audio reagem a um
// AudioSnapshot sintetico (graves altos), orientacao (topOrigin) coerente, SliceRefiner2 com os slices de um OutputSetup
// de teste e com XML do Resolume, exportacao do XML do LED Mapper e o registro (efeitos, geradores, projeto).

#include "ai/AiEngine.h"
#include "audio/AudioAnalyzer.h"
#include "audio/AudioDsp.h"
#include "core/Clip.h"
#include "core/Composition.h"
#include "core/Generators.h"
#include "ffgl/FfglCatalog.h"
#include "ffgl/FfglInstance.h"
#include "fx/EffectChain.h"
#include "fx/EffectRegistry.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/BslSources.h"
#include "fx/bsl/SliceFeed.h"
#include "fx/bsl/SliceState.h"
#include "fx/bsl/WebStudio.h"
#include "media/PdfSource.h"
#include "media/RemoteSource.h"
#include "media/WebSource.h"
#include "media/WorkerHost.h"
#include "net/MiniHttp.h"
#include "output/OutputRenderer.h"
#include "output/OutputSetup.h"
#include "render/Compositor.h"
#include "render/FboPool.h"
#include "render/ZoneMasks.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stb_image_write.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>
#include <set>
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

// ---------------------------------------------------------------------------------------------------- imagens

struct Img
{
    int w = 0, h = 0;
    std::vector<unsigned char> px; // RGBA, linha 0 da textura primeiro

    const unsigned char* at(int x, int y) const { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4]; }
};

Img readTexture(GLuint tex, int w, int h)
{
    Img img;
    img.w = w;
    img.h = h;
    img.px.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return img;
}

Img flipV(const Img& a)
{
    Img o = a;
    for (int y = 0; y < a.h; ++y)
        std::memcpy(&o.px[static_cast<size_t>(y) * static_cast<size_t>(a.w) * 4],
                    &a.px[static_cast<size_t>(a.h - 1 - y) * static_cast<size_t>(a.w) * 4], static_cast<size_t>(a.w) * 4);
    return o;
}

// Diferenca media absoluta por canal (0..255)
double meanDiff(const Img& a, const Img& b)
{
    if (a.w != b.w || a.h != b.h || a.px.empty())
        return 1.0e9;
    double sum = 0.0;
    for (size_t i = 0; i < a.px.size(); ++i)
        sum += std::abs(static_cast<int>(a.px[i]) - static_cast<int>(b.px[i]));
    return sum / static_cast<double>(a.px.size());
}

// Algum pixel com alpha e cor (a saida nao esta vazia nem toda preta)
bool nonEmpty(const Img& a)
{
    size_t lit = 0;
    for (size_t i = 0; i + 3 < a.px.size(); i += 4)
        if (a.px[i + 3] > 8 && (a.px[i] > 8 || a.px[i + 1] > 8 || a.px[i + 2] > 8))
            ++lit;
    return lit > a.px.size() / 4 / 2000; // pelo menos 0,05% dos pixels
}

GLuint makeTexture(int w, int h, const std::function<void(int, int, unsigned char*)>& f)
{
    std::vector<unsigned char> data(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f(x, y, &data[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4]);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

constexpr int kW = 640;
constexpr int kH = 360;

// Camera de teste: gradiente colorido, opaca; 'cutout' = so um disco central e opaco (recorte com alpha)
void cameraPixel(bool cutout, int x, int y, int tw, int th, unsigned char* p)
{
    const float dx = static_cast<float>(x) - static_cast<float>(tw) * 0.5f, dy = static_cast<float>(y) - static_cast<float>(th) * 0.55f;
    const bool inside = std::sqrt(dx * dx + dy * dy) < static_cast<float>(th) * 0.28f;
    p[0] = static_cast<unsigned char>(40 + 200 * x / tw);
    p[1] = static_cast<unsigned char>(30 + 200 * y / th);
    p[2] = static_cast<unsigned char>(inside ? 230 : 90);
    p[3] = cutout ? (inside ? 255 : 0) : 255;
}

GLuint makeCamera(bool cutout, bool topFirst = true, int tw = kW, int th = kH)
{
    return makeTexture(tw, th, [&](int x, int y0, unsigned char* p) { cameraPixel(cutout, x, topFirst ? y0 : th - 1 - y0, tw, th, p); });
}

// A mesma camera em memoria (linha 0 = topo)
Img makeCameraImg(bool cutout, bool /*reservado*/, int w, int h)
{
    Img img;
    img.w = w;
    img.h = h;
    img.px.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            cameraPixel(cutout, x, y, w, h, &img.px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4]);
    return img;
}

// ---------------------------------------------------------------------------------------------------- execucao

audio::AudioSnapshot loudBass()
{
    audio::AudioSnapshot s;
    s.active = true;
    s.sampleRate = 48000;
    s.bass = 1.5f;
    s.mid = 0.5f;
    s.high = 0.1f;
    s.rms = 0.5f;
    s.peak = 0.8f;
    return s;
}

audio::AudioSnapshot loudHigh()
{
    audio::AudioSnapshot s = loudBass();
    s.bass = 0.0f;
    s.high = 1.5f;
    return s;
}

struct RunOptions
{
    int frames = 60;
    bool topOrigin = true;
    audio::AudioSnapshot audio{};
    int w = kW, h = kH;
    int canvasW = 1920, canvasH = 1080;
    double timeBase = 0.0; // ctx.time do primeiro quadro (segundos)
    GLuint altInput = 0;   // se != 0, os quadros impares usam esta entrada (imagem em movimento)
};

// Cria o efeito, aplica os valores e processa 'frames' quadros; devolve o ultimo
Img runEffect(FboPool& pool, const std::string& typeId, const std::vector<std::pair<std::string, Json>>& values,
              GLuint input, const RunOptions& opt, std::string* statusOut = nullptr)
{
    std::unique_ptr<fx::IEffect> effect = fx::EffectRegistry::instance().create(typeId);
    if (!effect) {
        report(false, "efeito nao criado: " + typeId);
        return {};
    }
    media::ParamValues v;
    for (const auto& [k, val] : values)
        v.set(k, val);
    effect->setParams(media::ParamSet(effect->paramDescs(), v));
    Fbo out(opt.w, opt.h);
    for (int f = 0; f < opt.frames; ++f) {
        fx::EffectContext ctx;
        ctx.time = opt.timeBase + f / 60.0;
        ctx.dt = 1.0 / 60.0;
        ctx.frameIndex = static_cast<std::uint64_t>(f);
        ctx.bpm = 128.0f;
        ctx.beat = f / 60.0 * 128.0 / 60.0;
        ctx.canvasWidth = opt.canvasW;
        ctx.canvasHeight = opt.canvasH;
        ctx.topOrigin = opt.topOrigin;
        ctx.pool = &pool;
        ctx.audio = opt.audio;
        effect->process(ctx, (opt.altInput != 0 && (f & 1)) ? opt.altInput : input, opt.w, opt.h, out);
    }
    if (statusOut)
        *statusOut = effect->status().text;
    glFinish();
    return readTexture(out.texture(), opt.w, opt.h);
}

// Fonte (gerador): cria, abre, atualiza 'frames' quadros; devolve a textura lida
Img runSource(const std::string& id, const std::vector<std::pair<std::string, Json>>& values, int frames, int w = 480, int h = 270,
              std::string* statusOut = nullptr)
{
    const GeneratorInfo* info = findGenerator(id);
    if (!info) {
        report(false, "gerador nao registrado: " + id);
        return {};
    }
    std::unique_ptr<media::ISource> src = info->create(w, h);
    media::ParamValues v;
    v.fillDefaults(info->describe(v));
    for (const auto& [k, val] : values)
        v.set(k, val);
    src->setParams(v);
    std::string error;
    if (!src->open({}, &error)) {
        report(false, "fonte nao abriu: " + id + " " + error);
        return {};
    }
    for (int f = 0; f < frames; ++f)
        src->update(1.0 / 60.0);
    if (statusOut)
        *statusOut = src->status().text;
    glFinish();
    return readTexture(src->texture(), w, h);
}

using Values = std::vector<std::pair<std::string, Json>>;

// ---------------------------------------------------------------------------------------------------- audio

std::vector<float> sine(double hz, double seconds, double amp = 0.5, int sr = 48000)
{
    std::vector<float> v(static_cast<size_t>(seconds * sr));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    return v;
}

void testAudio()
{
    section("audio: FFT, bandas, espectro, batida, silencio");
    using audio::AudioDsp;
    // FFT: um seno de 1 bin
    {
        std::vector<std::complex<float>> a(512);
        for (int i = 0; i < 512; ++i)
            a[static_cast<size_t>(i)] = std::cos(2.0f * std::numbers::pi_v<float> * 8.0f * static_cast<float>(i) / 512.0f);
        audio::fftRadix2(a.data(), 512);
        CHECK(std::abs(a[8]) > 200.0f && std::abs(a[9]) < 1.0f && std::abs(a[100]) < 1.0f);
    }
    // Graves x agudos
    {
        AudioDsp dsp;
        const std::vector<float> low = sine(200.0, 1.0);
        dsp.feed(low.data(), low.size());
        const audio::AudioSnapshot s = dsp.snapshot(true);
        std::printf("  200 Hz: bass %.3f mid %.3f high %.3f rms %.3f peak %.3f\n", s.bass, s.mid, s.high, s.rms, s.peak);
        CHECK(s.bass > 0.2f);
        CHECK(s.bass > s.high * 10.0f);
        CHECK(s.rms > 0.2f && s.peak > 0.4f);
    }
    {
        AudioDsp dsp;
        const std::vector<float> hi = sine(6000.0, 1.0);
        dsp.feed(hi.data(), hi.size());
        const audio::AudioSnapshot s = dsp.snapshot(true);
        std::printf("  6 kHz: bass %.3f mid %.3f high %.3f\n", s.bass, s.mid, s.high);
        CHECK(s.high > 0.2f);
        CHECK(s.high > s.bass * 10.0f);
    }
    // Outra taxa de amostragem: as faixas seguem a frequencia
    {
        AudioDsp dsp;
        dsp.setSampleRate(44100);
        const std::vector<float> low = sine(200.0, 1.0, 0.5, 44100);
        dsp.feed(low.data(), low.size());
        const audio::AudioSnapshot s = dsp.snapshot(true);
        CHECK(s.bass > 0.2f && s.bass > s.high * 10.0f);
    }
    // Espectro: 1 kHz cai na banda esperada (log entre 40 Hz e 16 kHz)
    {
        AudioDsp dsp;
        const std::vector<float> tone = sine(1000.0, 1.0, 0.5);
        dsp.feed(tone.data(), tone.size());
        const audio::AudioSnapshot s = dsp.snapshot(true);
        int best = 0;
        for (int i = 0; i < audio::kSpectrumBands; ++i)
            if (s.spectrum[static_cast<size_t>(i)] > s.spectrum[static_cast<size_t>(best)])
                best = i;
        const double expected = 32.0 * std::log(1000.0 / 40.0) / std::log(16000.0 / 40.0);
        std::printf("  espectro 1 kHz: banda %d (esperada ~%.1f), valor %.2f\n", best, expected, static_cast<double>(s.spectrum[static_cast<size_t>(best)]));
        CHECK(std::abs(best - expected) <= 2.0);
        CHECK(s.spectrum[static_cast<size_t>(best)] > 0.5f);
    }
    // Silencio: as bandas caem a zero em vez de congelar
    {
        AudioDsp dsp;
        const std::vector<float> low = sine(100.0, 0.5);
        dsp.feed(low.data(), low.size());
        CHECK(dsp.snapshot(true).bass > 0.1f);
        dsp.feedSilence(0.5);
        const audio::AudioSnapshot s = dsp.snapshot(true);
        CHECK(s.bass < 0.01f && s.high < 0.01f && s.rms < 0.05f);
    }
    // Batida: rajadas de grave a cada 0,5 s
    {
        AudioDsp dsp;
        for (int beat = 0; beat < 5; ++beat) {
            const std::vector<float> burst = sine(70.0, 0.12, 0.8);
            dsp.feed(burst.data(), burst.size());
            dsp.feedSilence(0.38);
        }
        const audio::AudioSnapshot s = dsp.snapshot(true);
        std::printf("  batidas detectadas em 5 rajadas: %u\n", s.beatCount);
        CHECK(s.beatCount >= 4 && s.beatCount <= 8);
    }
    // Ganho geral e noise gate da configuracao
    {
        AudioDsp quiet, loud;
        audio::AudioConfig c;
        c.gain = 0.2f;
        quiet.setConfig(c);
        const std::vector<float> low = sine(200.0, 0.6);
        quiet.feed(low.data(), low.size());
        loud.feed(low.data(), low.size());
        CHECK(loud.snapshot(true).bass > quiet.snapshot(true).bass * 2.0f);
        AudioDsp gated;
        audio::AudioConfig g;
        g.gate = 0.15f;
        gated.setConfig(g);
        const std::vector<float> faint = sine(200.0, 0.6, 0.02);
        gated.feed(faint.data(), faint.size());
        CHECK(gated.snapshot(true).bass == 0.0f);
    }

    section("audio: analisador sem dispositivo e com o override de teste");
    audio::AudioAnalyzer& an = audio::AudioAnalyzer::instance();
    {
        const audio::AudioSnapshot s = an.snapshot();
        CHECK(!s.active && s.bass == 0.0f && s.high == 0.0f && s.rms == 0.0f && s.beatCount == 0);
    }
    an.setOverride(loudBass());
    CHECK(an.snapshot().bass == 1.5f);
    an.setOverride(std::nullopt);
    CHECK(an.snapshot().bass == 0.0f);
    // Captura de verdade (se a maquina tiver audio): liga, desliga, nada deve cair
    audio::AudioConfig off;
    off.enabled = false;
    an.setConfig(off);
    an.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(an.snapshot().bass == 0.0f);
    audio::AudioConfig on;
    an.setConfig(on);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::printf("  captura: %s | ativa=%d rms=%.3f\n", an.statusText().c_str(), an.snapshot().active ? 1 : 0, static_cast<double>(an.snapshot().rms));
    audio::AudioConfig input = on;
    input.source = audio::AudioSource::Input;
    an.setConfig(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::printf("  entrada: %s\n", an.statusText().c_str());
    an.stop();
    CHECK(!an.running());
    const auto loopbackDevices = audio::AudioAnalyzer::listDevices(audio::AudioSource::Loopback);
    const auto inputDevices = audio::AudioAnalyzer::listDevices(audio::AudioSource::Input);
    std::printf("  dispositivos: %zu saida(s), %zu entrada(s)\n", loopbackDevices.size(), inputDevices.size());
}

// ---------------------------------------------------------------------------------------------------- efeitos

void testRegistry()
{
    section("registro: provedor bsl: e geradores");
    fx::EffectRegistry& reg = fx::EffectRegistry::instance();
    for (const char* id : {bl::kAlphaReactorId, bl::kDatamoshId, bl::kTypoId, bl::kSliceRefinerId}) {
        const auto info = reg.find(id);
        CHECK_MSG(info.has_value(), std::string("find ") + id);
        if (info) {
            CHECK(info->section == fx::EffectSection::Bsl);
            CHECK(!info->description.empty());
            CHECK(!info->generator);
        }
        const auto descs = reg.describe(id);
        CHECK(!descs.empty());
        auto effect = reg.create(id);
        CHECK(effect != nullptr);
        if (effect) {
            CHECK(effect->id() == id);
            CHECK(effect->paramDescs().size() == descs.size());
        }
    }
    CHECK(!reg.find(bl::kGenBackgroundsId).has_value()); // as fontes nao entram como efeito
    // Contagem de parametros dos originais
    CHECK(reg.describe(bl::kAlphaReactorId).size() == 9);   // 10 sem o "Audio Source" (agora em Ver > Audio)
    CHECK(reg.describe(bl::kDatamoshId).size() == 16);      // 19 sem o "Audio Source"; Neon R/G/B viram uma cor
    CHECK(reg.describe(bl::kSliceRefinerId).size() == 50);  // 2 + 8 x 6
    for (const char* id : {bl::kGenBackgroundsId, bl::kLedMapperId}) {
        const GeneratorInfo* g = findGenerator(id);
        CHECK_MSG(g != nullptr, std::string("gerador ") + id);
        if (g) {
            CHECK(!g->describe({}).empty());
            const Clip c = Clip::fromId(id);
            CHECK(!c.empty() && c.isGenerator() && c.generatorId == id);
            CHECK(!c.params.empty()); // padroes gravados
        }
    }
    // Cadeia: serializa e le de volta
    fx::EffectChain chain;
    chain.add(bl::kAlphaReactorId).setParam("auraIntensity", Json(70.0));
    chain.add(bl::kTypoId);
    const Json j = chain.toJson();
    const fx::EffectChain back = fx::EffectChain::fromJson(j);
    CHECK(back.size() == 2 && back.items[0].typeId == bl::kAlphaReactorId && back.items[0].known());
}

void testEffectsBasic(FboPool& pool)
{
    section("efeitos: instanciam, 60 quadros, saida nao vazia");
    const GLuint opaque = makeCamera(false);
    const GLuint cutout = makeCamera(true);

    // Slices para o SliceRefiner2
    OutputSetup out;
    const int si = out.addScreen(960, 540);
    out.screens[static_cast<size_t>(si)].slices.clear();
    Slice a = makeFullSlice("A", 960, 540, 960, 540);
    a.input = {80, 60, 320, 180};
    out.screens[static_cast<size_t>(si)].slices.push_back(a);
    bl::SliceFeed::instance().set(bl::sliceFeedFromOutput(out), 960, 540);

    struct Case
    {
        const char* id;
        GLuint input;
        Values values;
    };
    const Case cases[] = {{bl::kAlphaReactorId, cutout, {}},
                          {bl::kDatamoshId, opaque, {{"sortLength", Json(40.0)}, {"moshAmount", Json(50.0)}}},
                          {bl::kTypoId, opaque, {}},
                          {bl::kSliceRefinerId, opaque, {}}};
    for (const Case& c : cases) {
        RunOptions opt;
        opt.canvasW = 960;
        opt.canvasH = 540;
        std::string status;
        const Img img = runEffect(pool, c.id, c.values, c.input, opt, &status);
        const bool ok = nonEmpty(img);
        CHECK_MSG(ok, std::string("saida vazia: ") + c.id);
        CHECK_MSG(status.empty(), std::string("status: ") + c.id + " " + status);
        std::printf("  %-26s ok=%d\n", c.id, ok ? 1 : 0);
    }
    for (const char* id : {bl::kGenBackgroundsId, bl::kLedMapperId}) {
        std::string status;
        const Img img = runSource(id, {}, 60, 480, 270, &status);
        CHECK_MSG(nonEmpty(img), std::string("fonte vazia: ") + id);
        CHECK_MSG(status.empty(), std::string("status: ") + id + " " + status);
    }
    // Todos os 22 estilos do GenBackgrounds compilam e desenham
    {
        const auto descs = bl::describeGenBackgrounds();
        const media::ParamDesc* style = nullptr;
        for (const auto& d : descs)
            if (d.id == "style")
                style = &d;
        CHECK(style && style->options.size() == 22);
        int drawn = 0;
        if (style)
            for (const auto& opt : style->options) {
                const Img img = runSource(bl::kGenBackgroundsId, {{"style", Json(opt.value)}, {"background", media::param::colorJson({0.1f, 0.0f, 0.2f, 1.0f})}}, 5);
                drawn += nonEmpty(img) ? 1 : 0;
            }
        std::printf("  GenBackgrounds: %d de 22 estilos desenham\n", drawn);
        CHECK(drawn == 22);
    }
    glDeleteTextures(1, &opaque);
    glDeleteTextures(1, &cutout);
}

void testParamsChangeOutput(FboPool& pool)
{
    section("parametros principais mudam a saida");
    const GLuint opaque = makeCamera(false);
    const GLuint cutout = makeCamera(true);
    RunOptions opt;
    opt.canvasW = 960;
    opt.canvasH = 540;
    auto differs = [&](const char* what, const std::string& id, GLuint in, const Values& a, const Values& b, int frames = 20,
                       double threshold = 0.3) {
        RunOptions o = opt;
        o.frames = frames;
        const Img ia = runEffect(pool, id, a, in, o);
        const Img ib = runEffect(pool, id, b, in, o);
        const double d = meanDiff(ia, ib);
        std::printf("  %-34s diferenca media %.2f\n", what, d);
        CHECK_MSG(d > threshold, std::string("nao mudou: ") + what);
    };
    // AlphaReactor
    differs("ALPR aura 0 -> 100", bl::kAlphaReactorId, cutout, {{"auraIntensity", Json(0.0)}}, {{"auraIntensity", Json(100.0)}});
    {
        // Rastro: so aparece se o sujeito se mexe; a entrada alterna entre duas posicoes
        const GLuint moved = makeTexture(kW, kH, [&](int x, int y, unsigned char* p) {
            const float dx = static_cast<float>(x) - kW * 0.75f, dy = static_cast<float>(y) - kH * 0.55f;
            const bool inside = std::sqrt(dx * dx + dy * dy) < kH * 0.28f;
            p[0] = 230;
            p[1] = 80;
            p[2] = 60;
            p[3] = inside ? 255 : 0;
        });
        RunOptions o = opt;
        o.frames = 20;
        o.altInput = moved;
        const Img none = runEffect(pool, bl::kAlphaReactorId, {{"trailLength", Json(0.0)}, {"trailZoom", Json(0.0)}, {"auraIntensity", Json(0.0)}}, cutout, o);
        const Img trail = runEffect(pool, bl::kAlphaReactorId, {{"trailLength", Json(90.0)}, {"trailZoom", Json(0.0)}, {"auraIntensity", Json(0.0)}}, cutout, o);
        const double d = meanDiff(none, trail);
        std::printf("  %-34s diferenca media %.2f\n", "ALPR rastro 0 -> 90 (sujeito em movimento)", d);
        CHECK_MSG(d > 1.0, "rastro nao aparece");
        glDeleteTextures(1, &moved);
    }
    // Datamosh
    differs("DMSH sort 0 -> 80", bl::kDatamoshId, opaque, {{"sortThreshold", Json(30.0)}, {"sortLength", Json(0.0)}},
            {{"sortThreshold", Json(30.0)}, {"sortLength", Json(80.0)}});
    differs("DMSH direcao horizontal -> vertical", bl::kDatamoshId, opaque,
            {{"sortThreshold", Json(40.0)}, {"sortLength", Json(60.0)}, {"sortDirection", Json("horizontal")}},
            {{"sortThreshold", Json(40.0)}, {"sortLength", Json(60.0)}, {"sortDirection", Json("vertical")}});
    differs("DMSH neon 0 -> 100", bl::kDatamoshId, opaque, {{"neonEdges", Json(0.0)}}, {{"neonEdges", Json(100.0)}});
    differs("DMSH protecao 0 -> 30", bl::kDatamoshId, opaque, {{"neonEdges", Json(100.0)}, {"protectRadius", Json(0.0)}},
            {{"neonEdges", Json(100.0)}, {"protectRadius", Json(30.0)}});
    // Typo
    differs("ARTY modo boiler -> glitch", bl::kTypoId, opaque, {{"mode", Json("boiler")}}, {{"mode", Json("glitch")}});
    differs("ARTY modo boiler -> marquee", bl::kTypoId, opaque, {{"mode", Json("boiler")}}, {{"mode", Json("marquee")}});
    differs("ARTY modo boiler -> mask", bl::kTypoId, opaque, {{"mode", Json("boiler")}}, {{"mode", Json("mask")}});
    differs("ARTY texto", bl::kTypoId, opaque, {{"djText", Json("DJ NAME")}}, {{"djText", Json("OUTRO")}});
    differs("ARTY mesclagem difference -> add", bl::kTypoId, opaque, {{"blendMode", Json("difference")}}, {{"blendMode", Json("add")}});
    differs("ARTY HUD 100 -> 0", bl::kTypoId, opaque, {{"hud", Json(100.0)}}, {{"hud", Json(0.0)}});
    differs("ARTY inverter", bl::kTypoId, opaque, {{"masterInvert", Json(false)}}, {{"masterInvert", Json(true)}});
    // Slice refiner (slices do OutputSetup ja publicados no teste anterior)
    differs("BSS2 modo neon -> gradiente", bl::kSliceRefinerId, opaque, {{"s1Mode", Json("neon")}}, {{"s1Mode", Json("gradient")}});
    differs("BSS2 espessura 10 -> 40", bl::kSliceRefinerId, opaque, {{"s1Stroke", Json(10.0)}}, {{"s1Stroke", Json(40.0)}});
    differs("BSS2 slice desligado", bl::kSliceRefinerId, opaque, {{"s1Enable", Json(true)}}, {{"s1Enable", Json(false)}});
    // Fontes
    auto sourceDiffers = [&](const char* what, const std::string& id, const Values& a, const Values& b) {
        const Img ia = runSource(id, a, 20), ib = runSource(id, b, 20);
        const double d = meanDiff(ia, ib);
        std::printf("  %-34s diferenca media %.2f\n", what, d);
        CHECK_MSG(d > 0.3, std::string("nao mudou: ") + what);
    };
    sourceDiffers("GBG1 estilo waves -> plasma", bl::kGenBackgroundsId, {{"style", Json("waves")}}, {{"style", Json("plasma")}});
    sourceDiffers("GBG1 cor 1", bl::kGenBackgroundsId, {{"color1", media::param::colorJson({1, 0, 0, 1})}},
                  {{"color1", media::param::colorJson({0, 1, 0, 1})}});
    sourceDiffers("GBG1 densidade", bl::kGenBackgroundsId, {{"density", Json(10.0)}}, {{"density", Json(90.0)}});
    sourceDiffers("GBG1 rotacao", bl::kGenBackgroundsId, {{"rotation", Json(0.0)}}, {{"rotation", Json(90.0)}});
    sourceDiffers("LMPR colunas 1 -> 4", bl::kLedMapperId, {{"columns", Json(1)}, {"rows", Json(1)}}, {{"columns", Json(4)}, {"rows", Json(1)}});
    sourceDiffers("LMPR espaco 0 -> 20", bl::kLedMapperId, {{"columns", Json(3)}, {"rows", Json(2)}, {"gapX", Json(0.0)}},
                  {{"columns", Json(3)}, {"rows", Json(2)}, {"gapX", Json(20.0)}});
    glDeleteTextures(1, &opaque);
    glDeleteTextures(1, &cutout);
}

void testAudioReaction(FboPool& pool)
{
    section("efeitos de audio reagem a um AudioSnapshot sintetico");
    const GLuint opaque = makeCamera(false);
    const GLuint cutout = makeCamera(true);
    RunOptions silent;
    silent.canvasW = 960;
    silent.canvasH = 540;
    RunOptions bass = silent;
    bass.audio = loudBass();
    RunOptions high = silent;
    high.audio = loudHigh();
    auto reacts = [&](const char* what, const std::string& id, GLuint in, const Values& v, const RunOptions& with, int frames,
                      double threshold = 0.3) {
        RunOptions a = silent, b = with;
        a.frames = b.frames = frames;
        const Img quiet = runEffect(pool, id, v, in, a);
        const Img loud = runEffect(pool, id, v, in, b);
        const double d = meanDiff(quiet, loud);
        std::printf("  %-44s diferenca media %.2f\n", what, d);
        CHECK_MSG(d > threshold, std::string("sem reacao: ") + what);
    };
    reacts("ALPR graves -> rastros", bl::kAlphaReactorId, cutout, {{"trailLength", Json(90.0)}, {"trailZoom", Json(5.0)}}, bass, 30);
    reacts("ALPR agudos -> aura", bl::kAlphaReactorId, cutout, {{"auraIntensity", Json(100.0)}, {"auraLength", Json(100.0)}}, high, 10);
    reacts("DMSH graves -> sort", bl::kDatamoshId, opaque, {{"sortThreshold", Json(60.0)}, {"sortLength", Json(50.0)}}, bass, 10);
    reacts("DMSH agudos -> comprimento do sort", bl::kDatamoshId, opaque, {{"sortThreshold", Json(45.0)}, {"sortLength", Json(40.0)}}, high, 10);
    reacts("DMSH graves -> neon", bl::kDatamoshId, opaque,
           {{"neonEdges", Json(100.0)}, {"neonColor", media::param::colorJson({0.15f, 0.0f, 0.0f, 1.0f})}}, bass, 10, 0.1);
    reacts("ARTY graves (tremor/VU/texto)", bl::kTypoId, opaque, {}, bass, 10);
    reacts("ARTY agudos (glitch/VU)", bl::kTypoId, opaque, {}, high, 10);
    // O ganho do efeito escala a reacao: ganho minimo quase nao reage
    {
        RunOptions a = bass;
        a.frames = 10;
        RunOptions s = silent;
        s.frames = 10;
        const Img quiet = runEffect(pool, bl::kDatamoshId, {{"gain", Json(0.1)}, {"sortThreshold", Json(60.0)}, {"sortLength", Json(50.0)}}, opaque, s);
        const Img lowGain = runEffect(pool, bl::kDatamoshId, {{"gain", Json(0.1)}, {"sortThreshold", Json(60.0)}, {"sortLength", Json(50.0)}}, opaque, a);
        const Img fullGain = runEffect(pool, bl::kDatamoshId, {{"gain", Json(4.1)}, {"sortThreshold", Json(60.0)}, {"sortLength", Json(50.0)}}, opaque, a);
        std::printf("  DMSH ganho 0.1: %.2f | ganho 4.1: %.2f\n", meanDiff(quiet, lowGain), meanDiff(quiet, fullGain));
        CHECK(meanDiff(quiet, fullGain) > meanDiff(quiet, lowGain));
    }
    // ARTY no modo manual: sem o analisador, so os controles de reacao
    {
        const Img quiet = runEffect(pool, bl::kTypoId, {{"audioSource", Json("manual")}}, opaque, silent);
        const Img deaf = runEffect(pool, bl::kTypoId, {{"audioSource", Json("manual")}}, opaque, [&] { RunOptions o = bass; return o; }());
        CHECK(meanDiff(quiet, deaf) < 0.05); // o snapshot e ignorado
        const Img manual = runEffect(pool, bl::kTypoId, {{"audioSource", Json("manual")}, {"bassReact", Json(100.0)}}, opaque, silent);
        CHECK(meanDiff(quiet, manual) > 0.3);
    }
    glDeleteTextures(1, &opaque);
    glDeleteTextures(1, &cutout);
}

void testOrientation(FboPool& pool)
{
    section("orientacao: topOrigin verdadeiro e falso dao a mesma imagem");
    const GLuint topTex = makeCamera(false, true);
    const GLuint botTex = makeCamera(false, false); // a mesma imagem, linha 0 na base
    const GLuint topCut = makeCamera(true, true);
    const GLuint botCut = makeCamera(true, false);
    RunOptions top;
    top.frames = 1;
    top.canvasW = 960;
    top.canvasH = 540;
    RunOptions bot = top;
    bot.topOrigin = false;
    struct Case
    {
        const char* id;
        bool cutout;
        Values values;
    };
    const Case cases[] = {
        {bl::kAlphaReactorId, true, {{"auraIntensity", Json(100.0)}}},
        {bl::kDatamoshId, false, {{"sortThreshold", Json(40.0)}, {"sortLength", Json(60.0)}, {"sortDirection", Json("vertical")}}},
        {bl::kDatamoshId, false, {{"sortThreshold", Json(40.0)}, {"sortLength", Json(60.0)}, {"sortDirection", Json("diagonal")}, {"neonEdges", Json(60.0)}, {"protectRadius", Json(20.0)}, {"protectY", Json(30.0)}}},
        // sem grao: ele e um hash de sin() em coordenadas grandes e nao bate bit a bit entre as duas orientacoes
        {bl::kTypoId, false, {{"mode", Json("boiler")}, {"vignetteGrain", Json(0.0)}}},
        {bl::kTypoId, false, {{"mode", Json("marquee")}, {"vignetteGrain", Json(0.0)}}},
        {bl::kTypoId, false, {{"mode", Json("glitch")}, {"vignetteGrain", Json(0.0)}, {"djText", Json("TESTE")}}},
        {bl::kSliceRefinerId, false, {{"s1Mode", Json("gradient")}}},
    };
    for (const Case& c : cases) {
        const Img a = runEffect(pool, c.id, c.values, c.cutout ? topCut : topTex, top);
        const Img b = runEffect(pool, c.id, c.values, c.cutout ? botCut : botTex, bot);
        const double d = meanDiff(a, flipV(b));
        std::printf("  %-26s diferenca apos inverter %.3f\n", c.id, d);
        CHECK_MSG(d < 1.2, std::string("orientacao: ") + c.id);
    }
    for (GLuint t : {topTex, botTex, topCut, botCut})
        glDeleteTextures(1, &t);
}

void testSlices(FboPool& pool)
{
    section("SliceRefiner2: slices do OutputSetup, XML do Resolume e LED Mapper");
    const GLuint opaque = makeCamera(false);
    constexpr int cw = 960, ch = 540;
    OutputSetup setup;
    const int si = setup.addScreen(cw, ch);
    Screen& screen = setup.screens[static_cast<size_t>(si)];
    screen.slices.clear();
    Slice a = makeFullSlice("A", cw, ch, cw, ch);
    a.input = {80, 60, 320, 180};
    Slice b = makeFullSlice("B", cw, ch, cw, ch);
    b.input = {600, 300, 240, 160};
    screen.slices.push_back(a);
    screen.slices.push_back(b);
    Slice off = makeFullSlice("C (desligado)", cw, ch, cw, ch);
    off.input = {10, 10, 50, 50};
    off.enabled = false;
    screen.slices.push_back(off);

    const std::vector<bl::FeedSlice> feed = bl::sliceFeedFromOutput(setup);
    CHECK(feed.size() == 2); // o slice desligado nao entra
    CHECK(feed.size() == 2 && feed[0].x == 80.0f && feed[0].w == 320.0f && feed[1].y == 300.0f);
    bl::SliceFeed::instance().set(feed, cw, ch);

    RunOptions opt;
    opt.frames = 3;
    opt.canvasW = cw;
    opt.canvasH = ch;
    opt.w = kW;
    opt.h = kH;
    // Os slices estao em pixels de um canvas 960x540; a textura tem 640x360 (escala 2/3)
    auto px = [&](const Img& img, int x, int y) { return img.at(x * kW / cw, y * kH / ch); };
    Img img = runEffect(pool, bl::kSliceRefinerId, {{"s1Mode", Json("neon")}, {"s1Hue", Json(180.0)}, {"s2Mode", Json("neon")}, {"s2Hue", Json(0.0)}}, opaque, opt);
    const unsigned char* edgeA = px(img, 80, 150);   // borda esquerda do slice A: ciano
    const unsigned char* inside = px(img, 240, 150); // dentro do slice, longe da borda: transparente (so a moldura)
    const unsigned char* edgeB = px(img, 600, 380);  // borda esquerda do slice B: vermelho
    const unsigned char* outside = px(img, 800, 80);
    std::printf("  borda A (%d,%d,%d,%d) | dentro (%d,%d,%d,%d) | borda B (%d,%d,%d,%d) | fora a=%d\n", edgeA[0], edgeA[1], edgeA[2], edgeA[3],
                inside[0], inside[1], inside[2], inside[3], edgeB[0], edgeB[1], edgeB[2], edgeB[3], outside[3]);
    CHECK(edgeA[3] > 200 && edgeA[1] > 200 && edgeA[2] > 200 && edgeA[0] < 60);
    CHECK(inside[3] < 10);
    CHECK(edgeB[3] > 200 && edgeB[0] > 200 && edgeB[1] < 60);
    CHECK(outside[3] < 10);

    // Mudar o OutputSetup muda a imagem (o app publica a cada quadro)
    screen.slices[0].input = {300, 60, 320, 180};
    bl::SliceFeed::instance().set(bl::sliceFeedFromOutput(setup), cw, ch);
    Img moved = runEffect(pool, bl::kSliceRefinerId, {{"s1Mode", Json("neon")}, {"s1Hue", Json(180.0)}, {"s2Mode", Json("neon")}, {"s2Hue", Json(0.0)}}, opaque, opt);
    CHECK(px(moved, 80, 150)[3] < 10);
    CHECK(px(moved, 300, 150)[3] > 200);
    CHECK(meanDiff(img, moved) > 0.3);

    // XML do Resolume: o LED Mapper exporta e o refiner le de volta
    bl::LedGrid grid;
    grid.columns = 3;
    grid.rows = 2;
    grid.cabinetW = 100;
    grid.cabinetH = 50;
    grid.gapX = 10;
    grid.gapY = 5;
    const std::string xml = bl::buildLedMapperXml(grid, 12345u);
    CHECK(xml.find("<ScreenSetup") != std::string::npos && xml.find("</ScreenSetup>") != std::string::npos);
    const std::vector<bl::FeedSlice> parsed = bl::parseResolumeSlices(xml);
    CHECK(parsed.size() == 6);
    if (parsed.size() == 6) {
        CHECK(parsed[0].name == "Cabinet 1-1" && parsed[0].x == 0.0f && parsed[0].y == 0.0f && parsed[0].w == 100.0f && parsed[0].h == 50.0f);
        CHECK(parsed[4].x == 110.0f && parsed[4].y == 55.0f); // coluna 2, linha 2
        CHECK(parsed[5].x == 220.0f && parsed[5].name == "Cabinet 3-2");
    }
    // ids unicos
    {
        size_t at = 0;
        std::vector<std::string> ids;
        while ((at = xml.find("uniqueId=\"", at)) != std::string::npos) {
            at += 10;
            ids.push_back(xml.substr(at, xml.find('"', at) - at));
        }
        std::sort(ids.begin(), ids.end());
        CHECK(ids.size() == 7 && std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    }
    const fs::path file = fs::temp_directory_path() / L"bsl-bsltest-led.xml";
    std::string error;
    CHECK(bl::writeLedMapperXml(file, grid, &error));
    {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(bl::parseResolumeSlices(text).size() == 6);
    }
    // Importar o XML no efeito: origem "xml" com o arquivo
    {
        const std::string path(reinterpret_cast<const char*>(file.u8string().c_str()));
        Img fromXml = runEffect(pool, bl::kSliceRefinerId,
                                {{"source", Json("xml")}, {"xmlFile", Json(path)}, {"s1Mode", Json("neon")}, {"s1Hue", Json(120.0)}, {"s1Stroke", Json(6.0)}},
                                opaque, opt);
        // Cabinet 1-1: (0,0)-(100,50) em 960x540 -> a borda direita em x=100 e verde (matiz 120), y=25
        const unsigned char* e = px(fromXml, 100, 25);
        std::printf("  XML importado: borda do cabinet 1-1 (%d,%d,%d,%d)\n", e[0], e[1], e[2], e[3]);
        CHECK(e[3] > 150 && e[1] > 150 && e[0] < 80);
        // O XML sem arquivo no caminho padrao: nenhum slice (sem erro)
        Img missing = runEffect(pool, bl::kSliceRefinerId, {{"source", Json("xml")}, {"xmlFile", Json("Z:\\nao-existe\\x.xml")}}, opaque, opt);
        CHECK(!nonEmpty(missing));
    }
    std::error_code ec;
    fs::remove(file, ec);
    glDeleteTextures(1, &opaque);
}

void testLedExport()
{
    section("LED Mapper: botao Exportar grava o XML do arquivo escolhido");
    const fs::path file = fs::temp_directory_path() / L"bsl-bsltest-led-export.xml";
    std::error_code ec;
    fs::remove(file, ec);
    const GeneratorInfo* info = findGenerator(bl::kLedMapperId);
    CHECK(info != nullptr);
    if (!info)
        return;
    auto src = info->create(480, 270);
    media::ParamValues v;
    v.fillDefaults(info->describe(v));
    v.set("columns", 4);
    v.set("rows", 3);
    src->setParams(v);
    CHECK(src->open({}, nullptr));
    // Como o ParamWidgets: soma 1 ao contador e grava o caminho
    const std::string path(reinterpret_cast<const char*>(file.u8string().c_str()));
    v.set("export", Json{{"n", 1}, {"path", path}});
    src->setParams(v);
    CHECK(fs::exists(file));
    {
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(bl::parseResolumeSlices(text).size() == 12);
    }
    CHECK(src->status().text.find("XML gravado") != std::string::npos);
    // Reabrir com o mesmo contador (projeto salvo) nao exporta de novo
    fs::remove(file, ec);
    auto again = info->create(480, 270);
    again->setParams(v);
    CHECK(again->open({}, nullptr));
    again->update(1.0 / 60.0);
    CHECK(!fs::exists(file));
    // Caminho invalido: aviso, sem excecao
    v.set("export", Json{{"n", 2}, {"path", "Z:\\pasta-que-nao-existe\\x.xml"}});
    src->setParams(v);
    CHECK(src->status().text.find("Não foi possível") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------------- etapa B: BSR3, PPT, WBBG, WB01

// Cliente HTTP minimo (Winsock) para falar com o controle remoto dos plugins
struct HttpReply
{
    int status = 0;
    std::string contentType;
    std::string body;
};

HttpReply httpRequest(int port, const std::string& method, const std::string& path, const std::string& body = {})
{
    HttpReply r;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    const DWORD timeout = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        WSACleanup();
        return r;
    }
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    if (!body.empty())
        req += "Content-Type: text/plain;charset=UTF-8\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    send(s, req.data(), static_cast<int>(req.size()), 0);
    std::string resp;
    char buf[65536];
    int n;
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
    closesocket(s);
    WSACleanup();
    const size_t sp = resp.find(' ');
    if (sp != std::string::npos)
        r.status = std::atoi(resp.c_str() + sp + 1);
    const size_t he = resp.find("\r\n\r\n");
    if (he != std::string::npos) {
        std::string head = resp.substr(0, he);
        for (char& c : head)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const size_t ct = head.find("content-type:");
        if (ct != std::string::npos) {
            const size_t e = head.find("\r\n", ct);
            r.contentType = head.substr(ct + 13, e == std::string::npos ? std::string::npos : e - ct - 13);
            while (!r.contentType.empty() && r.contentType.front() == ' ')
                r.contentType.erase(0, 1);
        }
        r.body = resp.substr(he + 4);
    }
    return r;
}

int freePort()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    int len = sizeof(addr);
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    closesocket(s);
    WSACleanup();
    return port;
}

// Quantos bslstage_worker.exe existem como filhos deste processo
int workerCount()
{
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, L"bslstage_worker.exe") == 0 && pe.th32ParentProcessID == GetCurrentProcessId())
            ++n;
    CloseHandle(snap);
    return n;
}

// Espera (ate 'seconds') os workers filhos baixarem para 'baseline'
bool waitWorkers(int baseline, double seconds)
{
    const auto t0 = std::chrono::steady_clock::now();
    while (workerCount() > baseline) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > seconds)
            return false;
        Sleep(100);
    }
    return true;
}

// Um efeito vivo, para testes que precisam de varios quadros seguidos e de pollParamChanges
struct EffectRig
{
    std::unique_ptr<fx::IEffect> effect;
    media::ParamValues values;
    Fbo out;
    GLuint input;
    FboPool& pool;
    int w, h, frame = 0;
    int canvasW = 960, canvasH = 540;

    EffectRig(FboPool& p, const std::string& id, const Values& v, GLuint in, int ww = kW, int hh = kH)
        : effect(fx::EffectRegistry::instance().create(id)), out(ww, hh), input(in), pool(p), w(ww), h(hh)
    {
        for (const auto& [k, val] : v)
            values.set(k, val);
        applyValues();
    }
    void applyValues() { effect->setParams(media::ParamSet(effect->paramDescs(), values)); }
    void step()
    {
        fx::EffectContext ctx;
        ctx.time = frame / 60.0;
        ctx.dt = 1.0 / 60.0;
        ctx.frameIndex = static_cast<std::uint64_t>(frame);
        ctx.bpm = 128.0f;
        ctx.beat = frame / 60.0 * 128.0 / 60.0;
        ctx.canvasWidth = canvasW;
        ctx.canvasHeight = canvasH;
        ctx.topOrigin = true;
        ctx.pool = &pool;
        effect->process(ctx, input, w, h, out);
        ++frame;
    }
    Img read()
    {
        glFinish();
        return readTexture(out.texture(), w, h);
    }
};

bool closeTo(const unsigned char* px, int r, int g, int b, int tol = 10)
{
    return std::abs(px[0] - r) <= tol && std::abs(px[1] - g) <= tol && std::abs(px[2] - b) <= tol;
}

Json sliceState(const std::vector<std::string>& ids, int group, const std::function<void(Json&)>& tweak = {})
{
    bl::SliceState s;
    for (const std::string& id : ids)
        s.assign[id] = group;
    Json j = bl::sliceStateToJson(s);
    if (tweak)
        tweak(j["groups"][static_cast<size_t>(group)]);
    return j;
}

void testSliceRefiner3(FboPool& pool)
{
    section("BSL Slice Refiner (BSR3): estado, slices do BSL Stage, XML do Resolume, rotacao, animacao");
    fx::EffectRegistry& reg = fx::EffectRegistry::instance();
    const auto info = reg.find(bl::kSliceRefiner3Id);
    CHECK(info.has_value() && info->section == fx::EffectSection::Bsl && !info->description.empty());
    CHECK(reg.describe(bl::kSliceRefiner3Id).size() == 10);
    {
        // O parametro "remote" nasce desligado e a porta padrao e a do plugin original
        media::ParamValues defaults;
        defaults.fillDefaults(reg.describe(bl::kSliceRefiner3Id));
        CHECK(defaults.find("remote") && !defaults.find("remote")->get<bool>());
        CHECK(defaults.find("remotePort") && defaults.find("remotePort")->get<int>() == 8083);
        CHECK(defaults.find("opacity") && defaults.find("opacity")->get<double>() == 100.0);
        CHECK(defaults.find("masterSpeed") && defaults.find("masterSpeed")->get<double>() == 1.0);
    }

    // Estado JSON: ida e volta, limites do original
    {
        bl::SliceState s;
        CHECK(bl::sliceStateFromJson(bl::defaultSliceStateJson(), s));
        CHECK(bl::sliceStateToJson(s) == bl::defaultSliceStateJson());
        CHECK(!bl::sliceStateFromJson(Json::array(), s)); // so objetos
        Json wild = {{"assign", {{"a", 3}, {"b", 99}, {"", 1}}},
                     {"groups", Json::array({Json{{"masterSize", 5.0}, {"offset", -9999}, {"corners", Json::array({2, -1, 0.5, "x"})},
                                                  {"stroke", {{"width", -3}, {"color", "#ff0000"}, {"effect", "radial"}, {"speed", 9}}},
                                                  {"blinker", {{"mode", "bpm"}, {"rate", 0}}},
                                                  {"bounce", {{"mode", "x"}, {"min", 3}}}}})}};
        bl::SliceState w;
        CHECK(bl::sliceStateFromJson(wild, w));
        CHECK(w.assign.size() == 1 && w.assign.count("a") && w.assign["a"] == 3);
        CHECK(w.groups[0].masterSize == 1.0f && w.groups[0].offset == -200.0f);
        CHECK(w.groups[0].corners[0] == 1.0f && w.groups[0].corners[1] == 0.0f && w.groups[0].corners[2] == 0.5f && w.groups[0].corners[3] == 1.0f);
        CHECK(w.groups[0].strokeWidth == 0.0f && w.groups[0].effect == 2 && w.groups[0].speed == 2.0f && w.groups[0].color[0] == 1.0f && w.groups[0].color[1] == 0.0f);
        CHECK(w.groups[0].blinkMode == bl::SliceMode::Bpm && std::abs(w.groups[0].blinkRate - 0.05f) < 1e-6f);
        CHECK(w.groups[0].bounceMode == bl::SliceMode::Off && w.groups[0].bounceMin == 1.0f);
        CHECK(w.groups[1].name == "Grupo 2"); // os grupos ausentes ficam no padrao
    }

    // O estado mora nos parametros do efeito: a cadeia grava e le de volta (o que o projeto faz)
    {
        fx::EffectChain chain;
        const Json st = sliceState({"Tela/A#0"}, 2, [](Json& g) { g["stroke"]["color"] = "#123456"; });
        chain.add(bl::kSliceRefiner3Id).setParam("state", st);
        chain.items[0].setParam("remotePort", Json(9123));
        const fx::EffectChain back = fx::EffectChain::fromJson(chain.toJson());
        CHECK(back.size() == 1 && back.items[0].known());
        const media::ParamSet ps(back.items[0].paramDescs(), back.items[0].params);
        CHECK(ps.raw("state") == st && ps.i("remotePort") == 9123 && !ps.b("remote"));
    }

    // Slices do OutputSetup de teste (canvas 960x540; a textura tem 640x360)
    const GLuint cam = makeCamera(false);
    constexpr int cw = 960, ch = 540;
    OutputSetup setup;
    const int si = setup.addScreen(cw, ch);
    Screen& screen = setup.screens[static_cast<size_t>(si)];
    screen.slices.clear();
    Slice a = makeFullSlice("A", cw, ch, cw, ch);
    a.input = {80, 60, 320, 180};
    Slice b = makeFullSlice("B", cw, ch, cw, ch);
    b.input = {600, 300, 240, 160};
    screen.slices.push_back(a);
    screen.slices.push_back(b);
    const std::vector<bl::FeedSlice> feed = bl::sliceFeedFromOutput(setup);
    CHECK(feed.size() == 2 && feed[0].id.find("/A#0") != std::string::npos && feed[1].id.find("/B#1") != std::string::npos);
    bl::SliceFeed::instance().set(feed, cw, ch);
    const std::vector<std::string> ids = {feed[0].id, feed[1].id};

    RunOptions opt;
    opt.frames = 3;
    opt.canvasW = cw;
    opt.canvasH = ch;
    auto px = [&](const Img& img, int x, int y) { return img.at(x * kW / cw, y * kH / ch); };
    auto camAt = [&](int x, int y, unsigned char out[3]) {
        unsigned char p[4];
        cameraPixel(false, x * kW / cw, y * kH / ch, kW, kH, p);
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    };

    // Sem grupos atribuidos o efeito deixa a imagem passar
    {
        const Img pass = runEffect(pool, bl::kSliceRefiner3Id, {}, cam, opt);
        const Img ref = makeCameraImg(false, true, kW, kH);
        CHECK(meanDiff(pass, ref) < 1.0);
    }

    // Grupo 1 com os dois slices: video dentro, borda vermelha, preto fora
    const Json redState = sliceState(ids, 0, [](Json& g) {
        g["masterSize"] = 0.0;
        g["merge"] = false;
        g["stroke"]["width"] = 8.0;
        g["stroke"]["color"] = "#ff0000";
    });
    Img img = runEffect(pool, bl::kSliceRefiner3Id, {{"state", redState}}, cam, opt);
    {
        unsigned char want[3];
        camAt(240, 150, want);
        const unsigned char* inside = px(img, 240, 150);
        const unsigned char* edgeA = px(img, 80, 150);
        const unsigned char* edgeB = px(img, 600, 380);
        const unsigned char* outside = px(img, 800, 80);
        const unsigned char* between = px(img, 500, 300);
        std::printf("  dentro de A (%d,%d,%d) esperado (%d,%d,%d) | borda A (%d,%d,%d) | borda B (%d,%d,%d) | fora (%d,%d,%d,%d)\n", inside[0],
                    inside[1], inside[2], want[0], want[1], want[2], edgeA[0], edgeA[1], edgeA[2], edgeB[0], edgeB[1], edgeB[2], outside[0],
                    outside[1], outside[2], outside[3]);
        CHECK(closeTo(inside, want[0], want[1], want[2], 8) && inside[3] == 255);
        CHECK(edgeA[0] > 220 && edgeA[1] < 40 && edgeA[2] < 40);
        CHECK(edgeB[0] > 220 && edgeB[1] < 40 && edgeB[2] < 40);
        CHECK(outside[0] < 8 && outside[1] < 8 && outside[2] < 8 && outside[3] == 255); // fundo preto
        CHECK(between[3] == 255 && between[0] < 8);
        unsigned char wantB[3];
        camAt(720, 380, wantB);
        CHECK(closeTo(px(img, 720, 380), wantB[0], wantB[1], wantB[2], 8));
    }
    // Fundo transparente fora dos slices
    {
        const Img t = runEffect(pool, bl::kSliceRefiner3Id, {{"state", redState}, {"blackBg", Json(false)}}, cam, opt);
        CHECK(px(t, 800, 80)[3] < 8 && px(t, 240, 150)[3] == 255);
    }
    // So borda (sem video dentro)
    {
        const Json edgeOnly = sliceState(ids, 0, [](Json& g) {
            g["fill"] = "none";
            g["masterSize"] = 0.0;
            g["merge"] = false;
            g["stroke"]["width"] = 8.0;
            g["stroke"]["color"] = "#00ff00";
        });
        const Img t = runEffect(pool, bl::kSliceRefiner3Id, {{"state", edgeOnly}, {"blackBg", Json(false)}}, cam, opt);
        CHECK(px(t, 240, 150)[3] < 8);
        CHECK(px(t, 80, 150)[1] > 200 && px(t, 80, 150)[3] > 200);
    }
    // Opacidade: 0 devolve a imagem original; 50% mistura
    {
        const Img zero = runEffect(pool, bl::kSliceRefiner3Id, {{"state", redState}, {"opacity", Json(0.0)}}, cam, opt);
        CHECK(meanDiff(zero, makeCameraImg(false, true, kW, kH)) < 1.5);
        const Img half = runEffect(pool, bl::kSliceRefiner3Id, {{"state", redState}, {"opacity", Json(50.0)}}, cam, opt);
        CHECK(meanDiff(half, img) > 3.0 && meanDiff(half, zero) > 3.0);
    }
    // Arredondamento: o canto do slice A fica preto com cantos redondos, com video com cantos retos
    {
        const Json round = sliceState(ids, 0, [](Json& g) {
            g["masterSize"] = 1.0;
            g["merge"] = false;
            g["stroke"]["width"] = 0.0;
        });
        const Img rounded = runEffect(pool, bl::kSliceRefiner3Id, {{"state", round}}, cam, opt);
        const Json sq = sliceState(ids, 0, [](Json& g) {
            g["masterSize"] = 0.0;
            g["merge"] = false;
            g["stroke"]["width"] = 0.0;
        });
        const Img square = runEffect(pool, bl::kSliceRefiner3Id, {{"state", sq}}, cam, opt);
        CHECK(px(rounded, 84, 64)[0] < 10 && px(square, 84, 64)[3] == 255 && px(square, 84, 64)[2] > 40);
        CHECK(px(rounded, 240, 150)[3] == 255 && px(rounded, 240, 150)[0] > 20);
    }
    // Juntar slices: dois slices lado a lado viram um formato so (sem borda entre eles)
    {
        OutputSetup adj;
        const int ai = adj.addScreen(cw, ch);
        adj.screens[static_cast<size_t>(ai)].slices.clear();
        for (int i = 0; i < 2; ++i) {
            Slice s = makeFullSlice(i == 0 ? "L" : "R", cw, ch, cw, ch);
            s.input = {100 + i * 200, 100, 200, 200};
            adj.screens[static_cast<size_t>(ai)].slices.push_back(s);
        }
        const auto f2 = bl::sliceFeedFromOutput(adj);
        bl::SliceFeed::instance().set(f2, cw, ch);
        const std::vector<std::string> ids2 = {f2[0].id, f2[1].id};
        auto make = [&](bool merge) {
            return sliceState(ids2, 0, [&](Json& g) {
                g["masterSize"] = 0.0;
                g["merge"] = merge;
                g["stroke"]["width"] = 8.0;
                g["stroke"]["color"] = "#ff0000";
            });
        };
        const Img merged = runEffect(pool, bl::kSliceRefiner3Id, {{"state", make(true)}}, cam, opt);
        const Img split = runEffect(pool, bl::kSliceRefiner3Id, {{"state", make(false)}}, cam, opt);
        CHECK(px(split, 300, 200)[0] > 200 && px(split, 300, 200)[1] < 60);   // linha entre os slices
        CHECK(px(merged, 300, 200)[1] > 60 || px(merged, 300, 200)[0] < 200); // juntos: sem linha no meio
        bl::SliceFeed::instance().set(feed, cw, ch);
    }

    // AdvancedOutput.xml de exemplo: um slice reto e um girado 30 graus; importado pelo parametro "source"
    {
        const double ang = 30.0 * std::numbers::pi / 180.0;
        auto rot = [&](double lx, double ly, double ccx, double ccy) {
            return std::pair<double, double>{ccx + lx * std::cos(ang) - ly * std::sin(ang), ccy + lx * std::sin(ang) + ly * std::cos(ang)};
        };
        std::string xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ScreenSetup name=\"ScreenSetup\">\n<CurrentCompositionTextureSize width=\"960\" height=\"540\"/>\n<screens>\n"
            "<Screen name=\"Palco\" uniqueId=\"1\"><Params name=\"Params\"><Param name=\"Name\" T=\"STRING\" default=\"\" value=\"Palco\"/></Params><layers>\n"
            "<Slice uniqueId=\"2\"><Params name=\"Common\"><Param name=\"Name\" T=\"STRING\" default=\"Layer\" value=\"Reto\"/></Params>"
            "<InputRect orientation=\"0\"><v x=\"80\" y=\"60\"/><v x=\"400\" y=\"60\"/><v x=\"400\" y=\"240\"/><v x=\"80\" y=\"240\"/></InputRect></Slice>\n";
        const auto v0 = rot(-100, -50, 700, 400), v1 = rot(100, -50, 700, 400), v2 = rot(100, 50, 700, 400), v3 = rot(-100, 50, 700, 400);
        char slice2[512];
        std::snprintf(slice2, sizeof(slice2),
                      "<Slice uniqueId=\"3\"><Params name=\"Common\"><Param name=\"Name\" T=\"STRING\" default=\"Layer\" value=\"Girado\"/></Params>"
                      "<InputRect orientation=\"0\"><v x=\"%.4f\" y=\"%.4f\"/><v x=\"%.4f\" y=\"%.4f\"/><v x=\"%.4f\" y=\"%.4f\"/><v x=\"%.4f\" y=\"%.4f\"/></InputRect></Slice>\n",
                      v0.first, v0.second, v1.first, v1.second, v2.first, v2.second, v3.first, v3.second);
        xml += slice2;
        xml += "</layers></Screen></screens></ScreenSetup>\n";
        const fs::path file = fs::temp_directory_path() / L"bsl-bsltest-bsr3.xml";
        {
            std::ofstream out(file, std::ios::binary);
            out << xml;
        }
        const std::string path(reinterpret_cast<const char*>(file.u8string().c_str()));
        int cwx = 0, chx = 0;
        const std::vector<bl::FeedSlice> parsed = bl::parseResolumeSlices(xml, &cwx, &chx);
        CHECK(parsed.size() == 2 && cwx == 960 && chx == 540);
        if (parsed.size() == 2) {
            CHECK(parsed[0].id == "Palco/Reto#0" && parsed[1].id == "Palco/Girado#1" && parsed[0].screen == "Palco");
            CHECK(std::abs(parsed[1].angle - 30.0f) < 0.05f && std::abs(parsed[1].hw - 100.0f) < 0.05f && std::abs(parsed[1].hh - 50.0f) < 0.05f);
            CHECK(parsed[1].w > 200.0f && parsed[1].h > 100.0f); // a caixa envolvente e maior que o retangulo
            const Json st = sliceState({parsed[0].id, parsed[1].id}, 0, [](Json& g) {
                g["masterSize"] = 0.0;
                g["merge"] = false;
                g["stroke"]["width"] = 0.0;
            });
            const Img x = runEffect(pool, bl::kSliceRefiner3Id, {{"source", Json("xml")}, {"xmlFile", Json(path)}, {"state", st}}, cam, opt);
            unsigned char want[3];
            camAt(240, 150, want);
            CHECK(closeTo(px(x, 240, 150), want[0], want[1], want[2], 8)); // dentro do reto
            const unsigned char* center = px(x, 700, 400);
            unsigned char wantC[3];
            camAt(700, 400, wantC);
            CHECK(closeTo(center, wantC[0], wantC[1], wantC[2], 8)); // centro do girado: video
            // O canto da caixa envolvente do girado (fora do retangulo girado) e preto; um ponto perto da ponta do retangulo e video
            const int cornerX = 700 + 110, cornerY = 400 - 76; // dentro da caixa, fora do retangulo
            const unsigned char* corner = px(x, cornerX, cornerY);
            CHECK(corner[0] < 8 && corner[1] < 8 && corner[3] == 255);
            const auto tip = rot(95, 0, 700, 400); // dentro do retangulo, perto da ponta
            unsigned char wantT[3];
            camAt(static_cast<int>(tip.first), static_cast<int>(tip.second), wantT);
            CHECK(closeTo(px(x, static_cast<int>(tip.first), static_cast<int>(tip.second)), wantT[0], wantT[1], wantT[2], 10));
            std::printf("  XML: reto + girado 30 graus; centro do girado (%d,%d,%d), canto da caixa (%d,%d,%d)\n", center[0], center[1], center[2], corner[0],
                        corner[1], corner[2]);
        }
        std::error_code ec;
        fs::remove(file, ec);
    }

    // Animacao: a borda arco-iris anda com o tempo; velocidade geral 0 congela
    {
        const Json rb = sliceState(ids, 0, [](Json& g) {
            g["masterSize"] = 0.0;
            g["merge"] = false;
            g["stroke"]["width"] = 16.0;
            g["stroke"]["mode"] = "effect";
            g["stroke"]["effect"] = "rainbow";
            g["stroke"]["speed"] = 1.0;
        });
        RunOptions t0 = opt, t1 = opt;
        t1.frames = 120; // 2 s depois
        const Img early = runEffect(pool, bl::kSliceRefiner3Id, {{"state", rb}}, cam, t0);
        const Img late = runEffect(pool, bl::kSliceRefiner3Id, {{"state", rb}}, cam, t1);
        CHECK(meanDiff(early, late) > 0.2);
        const Img frozen = runEffect(pool, bl::kSliceRefiner3Id, {{"state", rb}, {"masterSpeed", Json(0.0)}}, cam, t1);
        const Img frozen0 = runEffect(pool, bl::kSliceRefiner3Id, {{"state", rb}, {"masterSpeed", Json(0.0)}}, cam, t0);
        CHECK(meanDiff(frozen, frozen0) < 0.05);
        // Os 12 efeitos de borda desenham e sao diferentes entre si
        int distinct = 0;
        Img first;
        for (int e = 0; e < bl::kSliceEffects; ++e) {
            const Json st = sliceState(ids, 0, [&](Json& g) {
                g["masterSize"] = 0.0;
                g["merge"] = false;
                g["stroke"]["width"] = 14.0;
                g["stroke"]["mode"] = "effect";
                g["stroke"]["effect"] = bl::kSliceEffectIds[e];
                g["stroke"]["color"] = "#ffcc00";
                g["stroke"]["colorB"] = "#0033ff";
            });
            RunOptions o2 = opt;
            o2.frames = 20;
            const Img e_img = runEffect(pool, bl::kSliceRefiner3Id, {{"state", st}}, cam, o2);
            CHECK_MSG(nonEmpty(e_img), std::string("efeito de borda vazio: ") + bl::kSliceEffectIds[e]);
            if (e == 0)
                first = e_img;
            else if (meanDiff(first, e_img) > 0.02)
                ++distinct;
        }
        CHECK(distinct >= 10);
        // Piscar: com o modo velocidade a borda some em parte do ciclo
        const Json blink = sliceState(ids, 0, [](Json& g) {
            g["masterSize"] = 0.0;
            g["merge"] = false;
            g["stroke"]["width"] = 12.0;
            g["stroke"]["color"] = "#ff0000";
            g["blinker"] = Json{{"mode", "speed"}, {"rate", 1.0}};
        });
        double mn = 1e9, mx = 0.0;
        for (int f = 0; f < 90; f += 6) {
            RunOptions o3 = opt;
            o3.frames = f + 1;
            const double v = px(runEffect(pool, bl::kSliceRefiner3Id, {{"state", blink}}, cam, o3), 80, 150)[0];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        std::printf("  piscar (1 Hz): vermelho da borda varia de %.0f a %.0f\n", mn, mx);
        CHECK(mx > 200.0 && mn < 120.0);
    }

    // Controle remoto pela rede
    {
        const int port = freePort();
        // Desligado por padrao: ninguem escuta
        {
            EffectRig rig(pool, bl::kSliceRefiner3Id, {{"state", redState}, {"remotePort", Json(port)}}, cam);
            for (int i = 0; i < 6; ++i)
                rig.step();
            CHECK(httpRequest(port, "GET", "/").status == 0);
        }
        EffectRig rig(pool, bl::kSliceRefiner3Id, {{"state", redState}, {"remote", Json(true)}, {"remotePort", Json(port)}}, cam);
        const Json blueState = sliceState(ids, 0, [](Json& g) {
            g["masterSize"] = 0.0;
            g["merge"] = false;
            g["stroke"]["width"] = 8.0;
            g["stroke"]["color"] = "#0000ff";
        });
        struct Results
        {
            HttpReply root, slices, post, slices2, screen, bad, notFound, reload;
        } res;
        std::atomic<bool> done{false};
        std::thread client([&] {
            Sleep(300);
            res.root = httpRequest(port, "GET", "/");
            res.slices = httpRequest(port, "GET", "/api/slices");
            res.post = httpRequest(port, "POST", "/api/slices/state", blueState.dump());
            res.slices2 = httpRequest(port, "GET", "/api/slices");
            res.screen = httpRequest(port, "GET", "/api/slices/screen");
            res.bad = httpRequest(port, "POST", "/api/slices/state", "isto nao e json");
            res.notFound = httpRequest(port, "GET", "/nao-existe");
            res.reload = httpRequest(port, "POST", "/api/slices/reload");
            done = true;
        });
        bool polled = false;
        const auto t0 = std::chrono::steady_clock::now();
        while (!done && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(30)) {
            rig.step();
            if (rig.effect->pollParamChanges(rig.values))
                polled = true;
            Sleep(8);
        }
        client.join();
        for (int i = 0; i < 3; ++i)
            rig.step();
        const Img after = rig.read();
        std::printf("  GET / -> %d (%zu bytes, %s) | /api/slices -> %d | POST state -> %d | screen -> %d (%zu bytes, %s)\n", res.root.status,
                    res.root.body.size(), res.root.contentType.c_str(), res.slices.status, res.post.status, res.screen.status, res.screen.body.size(),
                    res.screen.contentType.c_str());
        CHECK(res.root.status == 200 && res.root.contentType.find("text/html") != std::string::npos && res.root.body.size() > 100000 &&
              res.root.body.find("<html") != std::string::npos);
        const Json js = Json::parse(res.slices.body, nullptr, false);
        CHECK(js.is_object() && js.value("slices", Json::array()).size() == 2 && js["comp"]["w"] == 960 && js["comp"]["h"] == 540 &&
              js["state"]["groups"].size() == 8 && js["slices"][0]["name"] == "A" && js["slices"][0]["x"] == 80 && js["slices"][0]["w"] == 320);
        CHECK(res.post.status == 200 && res.post.body.find("\"ok\":true") != std::string::npos);
        const Json js2 = Json::parse(res.slices2.body, nullptr, false);
        CHECK(js2.is_object() && js2["state"]["groups"][0]["stroke"]["color"] == "#0000ff");
        CHECK(res.screen.status == 200 && res.screen.contentType.find("image/jpeg") != std::string::npos && res.screen.body.size() > 500 &&
              static_cast<unsigned char>(res.screen.body[0]) == 0xFF && static_cast<unsigned char>(res.screen.body[1]) == 0xD8);
        CHECK(res.bad.status == 400);
        CHECK(res.notFound.status == 404);
        CHECK(res.reload.status == 200 && Json::parse(res.reload.body, nullptr, false).is_object());
        // A mudanca feita pela rede alterou o parametro do efeito (que e salvo no projeto) e a imagem
        CHECK(polled);
        const Json* st = rig.values.find("state");
        CHECK(st && (*st)["groups"][0]["stroke"]["color"] == "#0000ff");
        CHECK(px(after, 80, 150)[2] > 200 && px(after, 80, 150)[0] < 40);
        // Desligar o controle fecha a porta
        rig.values.set("remote", false);
        rig.applyValues();
        for (int i = 0; i < 4; ++i)
            rig.step();
        Sleep(200);
        CHECK(httpRequest(port, "GET", "/api/slices").status == 0);
    }
    // Porta ocupada: aviso no status, sem travar
    {
        const int port = freePort();
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        SOCKET blocker = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<u_short>(port));
        bind(blocker, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(blocker, 1);
        EffectRig rig(pool, bl::kSliceRefiner3Id, {{"remote", Json(true)}, {"remotePort", Json(port)}}, cam);
        for (int i = 0; i < 4; ++i)
            rig.step();
        std::printf("  porta ocupada: %s\n", rig.effect->status().text.c_str());
        CHECK(rig.effect->status().text.find("ocupada") != std::string::npos);
        closesocket(blocker);
        WSACleanup();
    }
    glDeleteTextures(1, &cam);
    bl::SliceFeed::instance().set({}, 1920, 1080);
}

// ---- Fontes web e PPT -----------------------------------------------------------------------------------------

struct SourceRig
{
    std::unique_ptr<media::ISource> src;
    media::ParamValues values;

    bool open(const std::string& id, const Values& v, int w, int h, std::string* error = nullptr)
    {
        const GeneratorInfo* info = findGenerator(id);
        if (!info)
            return false;
        src = info->create(w, h);
        values.fillDefaults(info->describe(values));
        for (const auto& [k, val] : v)
            values.set(k, val);
        src->setParams(values);
        std::string err;
        const bool ok = src->open({}, &err);
        if (error)
            *error = err;
        return ok;
    }
    void setParam(const std::string& id, Json v)
    {
        values.set(id, std::move(v));
        src->setParams(values);
    }
    void bump(const std::string& id)
    {
        const Json* cur = values.find(id);
        setParam(id, (cur && cur->is_number() ? cur->get<int>() : 0) + 1);
    }
    // update + pollParamChanges como a Composition, por 'seconds'
    void pump(double seconds)
    {
        pumpUntil(seconds, [] { return false; });
    }
    bool pumpUntil(double seconds, const std::function<bool()>& pred)
    {
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds) {
            src->update(1.0 / 60.0);
            src->pollParamChanges(values);
            if (pred())
                return true;
            Sleep(8);
        }
        return false;
    }
    unsigned long long frames() const
    {
        const auto* r = dynamic_cast<const media::RemoteSource*>(src.get());
        return r ? r->framesShown() : 0;
    }
    Img read() const
    {
        glFinish();
        return readTexture(src->texture(), src->width(), src->height());
    }
};

double lumaMean(const Img& a)
{
    double sum = 0.0;
    for (size_t i = 0; i + 3 < a.px.size(); i += 4)
        sum += 0.299 * a.px[i] + 0.587 * a.px[i + 1] + 0.114 * a.px[i + 2];
    return sum / static_cast<double>(a.px.size() / 4);
}

double alphaMean(const Img& a)
{
    double sum = 0.0;
    for (size_t i = 3; i < a.px.size(); i += 4)
        sum += a.px[i];
    return sum / static_cast<double>(a.px.size() / 4);
}

fs::path findTestMedia()
{
    fs::path dir = fs::path(media::WorkerHost::workerExePath()).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        if (fs::exists(dir / L"testmedia" / L"docs" / L"sample.pptx"))
            return dir / L"testmedia";
        dir = dir.parent_path();
    }
    return {};
}

void testWebSources()
{
    section("BSL WebBackgrounds (WBBG) e WebMotion (WB01): pagina no CEF, parametros nativos, controle remoto");
    for (const char* id : {bl::kWebBackgroundsId, bl::kWebMotionId, bl::kPptManagerId}) {
        const GeneratorInfo* g = findGenerator(id);
        CHECK_MSG(g != nullptr, std::string("gerador ") + id);
        if (g) {
            CHECK(!g->describe({}).empty());
            const Clip c = Clip::fromId(id);
            CHECK(!c.empty() && c.generatorId == id && !c.params.empty());
        }
    }
    CHECK(Clip::fromId(bl::kWebBackgroundsId).isWeb() && Clip::fromId(bl::kWebBackgroundsId).isRemote() && Clip::fromId(bl::kWebBackgroundsId).isLive());
    CHECK(Clip::fromId(bl::kPptManagerId).hasPageNav() && !Clip::fromId(bl::kPptManagerId).isLive());
    // Parametros do WBBG: os do painel original, com as mesmas faixas; ajustes por fundo
    {
        const auto descs = bl::describeWebBackgrounds({});
        std::map<std::string, const media::ParamDesc*> by;
        for (const auto& d : descs)
            by[d.id] = &d;
        CHECK(descs.size() > 70);
        std::printf("  parametros: WBBG %zu (Silk) | WB01 %zu | PPT Manager %zu | BSR3 %zu\n", descs.size(), bl::describeWebMotion({}).size(),
                    bl::describePptManager({}).size(), bl::describeSliceRefiner3().size());
        CHECK(by.count("bg_id") && by["bg_id"]->options.size() == 42); // 41 fundos + "nenhum"
        CHECK(by.count("fx_brightness") && by["fx_brightness"]->min == 0.0 && by["fx_brightness"]->max == 2.0);
        CHECK(by.count("fx_blur") && by["fx_blur"]->max == 40.0 && by.count("fx_letterbox") && std::abs(by["fx_letterbox"]->max - 0.35) < 1e-6);
        CHECK(by.count("text_size") && by["text_size"]->min == 2.0 && by["text_size"]->max == 60.0);
        CHECK(by.count("remote") && !by["remote"]->def.get<bool>() && by["remotePort"]->def.get<int>() == 8081);
        CHECK(by.count("bgp_Silk_scale") && by["bgp_Silk_scale"]->max == 5.0); // fundo padrao: Silk
        const auto aurora = bl::describeWebBackgrounds(media::ParamValues(Json{{"bg_id", "Aurora"}}));
        bool hasAurora = false, hasSilk = false;
        for (const auto& d : aurora) {
            hasAurora = hasAurora || d.id == "bgp_Aurora_blend";
            hasSilk = hasSilk || d.id == "bgp_Silk_scale";
        }
        CHECK(hasAurora && !hasSilk);
        const auto motion = bl::describeWebMotion({});
        std::map<std::string, const media::ParamDesc*> mm;
        for (const auto& d : motion)
            mm[d.id] = &d;
        CHECK(mm.count("remote") && mm["remotePort"]->def.get<int>() == 8082 && !mm["remote"]->def.get<bool>());
        CHECK(mm["letterDelay"]->def.get<double>() > 0.099 && mm["letterDelay"]->def.get<double>() < 0.101 && mm["animDuration"]->def.get<double>() == 2.0);
        // Estado da pagina: ida e volta (parametros -> estado -> parametros)
        media::ParamValues v;
        v.fillDefaults(descs);
        v.set("fx_brightness", 1.5);
        v.set("bg_id", "Aurora");
        v.set("bgp_Aurora_blend", 1.25);
        v.set("text_content", "OLA");
        v.set("text_weight", "700");
        v.set("palette_id", "custom");
        v.set("palette_c2", media::param::colorJson({0.0f, 1.0f, 0.0f, 1.0f}));
        const Json state = bl::buildStudioState(media::ParamSet(bl::describeWebBackgrounds(v), v));
        CHECK(state["v"] == 1 && state["fx"]["brightness"] == 1.5 && state["bg"]["id"] == "Aurora" && state["text"]["content"] == "OLA" &&
              state["text"]["weight"] == 700 && state["palette"]["colors"][1] == "#00ff00" && state["bg"]["params"]["Aurora"]["blend"] == 1.25);
        media::ParamValues back;
        back.fillDefaults(descs);
        CHECK(bl::applyStudioState(state, back));
        CHECK(back.find("fx_brightness")->get<double>() == 1.5 && back.find("bg_id")->get<std::string>() == "Aurora" &&
              back.find("text_weight")->get<std::string>() == "700");
        CHECK(!bl::applyStudioState(state, back)); // nada novo
    }

    if (!bl::bslWebAvailable()) {
        std::printf("  (CEF ausente: testes de pagina ignorados)\n");
        return;
    }
    const int workersBefore = workerCount();

    // --- WebBackgrounds ---
    {
        SourceRig rig;
        const int port = freePort();
        std::string error;
        CHECK(rig.open(bl::kWebBackgroundsId, {{"remote", Json(true)}, {"remotePort", Json(port)}, {"fps", Json("30")}}, 640, 360, &error));
        const auto t0 = std::chrono::steady_clock::now();
        const bool got = rig.pumpUntil(90.0, [&] { return rig.frames() > 5 && rig.src->texture() != 0; });
        std::printf("  WBBG: primeiro quadro em %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        CHECK(got);
        rig.pump(4.0); // o WebGL precisa de um tempo para o primeiro desenho
        const Img base = rig.read();
        CHECK(base.w == 640 && base.h == 360);
        CHECK(nonEmpty(base));
        std::printf("  WBBG Silk: luminancia media %.1f alpha %.0f\n", lumaMean(base), alphaMean(base));
        CHECK(alphaMean(base) > 250.0);
        // Mudar um parametro muda a saida: negativo
        rig.setParam("fx_invert", 1.0);
        rig.pump(2.5);
        const Img inverted = rig.read();
        std::printf("  WBBG com negativo: luminancia %.1f (era %.1f)\n", lumaMean(inverted), lumaMean(base));
        CHECK(meanDiff(base, inverted) > 20.0);
        rig.setParam("fx_invert", 0.0);
        // Outro fundo
        rig.setParam("bg_id", "Plasma");
        rig.pump(4.5);
        const Img plasma = rig.read();
        CHECK(nonEmpty(plasma) && meanDiff(base, plasma) > 5.0);
        // Blackout apaga a saida
        rig.setParam("fx_blackout", true);
        rig.pump(2.5);
        const Img black = rig.read();
        std::printf("  WBBG blackout: luminancia %.1f\n", lumaMean(black));
        CHECK(lumaMean(black) < 6.0);
        rig.setParam("fx_blackout", false);
        // Texto no ar
        rig.setParam("text_content", "TESTE");
        rig.setParam("text_visible", true);
        rig.setParam("text_size", 30.0);
        rig.pump(4.0);
        const Img withText = rig.read();
        CHECK(meanDiff(plasma, withText) > 1.0);
        // Fundo transparente: bg "nenhum" + transparent
        rig.setParam("bg_id", "none");
        rig.setParam("text_visible", false);
        rig.setParam("transparent", true);
        rig.pump(5.0);
        const Img clear = rig.read();
        std::printf("  WBBG transparente: alpha medio %.1f\n", alphaMean(clear));
        CHECK(alphaMean(clear) < 20.0);
        rig.setParam("transparent", false);
        rig.setParam("bg_id", "Silk");
        rig.pump(3.0);

        // Controle remoto: a mesma API e o mesmo painel
        const HttpReply root = httpRequest(port, "GET", "/");
        CHECK(root.status == 200 && root.body.size() > 100000 && root.body.find("<html") != std::string::npos);
        const HttpReply app = httpRequest(port, "GET", "/app");
        CHECK(app.status == 200 && app.body.size() > 100000);
        HttpReply st = httpRequest(port, "GET", "/api/state");
        Json sj = Json::parse(st.body, nullptr, false);
        CHECK(st.status == 200 && sj.is_object() && sj["state"].is_object() && sj["state"]["bg"]["id"] == "Silk" && sj["rev"].get<int>() >= 1);
        const int rev0 = sj.is_object() ? sj["rev"].get<int>() : 0;
        // Painel manda cmd=state (corpo form) -> vira parametros do clip
        const Json patch = {{"bg", {{"id", "Galaxy"}, {"speed", 1.25}}}, {"fx", {{"vignette", 0.6}}}, {"text", {{"content", "REMOTO"}}}};
        auto postAction = [&](const std::string& form) { return httpRequest(port, "POST", "/api/action", form); };
        const HttpReply ok = postAction("cmd=state&val=" + net::urlEncode(patch.dump()));
        CHECK(ok.status == 200 && ok.body.find("\"status\":\"ok\"") != std::string::npos);
        rig.pumpUntil(5.0, [&] { return rig.values.find("bg_id") && *rig.values.find("bg_id") == "Galaxy"; });
        CHECK(rig.values.find("bg_id") && *rig.values.find("bg_id") == "Galaxy");
        CHECK(rig.values.find("bg_speed") && std::abs(rig.values.find("bg_speed")->get<double>() - 1.25) < 1e-6);
        CHECK(rig.values.find("fx_vignette") && std::abs(rig.values.find("fx_vignette")->get<double>() - 0.6) < 1e-6);
        CHECK(rig.values.find("text_content") && *rig.values.find("text_content") == "REMOTO");
        // Comandos antigos
        postAction("cmd=bg&val=Aurora");
        postAction("cmd=logo_size&val=21");
        postAction("cmd=logo_toggle&val=1");
        postAction("cmd=logo_effect&val=glow");
        rig.pumpUntil(5.0, [&] { return rig.values.find("text_loop") && *rig.values.find("text_loop") == "breathe"; });
        CHECK(*rig.values.find("bg_id") == "Aurora" && rig.values.find("text_size")->get<double>() == 21.0 &&
              rig.values.find("text_visible")->get<bool>() && *rig.values.find("text_loop") == "breathe");
        rig.pump(1.0);
        st = httpRequest(port, "GET", "/api/state");
        sj = Json::parse(st.body, nullptr, false);
        CHECK(sj.is_object() && sj["rev"].get<int>() > rev0 && sj["state"]["bg"]["id"] == "Aurora" && sj["state"]["text"]["visible"] == true);
        // Previa JPEG
        std::atomic<bool> done{false};
        HttpReply shot;
        std::thread t([&] {
            shot = httpRequest(port, "GET", "/api/screen");
            done = true;
        });
        rig.pumpUntil(15.0, [&] { return done.load(); });
        t.join();
        CHECK(shot.status == 200 && shot.contentType.find("image/jpeg") != std::string::npos && shot.body.size() > 1000);
        // Logo pelo painel (data URL) vira arquivo no parametro logoFile
        const std::string tinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
        postAction("cmd=logo_url&val=" + net::urlEncode("data:image/png;base64," + tinyPng));
        rig.pumpUntil(5.0, [&] { return rig.values.find("logoFile") && !rig.values.find("logoFile")->get<std::string>().empty(); });
        const std::string logoPath = rig.values.find("logoFile") ? rig.values.find("logoFile")->get<std::string>() : std::string();
        CHECK(!logoPath.empty() && fs::exists(fs::path(std::u8string(logoPath.begin(), logoPath.end()))));
        CHECK(rig.values.find("text_mode") && *rig.values.find("text_mode") == "logo");
        const HttpReply logo = httpRequest(port, "GET", "/api/logo");
        CHECK(logo.status == 200 && logo.body.rfind("data:image/png;base64,", 0) == 0);
        // Desligar fecha a porta
        rig.setParam("remote", false);
        rig.pump(0.5);
        Sleep(200);
        CHECK(httpRequest(port, "GET", "/api/state").status == 0);
        rig.src->close();
    }

    // --- WebMotion ---
    {
        SourceRig rig;
        const int port = freePort();
        CHECK(rig.open(bl::kWebMotionId,
                       {{"remote", Json(true)}, {"remotePort", Json(port)}, {"fps", Json("30")}, {"textColor", media::param::colorJson({1, 1, 1, 1})}, {"textSize", Json(30.0)}},
                       640, 360));
        const bool got = rig.pumpUntil(90.0, [&] { return rig.frames() > 0 && rig.src->texture() != 0; });
        CHECK(got);
        rig.pump(1.5);
        const Img idle = rig.read();
        std::printf("  WB01 parado: luminancia %.1f\n", lumaMean(idle));
        // "Projetar texto" (cmd=text do painel 8082, que no plugin respondia not_found)
        rig.setParam("text", "ABC");
        rig.bump("show");
        rig.pump(4.5);
        const Img shown = rig.read();
        std::printf("  WB01 com texto: luminancia %.1f\n", lumaMean(shown));
        CHECK(nonEmpty(shown) && lumaMean(shown) > lumaMean(idle) + 3.0);
        // Apagar tela (fade out)
        rig.bump("clear");
        rig.pump(3.0);
        const Img cleared = rig.read();
        CHECK(lumaMean(cleared) < lumaMean(shown) - 2.0);
        // Atraso entre letras: com 0,5 s as letras ainda estao entrando 1 s depois; sem atraso ja estao todas
        rig.setParam("textAnim", "zoom");
        rig.setParam("letterDelay", 0.5);
        rig.bump("show");
        rig.pump(1.0);
        const Img slow = rig.read();
        rig.setParam("letterDelay", 0.0);
        rig.setParam("animDuration", 0.3);
        rig.bump("show");
        rig.pump(1.0);
        const Img fast = rig.read();
        std::printf("  WB01 atraso 0,5 s: %.1f | sem atraso: %.1f\n", lumaMean(slow), lumaMean(fast));
        CHECK(lumaMean(fast) > lumaMean(slow) + 1.0);
        // Imagem com transparencia: a pagina separa as letras pelas colunas vazias (o plugin nunca chamava receiveImage)
        {
            const int iw = 360, ih = 120;
            std::vector<unsigned char> png(static_cast<size_t>(iw) * static_cast<size_t>(ih) * 4, 0);
            for (int blockIndex = 0; blockIndex < 3; ++blockIndex)
                for (int y = 20; y < 100; ++y)
                    for (int x = 20 + blockIndex * 120; x < 100 + blockIndex * 120; ++x) {
                        unsigned char* p = &png[(static_cast<size_t>(y) * static_cast<size_t>(iw) + static_cast<size_t>(x)) * 4];
                        p[0] = p[1] = p[2] = p[3] = 255;
                    }
            const fs::path file = fs::temp_directory_path() / L"bsl-bsltest-letras.png";
            CHECK(stbi_write_png(reinterpret_cast<const char*>(file.u8string().c_str()), iw, ih, 4, png.data(), iw * 4) != 0);
            rig.bump("clear");
            rig.pump(2.5);
            const Img before = rig.read();
            rig.setParam("letterDelay", 0.1);
            rig.setParam("animDuration", 0.6);
            rig.setParam("image", std::string(reinterpret_cast<const char*>(file.u8string().c_str())));
            rig.pump(4.0);
            const Img letters = rig.read();
            std::printf("  WB01 imagem por letras: luminancia %.1f (antes %.1f)\n", lumaMean(letters), lumaMean(before));
            CHECK(lumaMean(letters) > lumaMean(before) + 2.0);
            std::error_code rmEc;
            fs::remove(file, rmEc);
            rig.setParam("image", "");
            rig.bump("clear");
            rig.pump(2.0);
        }
        // Controle remoto
        const HttpReply root = httpRequest(port, "GET", "/");
        CHECK(root.status == 200 && root.body.find("MOTION STUDIO") != std::string::npos);
        const HttpReply act = httpRequest(port, "GET", "/api/action?cmd=text&val=" + net::urlEncode("Ol\xC3\xA1") + "&anim=zoom");
        CHECK(act.status == 200 && act.body.find("\"ok\"") != std::string::npos);
        rig.pumpUntil(5.0, [&] { return rig.values.find("text") && *rig.values.find("text") == "Ol\xC3\xA1"; });
        CHECK(*rig.values.find("text") == "Ol\xC3\xA1" && *rig.values.find("textAnim") == "zoom");
        httpRequest(port, "GET", "/api/action?cmd=logo&val=" + net::urlEncode("https://exemplo.com/logo.png"));
        httpRequest(port, "GET", "/api/action?cmd=clear");
        httpRequest(port, "GET", "/api/load?url=" + net::urlEncode("about:blank"));
        rig.pumpUntil(5.0, [&] { return rig.values.find("url") && *rig.values.find("url") == "about:blank"; });
        CHECK(*rig.values.find("logo") == "https://exemplo.com/logo.png" && rig.values.find("showLogo")->get<int>() >= 1 && rig.values.find("clear")->get<int>() >= 2);
        CHECK(*rig.values.find("url") == "about:blank");
        rig.src->close();
    }

    // Nenhum worker fica sobrando (o encerramento acontece em segundo plano: espera ate 20 s)
    const bool gone = waitWorkers(workersBefore, 20.0);
    const int workersAfter = workerCount();
    std::printf("  workers: %d antes, %d depois\n", workersBefore, workersAfter);
    CHECK(gone && workersAfter <= workersBefore);
}

void testPptManager()
{
    section("BSL PPT Manager (BSPT): PPTX pelo worker (PowerPoint/LibreOffice + PDFium), navegacao entre slides");
    const fs::path testmedia = findTestMedia();
    const fs::path pptx = testmedia.empty() ? fs::path() : testmedia / L"docs" / L"sample.pptx";
    if (pptx.empty() || !fs::exists(pptx)) {
        std::printf("  (testmedia/docs/sample.pptx nao encontrado: ignorado)\n");
        return;
    }
    const std::string doc(reinterpret_cast<const char*>(pptx.u8string().c_str()));
    // Sem documento: nao abre e explica
    {
        SourceRig rig;
        std::string error;
        CHECK(!rig.open(bl::kPptManagerId, {}, 640, 360, &error));
        CHECK(error.find("documento") != std::string::npos);
    }
    {
        const auto descs = bl::describePptManager({});
        std::set<std::string> ids;
        for (const auto& d : descs)
            ids.insert(d.id);
        for (const char* id : {"document", "prev", "next", "reload", "page", "bg", "autoMode", "autoSeconds", "autoBeats", "loopEnd", "transition", "fadeSeconds"})
            CHECK_MSG(ids.count(id) > 0, std::string("parametro do PPT Manager: ") + id);
    }
    SourceRig rig;
    const int workersBefore = workerCount();
    std::string error;
    CHECK(rig.open(bl::kPptManagerId, {{"document", Json(doc)}}, 640, 360, &error));
    auto* pdf = dynamic_cast<media::PdfSource*>(rig.src.get());
    CHECK(pdf != nullptr);
    if (!pdf)
        return;
    const auto t0 = std::chrono::steady_clock::now();
    const bool got = rig.pumpUntil(120.0, [&] { return rig.frames() > 0 && rig.src->texture() != 0 && pdf->pageCount() > 0; });
    std::printf("  primeiro slide em %.1f s | paginas: %d | transporte: %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                pdf->pageCount(), pdf->transportName().c_str());
    CHECK(got);
    if (!got) {
        std::printf("  status: %s\n", rig.src->status().text.c_str());
        return;
    }
    CHECK(pdf->pageCount() == 4);
    rig.pump(0.5);
    const Img first = rig.read();
    CHECK(nonEmpty(first) && first.w == 640 && first.h == 360);
    auto shownPage = [&] { return pdf->shownPage(); };
    CHECK(pdf->page() == 1);
    // Proximo / anterior
    rig.bump("next");
    CHECK(rig.pumpUntil(30.0, [&] { return shownPage() == 2; }));
    const Img second = rig.read();
    CHECK(pdf->page() == 2 && meanDiff(first, second) > 10.0);
    rig.bump("next");
    CHECK(rig.pumpUntil(30.0, [&] { return shownPage() == 3; }));
    rig.bump("prev");
    CHECK(rig.pumpUntil(30.0, [&] { return shownPage() == 2; }));
    // Indice do slide (o "Slide Index" do plugin)
    rig.setParam("page", 4);
    CHECK(rig.pumpUntil(30.0, [&] { return shownPage() == 4; }));
    const Img fourth = rig.read();
    CHECK(meanDiff(first, fourth) > 10.0 && meanDiff(second, fourth) > 10.0);
    rig.setParam("page", 1);
    CHECK(rig.pumpUntil(30.0, [&] { return shownPage() == 1; }));
    rig.pump(0.3);
    CHECK(meanDiff(first, rig.read()) < 2.0);
    // Avanco automatico (do PdfSource)
    rig.setParam("autoMode", "seconds");
    rig.setParam("autoSeconds", 0.6);
    CHECK(rig.pumpUntil(30.0, [&] { return shownPage() >= 2; }));
    rig.setParam("autoMode", "off");
    // Recarregar reabre o documento
    rig.bump("reload");
    rig.pump(0.3);
    CHECK(rig.pumpUntil(90.0, [&] { return rig.frames() > 0 && rig.src->texture() != 0 && pdf->pageCount() == 4; }));
    rig.src->close();
    rig.src.reset();
    const bool gone = waitWorkers(workersBefore, 20.0);
    std::printf("  workers: %d antes, %d depois\n", workersBefore, workerCount());
    CHECK(gone && workerCount() <= workersBefore);
}

// ---------------------------------------------------------------------------------------------------- comparacao com os plugins originais

// Roda o plugin FFGL original (somente leitura) e o nativo e grava <nome>.png com original | nativo lado a lado
bool savePng(const fs::path& file, const Img& left, const Img& right)
{
    const int w = left.w + (right.w > 0 ? right.w + 8 : 0), h = std::max(left.h, right.h);
    std::vector<unsigned char> out(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 40);
    auto blit = [&](const Img& img, int x0) {
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                const unsigned char* s = img.at(x, y);
                unsigned char* d = &out[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x0 + x)) * 4];
                // sobre um fundo quadriculado, para ver o alpha
                const bool chk = ((x / 16) + (y / 16)) % 2 == 0;
                const float bg = chk ? 60.0f : 90.0f;
                const float a = s[3] / 255.0f;
                d[0] = static_cast<unsigned char>(s[0] * a + bg * (1 - a));
                d[1] = static_cast<unsigned char>(s[1] * a + bg * (1 - a));
                d[2] = static_cast<unsigned char>(s[2] * a + bg * (1 - a));
                d[3] = 255;
            }
    };
    blit(left, 0);
    if (right.w > 0)
        blit(right, left.w + 8);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return stbi_write_png(reinterpret_cast<const char*>(file.u8string().c_str()), w, h, 4, out.data(), w * 4) != 0;
}

struct OriginalRunner
{
    ffgl::EntryPtr entry;
    std::unique_ptr<ffgl::FfglInstance> inst;
};

// Roda o original pelo host FFGL: 'values' usa os ids dos parametros do plugin (ver imprimirParametros)
Img runOriginal(FboPool& pool, const ffgl::EntryPtr& entry, const Values& values, GLuint input, int w, int h, int frames,
                bool printParams = false)
{
    ffgl::FfglInstance inst(entry);
    if (printParams) {
        std::printf("  parametros de %s:", entry->info.fileName.c_str());
        for (const auto& d : inst.paramDescs())
            std::printf(" %s", d.id.c_str());
        std::printf("\n");
    }
    media::ParamValues v;
    for (const auto& [k, val] : values)
        v.set(k, val);
    inst.setParams(media::ParamSet(inst.paramDescs(), v));
    Fbo out(w, h);
    for (int f = 0; f < frames; ++f) {
        fx::EffectContext ctx;
        ctx.time = f / 60.0;
        ctx.dt = 1.0 / 60.0;
        ctx.frameIndex = static_cast<std::uint64_t>(f);
        ctx.bpm = 128.0f;
        ctx.canvasWidth = w;
        ctx.canvasHeight = h;
        ctx.topOrigin = true;
        ctx.pool = &pool;
        inst.process(ctx, entry->info.type == ffgl::kTypeSource ? 0 : input, w, h, out);
        if (f % 10 == 0)
            Sleep(16);
    }
    if (inst.faulted() || !inst.status().text.empty())
        std::printf("  [original %s] %s%s\n", entry->info.fileName.c_str(), inst.faulted() ? "FALHOU: " : "", inst.faulted() ? inst.faultReason().c_str() : inst.status().text.c_str());
    glFinish();
    return readTexture(out.texture(), w, h);
}

// --- Shader original como referencia ---------------------------------------------------------------------------
// O fragment/vertex shader original e lido do codigo-fonte do plugin (somente leitura) e rodado aqui com os mesmos
// uniforms, com MaxUV = 1 e a imagem em OpenGL (linha 0 = base), como o host Resolume faz.

std::string extractShader(const fs::path& cpp, const char* marker)
{
    std::ifstream in(cpp, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t m = text.find(marker);
    if (m == std::string::npos)
        return {};
    const size_t b = text.find("R\"(", m);
    if (b == std::string::npos)
        return {};
    const size_t e = text.find(")\";", b);
    if (e == std::string::npos)
        return {};
    return text.substr(b + 3, e - b - 3);
}

struct Uniforms
{
    GLuint prog;
    void f(const char* n, float v) const { glUniform1f(glGetUniformLocation(prog, n), v); }
    void i(const char* n, int v) const { glUniform1i(glGetUniformLocation(prog, n), v); }
    void v2(const char* n, float x, float y) const { glUniform2f(glGetUniformLocation(prog, n), x, y); }
    void v3(const char* n, float x, float y, float z) const { glUniform3f(glGetUniformLocation(prog, n), x, y, z); }
};

// Desenha o shader original num FBO w x h e devolve a imagem na orientacao do BSL Stage (linha 0 = topo) em alpha reto
bool runReference(const std::string& vs, const std::string& fsrc, int w, int h, const std::function<void(const Uniforms&)>& setUniforms, Img& out)
{
    if (vs.empty() || fsrc.empty())
        return false;
    try {
        Shader sh(vs.c_str(), fsrc.c_str());
        static const float verts[] = {-1, -1, 0, 1, 0, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1};
        GLuint vao = 0, vbo = 0;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
        Fbo fbo(w, h);
        fbo.bind();
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        sh.use();
        setUniforms(Uniforms{sh.id()});
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glUseProgram(0);
        glBindVertexArray(0);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glFinish();
        Img gl = readTexture(fbo.texture(), w, h); // linha 0 = base (OpenGL)
        Fbo::unbind();
        out = flipV(gl);
        for (size_t i = 0; i + 3 < out.px.size(); i += 4) { // premultiplicado -> reto
            const int a = out.px[i + 3];
            if (a > 0 && a < 255)
                for (int c = 0; c < 3; ++c)
                    out.px[i + static_cast<size_t>(c)] = static_cast<unsigned char>(std::min(255, out.px[i + static_cast<size_t>(c)] * 255 / a));
        }
        return true;
    } catch (const std::exception& e) {
        std::printf("  shader original nao compilou: %s\n", e.what());
        return false;
    }
}

// Reduz 2x (media 2x2) para os PNGs de comparacao em 1920x1080
Img downscale2x(const Img& a)
{
    Img o;
    o.w = a.w / 2;
    o.h = a.h / 2;
    o.px.resize(static_cast<size_t>(o.w) * static_cast<size_t>(o.h) * 4);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x)
            for (int c = 0; c < 4; ++c) {
                const int sum = a.at(2 * x, 2 * y)[c] + a.at(2 * x + 1, 2 * y)[c] + a.at(2 * x, 2 * y + 1)[c] + a.at(2 * x + 1, 2 * y + 1)[c];
                o.px[(static_cast<size_t>(y) * static_cast<size_t>(o.w) + static_cast<size_t>(x)) * 4 + static_cast<size_t>(c)] = static_cast<unsigned char>(sum / 4);
            }
    return o;
}

bool g_motionOnly = false;

int runCompare(const fs::path& outDir, FboPool& pool, const fs::path& srcDir)
{
    section("comparacao com os plugins FFGL originais (somente leitura)");
    char* home = nullptr;
    size_t homeLen = 0;
    _dupenv_s(&home, &homeLen, "USERPROFILE");
    const fs::path profile(home ? home : "");
    std::free(home);
    const fs::path extra = profile / L"Documents" / L"Resolume Arena" / L"Extra Effects";
    ffgl::Catalog& cat = ffgl::Catalog::instance();
    cat.setCachePath(fs::temp_directory_path() / L"bsl-bsltest-compare-cache.json");
    cat.clearCache();
    cat.setScanTimeoutMs(15000);
    cat.configure({{extra, "Resolume Extra Effects"}});
    cat.rescan();
    cat.waitIdle(300000);
    auto find = [&](const char* file) -> ffgl::EntryPtr {
        for (const ffgl::EntryPtr& e : cat.entries())
            if (e->info.fileName == file)
                return e;
        return nullptr;
    };
    std::printf("  plugins no Extra Effects: %zu\n", cat.entries().size());

    int rc = 0;
    auto report2 = [&](const char* what, double d, double limit) {
        const bool ok = d <= limit;
        std::printf("  %-34s diferenca media %.3f %s\n", what, d, ok ? "" : "  <-- ACIMA DO LIMITE");
        if (!ok)
            rc = 1;
    };

    // --- WebMotion sozinho (--compare-motion): no Resolume ele falha junto do WebBackgrounds por causa do libcef ---
    if (g_motionOnly) {
        if (const ffgl::EntryPtr wm = find("WebMotion.dll")) {
            const int w = 960, h = 540;
            ffgl::FfglInstance inst(wm);
            Fbo out(w, h);
            fx::EffectContext ctx;
            ctx.canvasWidth = w;
            ctx.canvasHeight = h;
            ctx.topOrigin = true;
            ctx.pool = &pool;
            const auto t0 = std::chrono::steady_clock::now();
            for (int f = 0; f < 600 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15); ++f) {
                ctx.time = f / 60.0;
                ctx.frameIndex = static_cast<std::uint64_t>(f);
                inst.process(ctx, 0, w, h, out);
                Sleep(16);
            }
            glFinish();
            const Img o = readTexture(out.texture(), w, h);
            SourceRig rig;
            rig.open(bl::kWebMotionId, {{"fps", Json("60")}}, w, h);
            rig.pumpUntil(60.0, [&] { return rig.frames() > 0; });
            rig.pump(3.0);
            const Img idle = rig.read();
            std::printf("  WebMotion: luminancia media original %.1f | nativo %.1f (a pagina mostra so o aviso, sobre preto)\n", lumaMean(o), lumaMean(idle));
            report2("WebMotion luminancia media", std::abs(lumaMean(o) - lumaMean(idle)) / 255.0 * 100.0, 6.0);
            savePng(outDir / "wb01-web-motion.png", o, idle);
            // O que o plugin nao fazia: o painel mandava cmd=text e a rota nao existia. Nativo, com texto animado:
            rig.setParam("text", "BSL LIGHT");
            rig.setParam("textSize", 14.0);
            rig.bump("show");
            rig.pump(5.0);
            savePng(outDir / "wb01-web-motion-texto.png", rig.read(), Img{});
            rig.src->close();
        } else {
            std::printf("  WebMotion.dll nao esta no Extra Effects\n");
        }
        return rc;
    }

    // --- LEDMapperPro: o plugin instalado, pelo host FFGL ---
    if (const ffgl::EntryPtr led = find("LEDMapperPro.dll")) {
        const int w = 960, h = 540;
        struct Cfg
        {
            const char* name;
            int cols, rows;
            float cw, ch, gx, gy;
        };
        for (const Cfg& c : {Cfg{"led-3x2-gap", 3, 2, 200, 150, 12, 8}, Cfg{"led-5x1", 5, 1, 160, 160, 0, 0}, Cfg{"led-4x4", 4, 4, 100, 100, 6, 6},
                             Cfg{"led-1x1", 1, 1, 400, 300, 0, 0}}) {
            // O plugin guarda 0..1: colunas = 1 + int(v*49), tamanho = v*2000, espaco = v*100
            Values orig = {{"columns", Json((c.cols - 1) / 49.0 + 0.002)}, {"rows", Json((c.rows - 1) / 49.0 + 0.002)},
                           {"cabinet_width", Json(c.cw / 2000.0)}, {"cabinet_height", Json(c.ch / 2000.0)},
                           {"gap_x", Json(c.gx / 100.0)}, {"gap_y", Json(c.gy / 100.0)}};
            const Img o = runOriginal(pool, led, orig, 0, w, h, 4);
            const Img n = runSource(bl::kLedMapperId, {{"columns", Json(c.cols)}, {"rows", Json(c.rows)}, {"cabinetW", Json(c.cw)}, {"cabinetH", Json(c.ch)}, {"gapX", Json(c.gx)}, {"gapY", Json(c.gy)}}, 2, w, h);
            report2((std::string("LEDMapperPro ") + c.name).c_str(), meanDiff(o, n), 0.5);
            savePng(outDir / (std::string(c.name) + ".png"), o, n);
        }
    } else {
        std::printf("  LEDMapperPro.dll nao esta no Extra Effects\n");
    }

    // --- SliceRefiner2: o plugin instalado, pelo host FFGL ---
    // O original nunca define o uniform MaxUV (o vertex shader faz uv = vUV * MaxUV, que fica 0): ele nao desenha nada em
    // host nenhum. A conferencia visual usa entao o shader original (lido do codigo-fonte) com MaxUV = 1.
    std::vector<bl::FeedSlice> xmlSlices;
    {
        std::ifstream in(bl::defaultResolumeSliceXmlPath(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        xmlSlices = bl::parseResolumeSlices(text);
        std::printf("  AdvancedOutput.xml do Resolume: %zu slice(s)\n", xmlSlices.size());
    }
    if (const ffgl::EntryPtr bss = find("BSL LIGHT SliceRefiner2.dll")) {
        const int w = 1920, h = 1080;
        const GLuint cam = makeCamera(false, true, w, h);
        Values orig = {{"load_custom_xml", Json(bl::defaultResolumeSliceXmlPath())}};
        const Img o = runOriginal(pool, bss, orig, cam, w, h, 3);
        size_t lit = 0;
        for (size_t i = 3; i < o.px.size(); i += 4)
            lit += o.px[i] > 0 ? 1 : 0;
        std::printf("  [original BSS2 pelo host FFGL] pixels com alpha: %zu (MaxUV nunca definido: nao desenha)\n", lit);
        glDeleteTextures(1, &cam);

        const std::string vs = extractShader(srcDir / "SliceRefiner2" / "SliceRefiner2.cpp", "_vertexShaderCode[]");
        const std::string fsrc = extractShader(srcDir / "SliceRefiner2" / "SliceRefiner2.cpp", "_fragmentShaderCode[]");
        const char* modeIds[] = {"neon", "gradient", "radar", "chaser", "bumper", "video", "sparkles", "glow", "rgbwave", "dotted", "glitch"};
        for (int mode = 0; mode <= 10; ++mode) {
            if (mode == 5)
                continue; // mascara de video precisa da imagem; coberta abaixo com a camera
            const float T = 1.5f;
            Img ref;
            const bool ran = runReference(vs, fsrc, w, h, [&](const Uniforms& u) {
                u.i("InputTexture", 0);
                u.v2("MaxUV", 1.0f, 1.0f);
                u.v2("Resolution", static_cast<float>(w), static_cast<float>(h));
                u.f("Time", T);
                for (int i = 0; i < 8; ++i) {
                    const std::string p = "slices[" + std::to_string(i) + "].";
                    const bool has = static_cast<size_t>(i) < xmlSlices.size();
                    u.f((p + "active").c_str(), has ? 1.0f : 0.0f);
                    u.f((p + "mode").c_str(), static_cast<float>(mode));
                    u.f((p + "roundness").c_str(), 0.25f);
                    u.f((p + "stroke").c_str(), 0.1f); // o shader multiplica por 100 px
                    u.f((p + "speed").c_str(), 0.5f);
                    u.v3((p + "color").c_str(), 0.0f, 1.0f, 1.0f);
                    if (has) {
                        const bl::FeedSlice& s = xmlSlices[static_cast<size_t>(i)];
                        glUniform4f(glGetUniformLocation(u.prog, (p + "rect").c_str()), s.x, s.y, s.w, s.h);
                    }
                }
            }, ref);
            if (!ran) {
                rc = 1;
                continue;
            }
            Values nat = {{"source", Json("xml")}};
            for (int i = 1; i <= 8; ++i) {
                nat.push_back({"s" + std::to_string(i) + "Mode", Json(modeIds[mode])});
                nat.push_back({"s" + std::to_string(i) + "Roundness", Json(25.0)});
            }
            RunOptions opt;
            opt.w = w;
            opt.h = h;
            opt.canvasW = w;
            opt.canvasH = h;
            opt.frames = 1;
            opt.timeBase = T;
            const GLuint blank = makeTexture(w, h, [](int, int, unsigned char* p) { p[0] = p[1] = p[2] = 0; p[3] = 255; });
            const Img n = runEffect(pool, bl::kSliceRefinerId, nat, blank, opt);
            glDeleteTextures(1, &blank);
            report2((std::string("SliceRefiner2 shader ") + modeIds[mode]).c_str(), meanDiff(ref, n), 0.6);
            if (mode == 0 || mode == 1 || mode == 3 || mode == 8)
                savePng(outDir / (std::string("slice-refiner-") + modeIds[mode] + ".png"), downscale2x(ref), downscale2x(n));
        }
    } else {
        std::printf("  BSL LIGHT SliceRefiner2.dll nao esta no Extra Effects\n");
    }

    // --- Etapa B: BSLSliceRefiner (BSR3) e WebBackgrounds, os plugins instalados, pelo host FFGL ---
    // BSR3: o original le o estado de BSL_STATE_DIR\state.json e os slices de BSL_SLICE_XML (ganchos de teste do plugin); o
    // nativo recebe o mesmo estado (parametro "state") e o mesmo XML (origem "xml"). Estado sem animacao: borda de cor solida.
    if (const ffgl::EntryPtr bsr = find("BSL LIGHT Slice Refiner.dll")) {
        const int w = 960, h = 540;
        const fs::path dir = fs::temp_directory_path() / L"bsl-bsltest-bsr3-compare";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const double ang = 20.0 * std::numbers::pi / 180.0;
        auto rot = [&](double lx, double ly) {
            return std::pair<double, double>{680 + lx * std::cos(ang) - ly * std::sin(ang), 330 + lx * std::sin(ang) + ly * std::cos(ang)};
        };
        const auto q0 = rot(-90, -60), q1 = rot(90, -60), q2 = rot(90, 60), q3 = rot(-90, 60);
        char xml[4096];
        std::snprintf(xml, sizeof(xml),
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ScreenSetup name=\"ScreenSetup\">\n<CurrentCompositionTextureSize width=\"960\" height=\"540\"/>\n<screens>\n"
                      "<Screen name=\"Palco\" uniqueId=\"1\"><Params name=\"Params\"><Param name=\"Name\" T=\"STRING\" default=\"\" value=\"Palco\"/></Params><layers>\n"
                      "<Slice uniqueId=\"2\"><Params name=\"Common\"><Param name=\"Name\" T=\"STRING\" default=\"Layer\" value=\"Esquerda\"/></Params><InputRect orientation=\"0\"><v x=\"60\" y=\"80\"/><v x=\"260\" y=\"80\"/><v x=\"260\" y=\"300\"/><v x=\"60\" y=\"300\"/></InputRect></Slice>\n"
                      "<Slice uniqueId=\"3\"><Params name=\"Common\"><Param name=\"Name\" T=\"STRING\" default=\"Layer\" value=\"Direita\"/></Params><InputRect orientation=\"0\"><v x=\"260\" y=\"80\"/><v x=\"460\" y=\"80\"/><v x=\"460\" y=\"300\"/><v x=\"260\" y=\"300\"/></InputRect></Slice>\n"
                      "<Slice uniqueId=\"4\"><Params name=\"Common\"><Param name=\"Name\" T=\"STRING\" default=\"Layer\" value=\"Girado\"/></Params><InputRect orientation=\"0\"><v x=\"%.3f\" y=\"%.3f\"/><v x=\"%.3f\" y=\"%.3f\"/><v x=\"%.3f\" y=\"%.3f\"/><v x=\"%.3f\" y=\"%.3f\"/></InputRect></Slice>\n"
                      "</layers></Screen></screens></ScreenSetup>\n",
                      q0.first, q0.second, q1.first, q1.second, q2.first, q2.second, q3.first, q3.second);
        {
            std::ofstream out(dir / L"AdvancedOutput.xml", std::ios::binary);
            out << xml;
        }
        const std::string xmlPath(reinterpret_cast<const char*>((dir / L"AdvancedOutput.xml").u8string().c_str()));
        const std::vector<bl::FeedSlice> sl = bl::parseResolumeSlices(xml);
        Json state = bl::defaultSliceStateJson();
        if (sl.size() == 3) {
            state["assign"][sl[0].id] = 0;
            state["assign"][sl[1].id] = 0;
            state["assign"][sl[2].id] = 1;
        }
        Json& g0 = state["groups"][0];
        g0["masterSize"] = 0.5;
        g0["corners"] = Json::array({0.8, 0.8, 0.8, 0.8});
        g0["offset"] = -2;
        g0["mergeSmooth"] = 40;
        g0["stroke"]["width"] = 6;
        g0["stroke"]["color"] = "#ff3030";
        g0["stroke"]["glow"] = 0.4;
        Json& g1 = state["groups"][1];
        g1["merge"] = false;
        g1["masterSize"] = 0.3;
        g1["offset"] = 8;
        g1["softEdge"] = 0.15;
        g1["stroke"]["width"] = 5;
        g1["stroke"]["color"] = "#30c0ff";
        g1["stroke"]["dual"] = Json{{"on", true}, {"width", 3}, {"gap", 7}};
        {
            std::ofstream out(dir / L"state.json", std::ios::binary);
            out << state.dump();
        }
        _putenv_s("BSL_STATE_DIR", reinterpret_cast<const char*>(dir.u8string().c_str()));
        _putenv_s("BSL_SLICE_XML", xmlPath.c_str());
        const GLuint cam = makeCamera(false, true, w, h);
        // O host FFGL do app cuida da orientacao (linha 0 = topo) e do alpha premultiplicado; a camera e opaca
        const Img o = runOriginal(pool, bsr, {}, cam, w, h, 12, true);
        {
            const Values nat = {{"source", Json("xml")}, {"xmlFile", Json(xmlPath)}, {"state", state}};
            RunOptions opt;
            opt.w = w;
            opt.h = h;
            opt.canvasW = w;
            opt.canvasH = h;
            opt.frames = 12;
            const Img n = runEffect(pool, bl::kSliceRefiner3Id, nat, cam, opt);
            report2("BSLSliceRefiner (BSR3) estado + XML identicos", meanDiff(o, n), 1.2);
            savePng(outDir / "bsr3-slice-refiner.png", o, n);
        }
        glDeleteTextures(1, &cam);
        _putenv_s("BSL_STATE_DIR", "");
        _putenv_s("BSL_SLICE_XML", "");
    } else {
        std::printf("  BSL LIGHT Slice Refiner.dll nao esta no Extra Effects\n");
    }

    // WebBackgrounds: o original roda a pagina em localhost:8081 (CEF no proprio processo); o nativo roda a mesma pagina na
    // fonte Web do app. Os fundos animam com o relogio: compara-se o conjunto (luminancia e cor media), nao pixel a pixel.
    if (const ffgl::EntryPtr wbbg = find("WebBackgrounds.dll")) {
        const int w = 960, h = 540;
        ffgl::FfglInstance inst(wbbg);
        Fbo out(w, h);
        fx::EffectContext ctx;
        ctx.canvasWidth = w;
        ctx.canvasHeight = h;
        ctx.topOrigin = true;
        ctx.pool = &pool;
        const auto t0 = std::chrono::steady_clock::now();
        Img o;
        for (int f = 0; f < 900 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(25); ++f) {
            ctx.time = f / 60.0;
            ctx.frameIndex = static_cast<std::uint64_t>(f);
            inst.process(ctx, 0, w, h, out);
            Sleep(16);
        }
        glFinish();
        o = readTexture(out.texture(), w, h);
        // O original entrega a imagem do CEF (BGRA do navegador) na orientacao do host
        SourceRig rig;
        rig.open(bl::kWebBackgroundsId, {{"fps", Json("60")}}, w, h);
        rig.pumpUntil(60.0, [&] { return rig.frames() > 5; });
        rig.pump(8.0);
        const Img n = rig.read();
        const double lumaO = lumaMean(o), lumaN = lumaMean(n);
        std::printf("  WebBackgrounds: luminancia media original %.1f | nativo %.1f\n", lumaO, lumaN);
        auto meanColor = [](const Img& img, int c) {
            double sum = 0;
            for (size_t i = static_cast<size_t>(c); i < img.px.size(); i += 4)
                sum += img.px[i];
            return sum / static_cast<double>(img.px.size() / 4);
        };
        std::printf("  cor media original (%.0f,%.0f,%.0f) | nativo (%.0f,%.0f,%.0f)\n", meanColor(o, 0), meanColor(o, 1), meanColor(o, 2),
                    meanColor(n, 0), meanColor(n, 1), meanColor(n, 2));
        report2("WebBackgrounds luminancia media", std::abs(lumaO - lumaN) / 255.0 * 100.0, 8.0);
        savePng(outDir / "wbbg-web-backgrounds.png", o, n);
        rig.src->close();
    } else {
        std::printf("  WebBackgrounds.dll nao esta no Extra Effects\n");
    }

    // --- Os demais: o shader original (lido do codigo-fonte) contra o nativo, no primeiro quadro, sem audio ---
    {
        // GenBackgrounds: 22 estilos, mesmos uniforms
        const std::string vs = extractShader(srcDir / "GenBackgrounds" / "GenBackgrounds.cpp", "vertexShaderCode[]");
        const std::string fsrc = extractShader(srcDir / "GenBackgrounds" / "GenBackgrounds.cpp", "fragmentShaderCode[]");
        const int w = 480, h = 270;
        double worst = 0.0;
        const auto descs = bl::describeGenBackgrounds();
        const media::ParamDesc* style = nullptr;
        for (const auto& d : descs)
            if (d.id == "style")
                style = &d;
        for (int st = 0; style && st < 22; ++st) {
            Img ref;
            const float T = 1.0f;
            if (!runReference(vs, fsrc, w, h, [&](const Uniforms& u) {
                    u.f("iTime", T);
                    u.i("iStyle", st);
                    u.f("iSpeed", 0.5f);
                    u.f("iScale", 0.5f);
                    u.v3("iColor1", 0.0f, 1.0f, 1.0f); // HSB (matiz 0, sat 1, brilho 1)
                    u.v3("iColor2", 0.5f, 1.0f, 1.0f);
                    u.v3("iColor3", 0.0f, 0.0f, 0.0f);
                    u.v2("iResolution", static_cast<float>(w), static_cast<float>(h));
                    u.f("iDetail", 0.5f);
                    u.f("iDensity", 0.5f);
                    u.f("iSoftness", 0.5f);
                    u.f("iDistortion", 0.0f);
                    u.f("iRotation", 0.0f);
                }, ref)) {
                rc = 1;
                break;
            }
            const Img n = runSource(bl::kGenBackgroundsId, {{"style", Json(style->options[static_cast<size_t>(st)].value)}}, 60, w, h);
            const double d = meanDiff(ref, n);
            worst = std::max(worst, d);
            if (d > 0.8)
                std::printf("    estilo %d (%s): diferenca %.3f\n", st, style->options[static_cast<size_t>(st)].value.c_str(), d);
            if (st == 0 || st == 4 || st == 8 || st == 14)
                savePng(outDir / ("bg-" + style->options[static_cast<size_t>(st)].value + ".png"), ref, n);
        }
        report2("GenBackgrounds shader (22 estilos, pior)", worst, 0.8);
    }
    {
        // Efeitos de um quadro: AlphaReactor, Datamosh e AudioReactiveTypo com a camera em OpenGL (linha 0 = base) e alpha premultiplicado
        const int w = 960, h = 540;
        const float T = 2.0f;
        const Img camImg = makeCameraImg(false, true, w, h);
        const Img cutImg = makeCameraImg(true, true, w, h);
        // As mesmas imagens em OpenGL: linha 0 = base
        const GLuint camGl = makeTexture(w, h, [&](int x, int y0, unsigned char* p) {
            const unsigned char* s = camImg.at(x, h - 1 - y0);
            p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = s[3];
        });
        const GLuint cutGl = makeTexture(w, h, [&](int x, int y0, unsigned char* p) {
            const unsigned char* s = cutImg.at(x, h - 1 - y0);
            p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = s[3];
        });
        const GLuint camTop = makeCamera(false, true, w, h);
        const GLuint cutTop = makeCamera(true, true, w, h);
        const GLuint black = makeTexture(8, 8, [](int, int, unsigned char* p) { p[0] = p[1] = p[2] = 0; p[3] = 0; });
        auto bindAll = [&](GLuint in, GLuint typo, GLuint fb) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, in);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, typo);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, fb);
            glActiveTexture(GL_TEXTURE0);
        };
        RunOptions opt;
        opt.w = w;
        opt.h = h;
        opt.canvasW = w;
        opt.canvasH = h;
        opt.frames = 1;
        opt.timeBase = T;

        // Datamosh (sort vertical, neon, protecao); realimentacao vazia no primeiro quadro
        {
            const std::string p = "DatamoshPixelSort";
            const std::string vs = extractShader(srcDir / p / (p + ".cpp"), "_vertexShaderCode[]");
            const std::string fsrc = extractShader(srcDir / p / (p + ".cpp"), "_fragmentShaderCode[]");
            Img ref;
            const bool ran = runReference(vs, fsrc, w, h, [&](const Uniforms& u) {
                bindAll(camGl, camGl, black);
                u.i("InputTexture", 0);
                u.i("FeedbackTexture", 2);
                u.v2("MaxUV", 1.0f, 1.0f);
                u.v2("Resolution", static_cast<float>(w), static_cast<float>(h));
                u.f("Time", T);
                u.f("SortThreshold", 0.45f);
                u.f("SortLength", 0.6f);
                u.f("SortDirection", 0.5f);
                u.f("MoshAmount", 0.4f);
                u.f("MoshDecay", 0.5f);
                u.f("ProtectRadius", 0.18f);
                u.f("ProtectFeather", 0.2f);
                u.f("ProtectX", 0.4f);
                u.f("ProtectY", 0.3f);
                u.f("NeonEdges", 0.35f);
                u.f("NeonR", 1.0f);
                u.f("NeonG", 0.2f);
                u.f("NeonB", 0.0f);
                u.f("GhostBlur", 0.3f);
                u.f("BassLevel", 0.0f);
                u.f("HighLevel", 0.0f);
            }, ref);
            const Img n = runEffect(pool, bl::kDatamoshId,
                                    {{"sortThreshold", Json(45.0)}, {"sortLength", Json(60.0)}, {"sortDirection", Json("vertical")}, {"moshAmount", Json(40.0)},
                                     {"protectRadius", Json(18.0)}, {"protectFeather", Json(20.0)}, {"protectX", Json(40.0)}, {"protectY", Json(30.0)},
                                     {"neonEdges", Json(35.0)}, {"neonColor", media::param::colorJson({1.0f, 0.2f, 0.0f, 1.0f})}, {"ghostBlur", Json(30.0)}},
                                    camTop, opt);
            if (ran) {
                report2("Datamosh shader (1 quadro)", meanDiff(ref, n), 0.8);
                savePng(outDir / "datamosh.png", ref, n);
            } else {
                rc = 1;
            }
        }
        // AlphaReactor
        {
            const std::string p = "AlphaReactor";
            const std::string vs = extractShader(srcDir / p / (p + ".cpp"), "_vertexShaderCode[]");
            const std::string fsrc = extractShader(srcDir / p / (p + ".cpp"), "_fragmentShaderCode[]");
            const GLuint cutPre = makeTexture(w, h, [&](int x, int y0, unsigned char* p2) { // premultiplicado, linha 0 = base
                const unsigned char* s = cutImg.at(x, h - 1 - y0);
                p2[0] = static_cast<unsigned char>(s[0] * s[3] / 255);
                p2[1] = static_cast<unsigned char>(s[1] * s[3] / 255);
                p2[2] = static_cast<unsigned char>(s[2] * s[3] / 255);
                p2[3] = s[3];
            });
            Img ref;
            const bool ran = runReference(vs, fsrc, w, h, [&](const Uniforms& u) {
                bindAll(cutPre, cutPre, black);
                u.i("InputTexture", 0);
                u.i("FeedbackTexture", 2);
                u.v2("MaxUV", 1.0f, 1.0f);
                u.v2("Resolution", static_cast<float>(w), static_cast<float>(h));
                u.f("Time", T);
                u.f("AuraIntensity", 0.8f);
                u.f("AuraLength", 0.7f);
                u.f("TrailLength", 0.8f);
                u.f("TrailScale", 0.5f);
                u.f("TrailRGBSplit", 0.5f);
                u.f("BassLevel", 0.0f);
                u.f("HighLevel", 0.0f);
            }, ref);
            glDeleteTextures(1, &cutPre);
            const Img n = runEffect(pool, bl::kAlphaReactorId, {{"auraIntensity", Json(80.0)}, {"auraLength", Json(70.0)}}, cutTop, opt);
            if (ran) {
                report2("AlphaReactor shader (1 quadro)", meanDiff(ref, n), 0.8);
                savePng(outDir / "alpha-reactor.png", ref, n);
            } else {
                rc = 1;
            }
        }
        // AudioReactiveTypo: 4 modos, sem texto (bitmap transparente) para nao depender do rasterizador
        {
            const std::string p = "AudioReactiveTypo";
            const std::string vs = extractShader(srcDir / p / (p + ".cpp"), "_vertexShaderCode[]");
            const std::string fsrc = extractShader(srcDir / p / (p + ".cpp"), "_fragmentShaderCode[]");
            const GLuint empty = makeTexture(64, 64, [](int, int, unsigned char* q) { q[0] = q[1] = q[2] = 255; q[3] = 0; });
            const char* modes[] = {"boiler", "marquee", "glitch", "mask"};
            const char* blends[] = {"difference", "overlay", "add", "cutout"};
            for (int m = 0; m < 4; ++m) {
                Img ref;
                const bool ran = runReference(vs, fsrc, w, h, [&](const Uniforms& u) {
                    bindAll(camGl, empty, black);
                    u.i("InputTexture", 0);
                    u.i("TypoTexture", 1);
                    u.i("FeedbackTexture", 2);
                    u.v2("MaxUV", 1.0f, 1.0f);
                    u.v2("Resolution", static_cast<float>(w), static_cast<float>(h));
                    u.f("AspectRatio", static_cast<float>(w) / static_cast<float>(h));
                    u.f("Time", T);
                    u.f("Mode", m * 0.25f + 0.01f);
                    u.f("BlendMode", m * 0.25f + 0.01f);
                    u.f("TextScale", 1.075f);
                    u.f("PosY", 0.48f);
                    u.f("Tracking", 0.15f);
                    u.f("BassLevel", 0.0f);
                    u.f("HighLevel", 0.0f);
                    u.f("GlitchTrigger", 0.0f);
                    u.f("KickShake", 0.45f);
                    u.f("RGBSplit", 0.35f);
                    u.f("ScrollSpeed", 0.4f);
                    u.f("BpmSync", 0.5f);
                    u.f("RepeatCount", 0.35f);
                    u.f("GhostBlur", 0.0f);
                    u.f("BWRaveGrade", 0.85f);
                    u.f("Scanlines", 0.3f);
                    u.f("VignetteGrain", 0.0f); // o grao e um hash de sin() em valores grandes: nao bate bit a bit entre compilacoes
                    u.f("HudElements", 1.0f);
                    u.v3("TextColor", 1.0f, 1.0f, 1.0f);
                    u.f("MasterInvert", 0.0f);
                }, ref);
                const Img n = runEffect(pool, bl::kTypoId, {{"mode", Json(modes[m])}, {"blendMode", Json(blends[m])}, {"djText", Json("")}, {"subText", Json("")}, {"vignetteGrain", Json(0.0)}}, camTop, opt);
                if (ran) {
                    report2((std::string("AudioReactiveTypo shader ") + modes[m]).c_str(), meanDiff(ref, n), 0.8);
                    savePng(outDir / (std::string("arty-") + modes[m] + ".png"), ref, n);
                } else {
                    rc = 1;
                }
            }
            // Orientacao do texto no original: bitmap enviado de cima para baixo (como o GDI), quarto superior branco
            {
                const GLuint typo = makeTexture(256, 256, [](int, int y, unsigned char* q) { q[0] = q[1] = q[2] = 255; q[3] = y < 64 ? 255 : 0; });
                const GLuint blackCam = makeTexture(8, 8, [](int, int, unsigned char* q) { q[0] = q[1] = q[2] = 0; q[3] = 255; });
                Img ref;
                const bool ran = runReference(vs, fsrc, w, h, [&](const Uniforms& u) {
                    bindAll(blackCam, typo, black);
                    u.i("InputTexture", 0);
                    u.i("TypoTexture", 1);
                    u.i("FeedbackTexture", 2);
                    u.v2("MaxUV", 1.0f, 1.0f);
                    u.v2("Resolution", static_cast<float>(w), static_cast<float>(h));
                    u.f("AspectRatio", static_cast<float>(w) / static_cast<float>(h));
                    u.f("Time", 0.0f);
                    u.f("Mode", 0.0f);
                    u.f("BlendMode", 0.3f);
                    u.f("TextScale", 1.0f);
                    u.f("PosY", 0.5f);
                    u.f("RepeatCount", 0.35f);
                    u.v3("TextColor", 1.0f, 1.0f, 1.0f);
                    u.f("BWRaveGrade", 0.0f);
                    u.f("HudElements", 0.0f);
                }, ref);
                if (ran) {
                    size_t top = 0, bottom = 0;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                            if (ref.at(x, y)[0] > 128)
                                (y < h / 2 ? top : bottom)++;
                    std::printf("  ARTY original: o quarto superior do bitmap (GDI, de cima para baixo) aparece na %s da tela (px: topo %zu, base %zu)\n",
                                bottom > top ? "BASE" : "PARTE DE CIMA", top, bottom);
                }
                glDeleteTextures(1, &typo);
                glDeleteTextures(1, &blackCam);
            }
            glDeleteTextures(1, &empty);
        }
        for (GLuint t : {camGl, cutGl, camTop, cutTop, black})
            glDeleteTextures(1, &t);
    }
    return rc;
}

// Imagens do nativo para conferir a olho (--dump <pasta>)
// ---------------------------------------------------------------------------------------------------- VHS e efeitos de IA

// Textura R16F (profundidade) ou R8 (mascara) sintetica, linha 0 = topo
GLuint makeDepthTexture(int w, int h, const std::function<float(int, int)>& f)
{
    std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = f(x, y);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint makeMaskTexture(int w, int h, const std::function<float(int, int)>& f)
{
    std::vector<unsigned char> data(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<unsigned char>(std::clamp(f(x, y), 0.0f, 1.0f) * 255.0f + 0.5f);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

constexpr std::uint64_t kAiTestSource = 424242;

// Efeito de IA com profundidade/mascara sinteticas no lugar da inferencia (AiEngine::setOverride)
Img runAiEffect(FboPool& pool, const std::string& typeId, const Values& values, GLuint input, GLuint depthTex, GLuint maskTex, int w, int h,
                int frames = 1, bool top = true, GLuint altInput = 0, std::string* statusOut = nullptr)
{
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.setOverride(kAiTestSource, depthTex, maskTex);
    std::unique_ptr<fx::IEffect> effect = fx::EffectRegistry::instance().create(typeId);
    if (!effect) {
        report(false, "efeito nao criado: " + typeId);
        return {};
    }
    media::ParamValues v;
    for (const auto& [k, val] : values)
        v.set(k, val);
    effect->setParams(media::ParamSet(effect->paramDescs(), v));
    Fbo out(w, h);
    for (int f = 0; f < frames; ++f) {
        fx::EffectContext ctx;
        ctx.time = f / 60.0;
        ctx.dt = 1.0 / 60.0;
        ctx.frameIndex = static_cast<std::uint64_t>(f);
        ctx.canvasWidth = w;
        ctx.canvasHeight = h;
        ctx.topOrigin = top;
        ctx.pool = &pool;
        ctx.sourceId = kAiTestSource;
        effect->process(ctx, (altInput != 0 && (f & 1)) ? altInput : input, w, h, out);
    }
    if (statusOut)
        *statusOut = effect->status().text;
    glFinish();
    eng.setOverride(kAiTestSource, 0, 0);
    return readTexture(out.texture(), w, h);
}

// Media da luminancia (canal verde) de uma faixa vertical da imagem
double columnMean(const Img& img, double x0, double x1, int channel = 1)
{
    double sum = 0.0;
    size_t n = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = static_cast<int>(x0 * img.w); x < static_cast<int>(x1 * img.w); ++x) {
            sum += img.at(x, y)[channel];
            ++n;
        }
    return n ? sum / static_cast<double>(n) : 0.0;
}

// Diferenca media absoluta numa faixa vertical
double columnDiff(const Img& a, const Img& b, double x0, double x1)
{
    double sum = 0.0;
    size_t n = 0;
    for (int y = 0; y < a.h; ++y)
        for (int x = static_cast<int>(x0 * a.w); x < static_cast<int>(x1 * a.w); ++x)
            for (int c = 0; c < 4; ++c) {
                sum += std::abs(static_cast<int>(a.at(x, y)[c]) - static_cast<int>(b.at(x, y)[c]));
                ++n;
            }
    return n ? sum / static_cast<double>(n) : 0.0;
}

Img runOnce(FboPool& pool, const std::string& typeId, const Values& values, GLuint input, int w, int h, double time, double beat, bool top = true)
{
    RunOptions opt;
    opt.frames = 1;
    opt.w = w;
    opt.h = h;
    opt.topOrigin = top;
    opt.timeBase = time;
    (void)beat;
    return runEffect(pool, typeId, values, input, opt);
}

void testVhs(FboPool& pool)
{
    section("BSL VHS: muda a imagem, presets, intensidade, texto, BPM, orientacao");
    CHECK(fx::EffectRegistry::instance().find(bl::kVhsId).has_value());
    CHECK(!fx::EffectRegistry::instance().find(bl::kVhsId)->ai); // VHS e nativo, sem IA
    CHECK(fx::EffectRegistry::instance().find(bl::kVhsId)->section == fx::EffectSection::Bsl);
    CHECK(fx::EffectRegistry::instance().find(bl::kRelightId)->ai && fx::EffectRegistry::instance().find(bl::kBodyGlitchId)->ai);

    const GLuint cam = makeCamera(false);
    const Img src = readTexture(cam, kW, kH);

    // O padrao muda a imagem
    const Img def = runOnce(pool, bl::kVhsId, {}, cam, kW, kH, 2.0, 0.0);
    CHECK(nonEmpty(def));
    const double dDef = meanDiff(def, src);
    std::printf("  VHS padrao x entrada: dif media %.2f\n", dDef);
    CHECK_MSG(dDef > 3.0, "o VHS muda a imagem");

    // Cada preset gera uma saida diferente (mesmo instante)
    std::vector<Img> presets;
    for (const char* p : {"worn", "cam90", "glitch", "subtle"})
        presets.push_back(runOnce(pool, bl::kVhsId, {{"preset", Json(p)}}, cam, kW, kH, 2.0, 0.0));
    for (size_t i = 0; i < presets.size(); ++i)
        for (size_t j = i + 1; j < presets.size(); ++j) {
            const double d = meanDiff(presets[i], presets[j]);
            CHECK_MSG(d > 0.8, "presets " + std::to_string(i) + " e " + std::to_string(j) + " diferentes (dif " + std::to_string(d) + ")");
        }
    // "Sutil" e o mais proximo da entrada
    CHECK(meanDiff(presets[3], src) < meanDiff(presets[0], src) && meanDiff(presets[3], src) < meanDiff(presets[1], src) &&
          meanDiff(presets[3], src) < meanDiff(presets[2], src));
    std::printf("  dif vs entrada: fita gasta %.1f | camera 90s %.1f | glitch forte %.1f | sutil %.1f\n", meanDiff(presets[0], src),
                meanDiff(presets[1], src), meanDiff(presets[2], src), meanDiff(presets[3], src));

    // Intensidade 0 ~ entrada
    const Img off = runOnce(pool, bl::kVhsId, {{"intensity", Json(0.0)}}, cam, kW, kH, 2.0, 0.0);
    CHECK_MSG(meanDiff(off, src) < 0.5, "intensidade 0 = entrada (dif " + std::to_string(meanDiff(off, src)) + ")");
    // Todos os defeitos em 0 % com intensidade 100 tambem ~ entrada
    const Img none = runOnce(pool, bl::kVhsId,
                             {{"wobble", Json(0.0)}, {"tracking", Json(0.0)}, {"noise", Json(0.0)}, {"chroma", Json(0.0)}, {"color", Json(0.0)},
                              {"vignette", Json(0.0)}, {"scanlines", Json(0.0)}, {"dropout", Json(0.0)}},
                             cam, kW, kH, 2.0, 0.0);
    CHECK_MSG(meanDiff(none, src) < 1.5, "todos os defeitos em 0 % = entrada (dif " + std::to_string(meanDiff(none, src)) + ")");
    // Intensidade intermediaria fica entre as duas
    const Img half = runOnce(pool, bl::kVhsId, {{"intensity", Json(50.0)}}, cam, kW, kH, 2.0, 0.0);
    const double dHalf = meanDiff(half, src);
    CHECK_MSG(dHalf > 0.2 * dDef && dHalf < 0.9 * dDef, "intensidade 50 % fica entre 0 e 100 %");

    // Cada defeito, sozinho, muda a imagem
    const Values zero = {{"wobble", Json(0.0)}, {"tracking", Json(0.0)}, {"noise", Json(0.0)}, {"chroma", Json(0.0)}, {"color", Json(0.0)},
                         {"vignette", Json(0.0)}, {"scanlines", Json(0.0)}, {"dropout", Json(0.0)}};
    for (const char* knob : {"wobble", "tracking", "noise", "chroma", "color", "vignette", "scanlines", "dropout"}) {
        Values v = zero;
        for (auto& kv : v)
            if (kv.first == knob)
                kv.second = Json(200.0);
        const Img one = runOnce(pool, bl::kVhsId, v, cam, kW, kH, 2.0, 0.0);
        const double d = meanDiff(one, none);
        std::printf("  so %-10s a 200 %%: dif %.2f\n", knob, d);
        CHECK_MSG(d > 0.05, std::string("o controle \"") + knob + "\" tem efeito");
    }

    // Texto PLAY e data/hora: aparecem nos cantos
    {
        Values z = zero;
        const Img plain = runOnce(pool, bl::kVhsId, z, cam, kW, kH, 2.0, 0.0);
        z.push_back({"playText", Json(true)});
        const Img play = runOnce(pool, bl::kVhsId, z, cam, kW, kH, 2.0, 0.0);
        Values z2 = zero;
        z2.push_back({"dateStamp", Json(true)});
        const Img stamp = runOnce(pool, bl::kVhsId, z2, cam, kW, kH, 2.0, 0.0);
        auto cornerDiff = [&](const Img& a, const Img& b, int x0, int x1, int y0, int y1) {
            double s = 0;
            size_t n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    for (int c = 0; c < 3; ++c) {
                        s += std::abs(static_cast<int>(a.at(x, y)[c]) - static_cast<int>(b.at(x, y)[c]));
                        ++n;
                    }
            return s / static_cast<double>(n);
        };
        const double playTL = cornerDiff(play, plain, 0, kW / 3, 0, kH / 4);
        const double playBR = cornerDiff(play, plain, kW * 2 / 3, kW, kH * 3 / 4, kH);
        const double stampBR = cornerDiff(stamp, plain, kW * 2 / 3, kW, kH * 3 / 4, kH);
        const double stampTL = cornerDiff(stamp, plain, 0, kW / 3, 0, kH / 4);
        std::printf("  PLAY: canto sup. esq. %.2f (dir. inf. %.2f) | data/hora: canto inf. dir. %.2f (sup. esq. %.2f)\n", playTL, playBR, stampBR, stampTL);
        CHECK_MSG(playTL > 1.0 && playBR < 0.05, "PLAY aparece so no canto superior esquerdo");
        CHECK_MSG(stampBR > 1.0 && stampTL < 0.05, "data e hora aparecem so no canto inferior direito");
    }

    // Tracking no BPM: com a sincronia ligada o quadro muda com a batida (mesmo ctx.time)
    {
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kVhsId);
        media::ParamValues v;
        v.set("bpmSync", Json(true));
        v.set("tracking", Json(200.0));
        e->setParams(media::ParamSet(e->paramDescs(), v));
        Fbo out(kW, kH);
        auto at = [&](double beat) {
            fx::EffectContext ctx;
            ctx.time = 3.0;
            ctx.dt = 1.0 / 60.0;
            ctx.beat = beat;
            ctx.pool = &pool;
            e->process(ctx, cam, kW, kH, out);
            glFinish();
            return readTexture(out.texture(), kW, kH);
        };
        const Img b0 = at(8.02), b1 = at(8.5), b2 = at(9.02);
        CHECK_MSG(meanDiff(b0, b1) > 1.0, "tracking sincronizado: a batida muda o quadro");
        // o strobe vem logo depois da batida: o quadro em 8.02 e mais claro que em 8.5
        CHECK_MSG(columnMean(b0, 0.0, 1.0) > columnMean(b1, 0.0, 1.0) + 1.0, "strobe de tracking no inicio da batida");
        // mesma fase da batida = mesmo quadro
        CHECK_MSG(meanDiff(b0, b2) < 6.0, "mesma fase da batida, quadro parecido");
    }

    // Orientacao: a mesma imagem de cabeca para baixo (topOrigin = false) da o mesmo resultado, invertido
    {
        const GLuint camFlip = makeCamera(false, false);
        const Img a = runOnce(pool, bl::kVhsId, {{"playText", Json(true)}, {"dateStamp", Json(true)}}, cam, kW, kH, 2.0, 0.0, true);
        const Img b = runOnce(pool, bl::kVhsId, {{"playText", Json(true)}, {"dateStamp", Json(true)}}, camFlip, kW, kH, 2.0, 0.0, false);
        const double d = meanDiff(a, flipV(b));
        CHECK_MSG(d < 1.0, "orientacao coerente (dif " + std::to_string(d) + ")");
        glDeleteTextures(1, &camFlip);
    }
    glDeleteTextures(1, &cam);
}

void testRelight(FboPool& pool)
{
    section("BSL Relight 2.5D: normais da profundidade, luzes 3D, depuracao");
    const auto descs = bl::describeRelight();
    CHECK(descs.size() == 5 + 4 * 10);
    // luz: ligada, X/Y/Z, cor, intensidade, raio/falloff, especular e brilho nas 4 luzes
    for (int i = 1; i <= 4; ++i)
        for (const char* k : {"on", "x", "y", "z", "color", "intensity", "radius", "falloff", "spec", "shine"}) {
            const std::string id = "l" + std::to_string(i) + "_" + k;
            bool found = false;
            for (const auto& d : descs)
                found = found || d.id == id;
            CHECK_MSG(found, "parametro " + id);
        }

    const int w = 640, h = 360;
    // entrada cinza uniforme; profundidade = rampa horizontal (perto na direita)
    const GLuint gray = makeTexture(w, h, [](int, int, unsigned char* p) { p[0] = p[1] = p[2] = 120; p[3] = 255; });
    const GLuint ramp = makeDepthTexture(64, 36, [](int x, int) { return static_cast<float>(x) / 63.0f; });
    const GLuint flat = makeDepthTexture(64, 36, [](int, int) { return 0.5f; });
    const Img src = readTexture(gray, w, h);

    auto relight = [&](GLuint depth, const Values& v, bool top = true) { return runAiEffect(pool, bl::kRelightId, v, gray, depth, 0, w, h, 1, top); };
    const Values leftLight = {{"l1_on", Json(true)}, {"l1_x", Json(5.0)}, {"l1_y", Json(50.0)}, {"l1_z", Json(40.0)},
                              {"l1_color", media::param::colorJson({1, 1, 1, 1})}, {"l1_intensity", Json(150.0)}, {"l1_radius", Json(150.0)},
                              {"ambient", Json(10.0)}};
    Values rightLight = leftLight;
    for (auto& kv : rightLight)
        if (kv.first == "l1_x")
            kv.second = Json(95.0);

    const Img L = relight(ramp, leftLight);
    const Img R = relight(ramp, rightLight);
    CHECK(nonEmpty(L));
    const double lL = columnMean(L, 0.0, 0.33), rL = columnMean(L, 0.67, 1.0);
    const double lR = columnMean(R, 0.0, 0.33), rR = columnMean(R, 0.67, 1.0);
    std::printf("  luz a esquerda: lado esq. %.0f | lado dir. %.0f ; luz a direita: esq. %.0f | dir. %.0f\n", lL, rL, lR, rR);
    CHECK_MSG(lL > rL + 10.0, "a luz a esquerda clareia o lado esquerdo");
    CHECK_MSG(rR > lR + 10.0, "a luz a direita clareia o lado direito");
    // com profundidade plana a posicao da luz tambem manda (queda pela distancia)
    const Img Lf = relight(flat, leftLight);
    CHECK_MSG(columnMean(Lf, 0.0, 0.33) > columnMean(Lf, 0.67, 1.0) + 10.0, "profundidade plana: a luz a esquerda ainda clareia o lado esquerdo");

    // Luzes apagadas: so o ambiente (25 % de 120)
    Values dark = {{"l1_on", Json(false)}};
    const Img D = relight(ramp, dark);
    const double dm = columnMean(D, 0.0, 1.0);
    CHECK_MSG(std::abs(dm - 30.0) < 4.0, "sem luzes, so o ambiente: " + std::to_string(dm));
    // Duas luzes (esq. e dir.) iluminam os dois lados
    Values two = leftLight;
    two.push_back({"l2_on", Json(true)});
    two.push_back({"l2_x", Json(95.0)});
    two.push_back({"l2_y", Json(50.0)});
    two.push_back({"l2_z", Json(40.0)});
    two.push_back({"l2_intensity", Json(150.0)});
    two.push_back({"l2_radius", Json(150.0)});
    two.push_back({"l2_color", media::param::colorJson({1, 1, 1, 1})});
    const Img T = relight(ramp, two);
    CHECK_MSG(columnMean(T, 0.67, 1.0) > rL + 15.0 && columnMean(T, 0.0, 0.33) > lL - 5.0, "duas luzes: os dois lados iluminados");
    // Cor da luz: luz vermelha deixa o vermelho dominante
    Values red = leftLight;
    for (auto& kv : red)
        if (kv.first == "l1_color")
            kv.second = media::param::colorJson({1, 0.1f, 0.1f, 1});
    const Img Rd = relight(ramp, red);
    CHECK_MSG(columnMean(Rd, 0.0, 0.33, 0) > 1.5 * columnMean(Rd, 0.0, 0.33, 1), "a cor da luz tinge a imagem");
    // Intensidade 0 = so ambiente
    Values zeroInt = leftLight;
    for (auto& kv : zeroInt)
        if (kv.first == "l1_intensity")
            kv.second = Json(0.0);
    CHECK_MSG(std::abs(columnMean(relight(ramp, zeroInt), 0.0, 0.33) - 12.0) < 3.0, "intensidade 0: so ambiente (10 %)");
    // Mistura 0 = original
    Values mix0 = leftLight;
    mix0.push_back({"mix", Json(0.0)});
    CHECK_MSG(meanDiff(relight(ramp, mix0), src) < 0.5, "mistura 0 = original");
    // Relevo: a rampa inclina as normais para a esquerda (R < 128) e o plano deixa-as de frente (128, 128, 255)
    const Img nRamp = relight(ramp, {{"debug", Json("normal")}});
    const Img nFlat = relight(flat, {{"debug", Json("normal")}});
    const double nr = columnMean(nRamp, 0.3, 0.7, 0), nf = columnMean(nFlat, 0.3, 0.7, 0);
    std::printf("  normais (R): rampa %.0f | plano %.0f ; B plano %.0f\n", nr, nf, columnMean(nFlat, 0.3, 0.7, 2));
    CHECK_MSG(nr < 118.0 && std::abs(nf - 128.0) < 4.0 && columnMean(nFlat, 0.3, 0.7, 2) > 250.0, "mapa de normais por derivadas");
    // Depuracao: profundidade = a rampa (escura a esquerda, clara a direita)
    const Img dd = relight(ramp, {{"debug", Json("depth")}});
    CHECK_MSG(columnMean(dd, 0.0, 0.2) < 60.0 && columnMean(dd, 0.8, 1.0) > 190.0, "ver profundidade mostra a rampa");
    // Intensidade do relevo: mais relevo, normais mais inclinadas
    const Img n4 = relight(ramp, {{"debug", Json("normal")}, {"bump", Json(400.0)}});
    CHECK_MSG(columnMean(n4, 0.3, 0.7, 0) < nr - 5.0, "intensidade do relevo inclina as normais");
    // Orientacao: entrada com a linha 0 na base (topOrigin = false) com a profundidade ainda de cima para baixo
    const Img Lb = relight(ramp, leftLight, false);
    CHECK_MSG(columnMean(Lb, 0.0, 0.33) > columnMean(Lb, 0.67, 1.0) + 10.0, "topOrigin = false: luz a esquerda no lado esquerdo");
    // Sem a IA pronta (sourceId sem override, perfil ligado, runtime pode nao ter terminado): a imagem passa direto
    {
        ai::AiEngine& eng = ai::AiEngine::instance();
        eng.setProfileRate(0); // Eco
        std::string status;
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kRelightId);
        e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
        Fbo out(w, h);
        fx::EffectContext ctx;
        ctx.pool = &pool;
        ctx.sourceId = kAiTestSource + 1;
        e->process(ctx, gray, w, h, out);
        glFinish();
        CHECK_MSG(meanDiff(readTexture(out.texture(), w, h), src) < 0.01, "IA desligada: a imagem passa direto");
        CHECK_MSG(e->status().text == "IA desligada no perfil Eco", "status: " + e->status().text);
        eng.setProfileRate(15);
    }
    glDeleteTextures(1, &gray);
    glDeleteTextures(1, &ramp);
    glDeleteTextures(1, &flat);
}

void testBodyGlitch(FboPool& pool)
{
    section("BSL Cybernetic Body-Parts Glitch: regiao pessoa E perto, 4 modos");
    const int w = 640, h = 360;
    const GLuint cam = makeCamera(false);
    const GLuint camB = makeCamera(true); // outra imagem (movimento para o datamosh)
    const Img src = readTexture(cam, w, h);
    // mascara: metade esquerda e pessoa; profundidade: perto em toda a imagem exceto o terco superior
    const GLuint mask = makeMaskTexture(64, 36, [](int x, int) { return x < 32 ? 1.0f : 0.0f; });
    const GLuint nearD = makeDepthTexture(64, 36, [](int, int) { return 0.9f; });
    const GLuint farD = makeDepthTexture(64, 36, [](int, int) { return 0.1f; });
    CHECK(bl::describeBodyGlitch().size() > 20);

    struct Mode
    {
        const char* name;
        Values values;
        int frames;
    };
    const Mode modes[] = {
        {"datamosh", {{"mode", Json("datamosh")}, {"moshAmount", Json(100.0)}}, 12},
        {"clones", {{"mode", Json("clones")}, {"cloneOffset", Json(0.0)}, {"cloneCount", Json(3)}, {"cloneDelay", Json(2)}}, 12},
        {"wireframe", {{"mode", Json("wireframe")}}, 3},
        {"slicer", {{"mode", Json("slicer")}, {"sliceShift", Json(25.0)}}, 3},
        {"slicer-grade", {{"mode", Json("slicer")}, {"sliceGrid", Json(true)}, {"sliceShift", Json(25.0)}}, 3},
    };
    for (const Mode& m : modes) {
        std::string st;
        const Img out = runAiEffect(pool, bl::kBodyGlitchId, m.values, cam, nearD, mask, w, h, m.frames, true, std::string(m.name) == "datamosh" ? camB : 0, &st);
        // fora da mascara (metade direita) a imagem fica igual a entrada; o datamosh alterna as entradas, entao compara-se
        // com a entrada do ultimo quadro (par/impar)
        const Img& ref = (std::string(m.name) == "datamosh" && ((m.frames - 1) & 1)) ? Img(readTexture(camB, w, h)) : src;
        const double outside = columnDiff(out, ref, 0.55, 1.0);
        const double inside = columnDiff(out, ref, 0.0, 0.45);
        std::printf("  %-12s: fora da mascara dif %.3f | dentro dif %.2f | status \"%s\"\n", m.name, outside, inside, st.c_str());
        CHECK_MSG(outside < 0.01, std::string(m.name) + ": fora da mascara a imagem fica igual a entrada");
        if (std::string(m.name) != "clones")
            CHECK_MSG(inside > 1.0, std::string(m.name) + ": dentro da mascara o glitch muda a imagem");
        CHECK(st.empty());
    }
    // Clones com deslocamento: os rastros aparecem (a imagem muda); sem deslocamento ficam sobre a propria pessoa
    {
        const Img still = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("clones")}, {"cloneOffset", Json(8.0)}, {"cloneDelay", Json(2)}, {"tint", Json(100.0)}}, cam, nearD, mask, w, h, 12);
        CHECK_MSG(meanDiff(still, src) > 0.5, "clones deslocados aparecem");
    }
    // Perto: sem profundidade acima do limiar, nenhuma regiao -> igual a entrada, mesmo com a pessoa na mascara
    {
        const Img none = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("wireframe")}}, cam, farD, mask, w, h, 3);
        CHECK_MSG(meanDiff(none, src) < 0.01, "pessoa longe da camera: sem glitch");
        // o limiar ajustavel: com 5 % a mesma cena entra na regiao
        const Img low = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("wireframe")}, {"near", Json(5.0)}, {"soft", Json(2.0)}}, cam, farD, mask, w, h, 3);
        CHECK_MSG(columnDiff(low, src, 0.0, 0.45) > 1.0, "limiar de perto ajustavel");
        // sem pessoa (mascara vazia): igual a entrada
        const GLuint empty = makeMaskTexture(64, 36, [](int, int) { return 0.0f; });
        const Img nobody = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("slicer")}}, cam, nearD, empty, w, h, 3);
        CHECK_MSG(meanDiff(nobody, src) < 0.01, "sem pessoa na mascara: sem glitch");
        // "so pessoas" desligado: a profundidade sozinha define a regiao (tudo perto)
        const Img depthOnly = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("slicer")}, {"usePerson", Json(false)}, {"sliceShift", Json(25.0)}}, cam, nearD, empty, w, h, 3);
        CHECK_MSG(columnDiff(depthOnly, src, 0.55, 1.0) > 1.0, "so profundidade: a regiao inclui a metade direita");
        glDeleteTextures(1, &empty);
    }
    // Mistura 0 = entrada
    CHECK_MSG(meanDiff(runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("wireframe")}, {"mix", Json(0.0)}}, cam, nearD, mask, w, h, 3), src) < 0.01, "mistura 0 = entrada");
    // Cor do glitch: wireframe com cor vermelha tem mais vermelho que com cor ciano
    {
        const Img r = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("wireframe")}, {"glitchColor", media::param::colorJson({1, 0, 0, 1})}}, cam, nearD, mask, w, h, 3);
        const Img c = runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json("wireframe")}, {"glitchColor", media::param::colorJson({0, 1, 1, 1})}}, cam, nearD, mask, w, h, 3);
        CHECK_MSG(columnMean(r, 0.0, 0.45, 0) > columnMean(c, 0.0, 0.45, 0) + 3.0, "cor do glitch");
    }
    glDeleteTextures(1, &cam);
    glDeleteTextures(1, &camB);
    glDeleteTextures(1, &mask);
    glDeleteTextures(1, &nearD);
    glDeleteTextures(1, &farD);
}

// ---------------------------------------------------------------------------------------------------- Aura, Z-Slice e roteamento

// Mascara retangular de borda suave (3 px) em coordenadas da mascara
float softBox(float x, float y, float cx, float halfW, float cy, float halfH)
{
    const float ax = std::clamp((halfW - std::abs(x - cx)) / 3.0f + 0.5f, 0.0f, 1.0f);
    const float ay = std::clamp((halfH - std::abs(y - cy)) / 3.0f + 0.5f, 0.0f, 1.0f);
    return ax * ay;
}

void uploadMask(GLuint tex, int w, int h, const std::function<float(int, int)>& f)
{
    std::vector<unsigned char> data(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<unsigned char>(std::clamp(f(x, y), 0.0f, 1.0f) * 255.0f + 0.5f);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

constexpr int kMaskW = 160, kMaskH = 90;

// Roda o Aura 'frames' quadros com uma pessoa-retangulo cujo centro (px da mascara) vem de centerAt(quadro)
Img runAura(FboPool& pool, const Values& values, GLuint input, int frames, const std::function<float(int)>& centerAt,
            const audio::AudioSnapshot* audioSnap = nullptr, std::string* statusOut = nullptr, int w = 640, int h = 360)
{
    ai::AiEngine& eng = ai::AiEngine::instance();
    const GLuint mask = makeMaskTexture(kMaskW, kMaskH, [](int, int) { return 0.0f; });
    eng.setOverride(kAiTestSource, 0, mask);
    std::unique_ptr<fx::IEffect> effect = fx::EffectRegistry::instance().create(bl::kAuraId);
    if (!effect) {
        report(false, "Aura nao criado");
        return {};
    }
    media::ParamValues v;
    for (const auto& [k, val] : values)
        v.set(k, val);
    effect->setParams(media::ParamSet(effect->paramDescs(), v));
    Fbo out(w, h);
    for (int f = 0; f < frames; ++f) {
        const float c = centerAt(f);
        uploadMask(mask, kMaskW, kMaskH, [&](int x, int y) { return softBox(static_cast<float>(x), static_cast<float>(y), c, 24.0f, 45.0f, 25.0f); });
        fx::EffectContext ctx;
        ctx.time = f / 60.0;
        ctx.dt = 1.0 / 60.0;
        ctx.frameIndex = static_cast<std::uint64_t>(f);
        ctx.canvasWidth = w;
        ctx.canvasHeight = h;
        ctx.pool = &pool;
        ctx.sourceId = kAiTestSource;
        if (audioSnap)
            ctx.audio = *audioSnap;
        effect->process(ctx, input, w, h, out);
    }
    if (statusOut)
        *statusOut = effect->status().text;
    glFinish();
    eng.setOverride(kAiTestSource, 0, 0);
    const Img img = readTexture(out.texture(), w, h);
    glDeleteTextures(1, &mask);
    return img;
}

// Media do canal numa faixa de colunas e de linhas (fracoes)
double boxMean(const Img& img, double x0, double x1, double y0, double y1, int channel)
{
    double sum = 0.0;
    size_t n = 0;
    for (int y = static_cast<int>(y0 * img.h); y < static_cast<int>(y1 * img.h); ++y)
        for (int x = static_cast<int>(x0 * img.w); x < static_cast<int>(x1 * img.w); ++x) {
            sum += img.at(x, y)[channel];
            ++n;
        }
    return n ? sum / static_cast<double>(n) : 0.0;
}

void testAura(FboPool& pool)
{
    section("BSL Performer Aura: borda em movimento empurra o fluido, parada dissipa");
    CHECK(fx::EffectRegistry::instance().find(bl::kAuraId).has_value() && fx::EffectRegistry::instance().find(bl::kAuraId)->ai);
    CHECK(fx::EffectRegistry::instance().find(bl::kAuraId)->section == fx::EffectSection::Bsl);
    CHECK(fx::EffectRegistry::instance().find(bl::kZSliceId).has_value() && fx::EffectRegistry::instance().find(bl::kZSliceId)->ai);
    const GLuint gray = makeTexture(640, 360, [](int, int, unsigned char* p) { p[0] = p[1] = p[2] = 90; p[3] = 255; });
    const Img src = readTexture(gray, 640, 360);

    auto moving = [](int f) { return 40.0f + 0.6f * static_cast<float>(std::min(f, 59)); };
    const float cEnd = 40.0f + 0.6f * 59.0f;
    const double rightEdge = (cEnd + 24.0) / kMaskW, leftEdge = (cEnd - 24.0) / kMaskW;

    // --- movendo para a direita: ha densidade nas bordas e nada longe delas
    const Img dens = runAura(pool, {{"debug", Json("density")}}, gray, 60, moving);
    const double dEdge = boxMean(dens, rightEdge - 0.03, rightEdge + 0.06, 0.25, 0.75, 0);
    const double dFar = boxMean(dens, 0.85, 1.0, 0.0, 1.0, 0);
    std::printf("  densidade (0..255): na borda direita %.1f | longe da pessoa %.1f\n", dEdge, dFar);
    CHECK_MSG(dEdge > 6.0 && dEdge > dFar * 3.0 + 2.0, "ha densidade na borda que avanca e quase nada longe dela");
    // o fluido e empurrado no sentido do movimento: depois que a pessoa para, a densidade continua indo para a direita
    // (esquerda, se ela foi para a esquerda)
    auto centroid = [](const Img& img) {
        double sw = 0.0, sx = 0.0;
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                const double d = img.at(x, y)[0];
                sw += d;
                sx += d * (static_cast<double>(x) / img.w);
            }
        return sw > 0.0 ? sx / sw : -1.0;
    };
    auto shortMove = [](int f) { return 60.0f + 0.6f * static_cast<float>(std::min(f, 20)); };
    auto shortMoveL = [](int f) { return 100.0f - 0.6f * static_cast<float>(std::min(f, 20)); };
    const double r0 = centroid(runAura(pool, {{"debug", Json("density")}}, gray, 21, shortMove));
    const double r1 = centroid(runAura(pool, {{"debug", Json("density")}}, gray, 21 + 40, shortMove));
    const double l0 = centroid(runAura(pool, {{"debug", Json("density")}}, gray, 21, shortMoveL));
    const double l1 = centroid(runAura(pool, {{"debug", Json("density")}}, gray, 21 + 40, shortMoveL));
    std::printf("  centroide da densidade ao parar -> 40 quadros depois: direita %.3f -> %.3f | esquerda %.3f -> %.3f\n", r0, r1, l0, l1);
    CHECK_MSG(r1 > r0 + 0.01, "movendo para a direita a densidade segue para a direita");
    CHECK_MSG(l1 < l0 - 0.01, "movendo para a esquerda a densidade segue para a esquerda");
    const Img vel = runAura(pool, {{"debug", Json("velocity")}}, gray, 60, moving);
    const double vPos = boxMean(vel, leftEdge - 0.05, rightEdge + 0.15, 0.3, 0.7, 0);  // R = velocidade x > 0
    const double vNeg = boxMean(vel, leftEdge - 0.05, rightEdge + 0.15, 0.3, 0.7, 2);  // B = velocidade x < 0
    std::printf("  velocidade x: positiva %.1f | negativa %.1f\n", vPos, vNeg);
    CHECK_MSG(vPos > 8.0 && vPos > vNeg * 3.0, "a velocidade aponta para a direita");
    // movendo para a esquerda inverte o sinal
    const Img velL = runAura(pool, {{"debug", Json("velocity")}}, gray, 60, [](int f) { return 100.0f - 0.6f * static_cast<float>(std::min(f, 59)); });
    CHECK_MSG(boxMean(velL, 0.1, 0.9, 0.3, 0.7, 2) > boxMean(velL, 0.1, 0.9, 0.3, 0.7, 0) * 3.0 + 2.0, "movendo para a esquerda a velocidade aponta para a esquerda");

    // --- mascara parada desde o inicio: nada de aura
    const Img still = runAura(pool, {{"debug", Json("density")}}, gray, 60, [](int) { return 80.0f; });
    CHECK_MSG(boxMean(still, 0.0, 1.0, 0.0, 1.0, 0) < 0.5, "mascara parada: sem densidade");
    CHECK(!nonEmpty(runAura(pool, {{"onlyEffect", Json(true)}, {"cutPerson", Json(false)}}, gray, 40, [](int) { return 80.0f; })));

    // --- mexe e para: com dissipacao alta a densidade zera; com baixa ela ainda esta la
    auto moveThenStop = [](int f) { return 40.0f + 0.6f * static_cast<float>(std::min(f, 45)); };
    const Img fast = runAura(pool, {{"debug", Json("density")}, {"dissipation", Json(100.0)}}, gray, 45 + 90, moveThenStop);
    const Img slow = runAura(pool, {{"debug", Json("density")}, {"dissipation", Json(0.0)}}, gray, 45 + 30, moveThenStop);
    const double mFast = boxMean(fast, 0.0, 1.0, 0.0, 1.0, 0), mSlow = boxMean(slow, 0.0, 1.0, 0.0, 1.0, 0);
    std::printf("  densidade media depois de parar: dissipacao 100 %% = %.2f | 0 %% = %.2f\n", mFast, mSlow);
    CHECK_MSG(mFast < 0.3, "mascara parada + dissipacao alta: a densidade zera");
    CHECK_MSG(mSlow > 2.0 && mSlow > mFast * 8.0, "dissipacao baixa: a densidade continua");
    const Img velStop = runAura(pool, {{"debug", Json("velocity")}, {"dissipation", Json(100.0)}}, gray, 45 + 90, moveThenStop);
    CHECK_MSG(boxMean(velStop, 0.0, 1.0, 0.0, 1.0, 0) < 0.5, "mascara parada: a velocidade tambem zera");

    // --- forca do movimento e espessura
    const Img weak = runAura(pool, {{"debug", Json("velocity")}, {"motionForce", Json(20.0)}}, gray, 60, moving);
    const Img strong = runAura(pool, {{"debug", Json("velocity")}, {"motionForce", Json(300.0)}}, gray, 60, moving);
    CHECK_MSG(boxMean(strong, 0.0, 1.0, 0.0, 1.0, 0) > boxMean(weak, 0.0, 1.0, 0.0, 1.0, 0) * 2.0, "a forca do movimento escala a velocidade");
    const Img thin = runAura(pool, {{"debug", Json("density")}, {"thickness", Json(1.0)}}, gray, 60, moving);
    const Img thick = runAura(pool, {{"debug", Json("density")}, {"thickness", Json(25.0)}}, gray, 60, moving);
    CHECK_MSG(boxMean(thick, 0.0, 1.0, 0.0, 1.0, 0) > boxMean(thin, 0.0, 1.0, 0.0, 1.0, 0) * 1.3, "mais espessura = aura mais larga");

    // --- depuracao: mascara suavizada e movimento das bordas
    const Img dbgMask = runAura(pool, {{"debug", Json("mask")}}, gray, 5, [](int) { return 80.0f; });
    CHECK_MSG(boxMean(dbgMask, 0.45, 0.55, 0.45, 0.55, 0) > 200.0 && boxMean(dbgMask, 0.0, 0.15, 0.0, 1.0, 0) < 5.0, "ver mascara suavizada");
    const Img dbgMotion = runAura(pool, {{"debug", Json("motion")}}, gray, 30, moving);
    CHECK_MSG(boxMean(dbgMotion, 0.0, 1.0, 0.0, 1.0, 0) > boxMean(dbgMotion, 0.0, 1.0, 0.0, 1.0, 2) * 3.0 + 1.0, "ver movimento: fluxo para a direita");

    // --- composicao
    std::string st;
    const Img aura = runAura(pool, {}, gray, 50, moving, nullptr, &st);
    CHECK_MSG(st.empty(), "status: " + st);
    CHECK_MSG(meanDiff(aura, src) > 0.5, "a aura muda a imagem");
    const Img onlyFx = runAura(pool, {{"onlyEffect", Json(true)}, {"cutPerson", Json(false)}}, gray, 50, moving);
    std::printf("  so o efeito: alfa longe %.2f | alfa medio %.2f\n", boxMean(onlyFx, 0.0, 0.05, 0.0, 1.0, 3), boxMean(onlyFx, 0.0, 1.0, 0.0, 1.0, 3));
    CHECK_MSG(boxMean(onlyFx, 0.0, 0.05, 0.0, 1.0, 3) < 12.0 && boxMean(onlyFx, 0.0, 1.0, 0.0, 1.0, 3) > 3.0, "so o efeito: transparente onde nao ha aura");
    // pessoa por cima: dentro da silhueta a imagem fica igual a entrada
    const Img cut = runAura(pool, {{"cutPerson", Json(true)}, {"intensity", Json(300.0)}}, gray, 50, moving);
    const double cx = (40.0 + 0.6 * 49) / kMaskW;
    CHECK_MSG(boxMean(cut, cx - 0.08, cx + 0.08, 0.35, 0.65, 0) < 91.0 && boxMean(cut, cx - 0.08, cx + 0.08, 0.35, 0.65, 0) > 89.0, "pessoa por cima: dentro da silhueta a imagem fica como a entrada");
    const Img nocut = runAura(pool, {{"cutPerson", Json(false)}, {"intensity", Json(300.0)}}, gray, 50, moving);
    CHECK_MSG(meanDiff(cut, nocut) > 0.05, "pessoa por cima muda o resultado");
    for (const char* mode : {"particles", "lasers"}) {
        const Img m = runAura(pool, {{"mode", Json(mode)}}, gray, 50, moving, nullptr, &st);
        CHECK_MSG(meanDiff(m, src) > 0.2 && meanDiff(m, aura) > 0.2 && st.empty(), std::string("modo ") + mode + " desenha");
    }
    for (const char* q : {"low", "high"})
        CHECK_MSG(meanDiff(runAura(pool, {{"quality", Json(q)}}, gray, 50, moving), src) > 0.3, std::string("qualidade ") + q);
    // cores: A azul, B magenta -> o vermelho domina com B
    const Img colB = runAura(pool, {{"colorA", media::param::colorJson({1, 0, 0, 1})}, {"colorB", media::param::colorJson({1, 0, 0, 1})}, {"cutPerson", Json(false)}}, gray, 50, moving);
    CHECK_MSG(boxMean(colB, 0.0, 1.0, 0.0, 1.0, 0) - 90.0 > boxMean(colB, 0.0, 1.0, 0.0, 1.0, 2) - 90.0 + 1.0, "cor A/B: aura vermelha soma no vermelho");
    // audio: graves aumentam a forca
    {
        audio::AudioSnapshot loud = loudBass();
        const Img off = runAura(pool, {{"debug", Json("density")}, {"audioReact", Json(false)}}, gray, 40, moving, &loud);
        const Img on = runAura(pool, {{"debug", Json("density")}, {"audioReact", Json(true)}, {"audioAmount", Json(200.0)}}, gray, 40, moving, &loud);
        const double mOff = boxMean(off, 0.0, 1.0, 0.0, 1.0, 0), mOn = boxMean(on, 0.0, 1.0, 0.0, 1.0, 0);
        std::printf("  densidade media: audio desligado %.2f | graves ligados %.2f\n", mOff, mOn);
        CHECK_MSG(mOn > mOff * 1.3, "os graves aumentam a forca/emissao");
    }
    // orientacao: com a linha 0 na base (topOrigin = false) a aura continua no mesmo lugar da imagem (esq. x dir.)
    {
        ai::AiEngine& eng = ai::AiEngine::instance();
        eng.setProfileRate(0); // Eco
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kAuraId);
        e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
        Fbo out(640, 360);
        fx::EffectContext ctx;
        ctx.pool = &pool;
        ctx.sourceId = kAiTestSource + 1;
        e->process(ctx, gray, 640, 360, out);
        glFinish();
        CHECK_MSG(meanDiff(readTexture(out.texture(), 640, 360), src) < 0.01, "IA desligada: a imagem passa direto");
        CHECK_MSG(e->status().text == "IA desligada no perfil Eco", "status: " + e->status().text);
        eng.setProfileRate(15);
    }
    glDeleteTextures(1, &gray);
}

// Roda o Z-Slice uma vez e devolve a imagem e a mascara de zonas publicada (se houve)
struct ZRun
{
    Img image;
    Img mask;
    ZoneMask info;
    bool published = false;
};

ZRun runZSlice(FboPool& pool, const Values& values, GLuint input, GLuint depthTex, int w, int h, fx::ChainKind chain = fx::ChainKind::Layer,
               int layerIndex = 0, bool top = false, bool preview = false)
{
    ZRun r;
    ai::AiEngine& eng = ai::AiEngine::instance();
    eng.setOverride(kAiTestSource, depthTex, 0);
    ZoneMasks::instance().beginFrame();
    std::unique_ptr<fx::IEffect> effect = fx::EffectRegistry::instance().create(bl::kZSliceId);
    media::ParamValues v;
    for (const auto& [k, val] : values)
        v.set(k, val);
    effect->setParams(media::ParamSet(effect->paramDescs(), v));
    Fbo out(w, h);
    fx::EffectContext ctx;
    ctx.dt = 1.0 / 60.0;
    ctx.canvasWidth = w;
    ctx.canvasHeight = h;
    ctx.topOrigin = top;
    ctx.pool = &pool;
    ctx.sourceId = kAiTestSource;
    ctx.chain = chain;
    ctx.layerIndex = layerIndex;
    ctx.preview = preview;
    effect->process(ctx, input, w, h, out);
    glFinish();
    eng.setOverride(kAiTestSource, 0, 0);
    r.image = readTexture(out.texture(), w, h);
    const int source = chain == fx::ChainKind::Master ? kZoneSourceMaster : layerIndex;
    r.published = ZoneMasks::instance().find(source, r.info);
    if (r.published)
        r.mask = readTexture(r.info.texture, r.info.width, r.info.height);
    return r;
}

void testZSlice(FboPool& pool)
{
    section("BSL Z-Slice: zonas por limiar, feather, visualizacoes e mascara publicada");
    const int w = 640, h = 360;
    const GLuint gray = makeTexture(w, h, [](int, int, unsigned char* p) { p[0] = 200; p[1] = 120; p[2] = 60; p[3] = 255; });
    const Img src = readTexture(gray, w, h);
    const GLuint ramp = makeDepthTexture(64, 36, [](int x, int) { return static_cast<float>(x) / 63.0f; });

    // --- 3 zonas numa rampa horizontal: Fundo | Meio | Frente
    ZRun z3 = runZSlice(pool, {{"feather", Json(0.0)}}, gray, ramp, w, h);
    CHECK(z3.published && z3.info.zones == 3);
    if (z3.published) {
        const double bgB = columnMean(z3.mask, 0.02, 0.28, 2), bgG = columnMean(z3.mask, 0.02, 0.28, 1), bgR = columnMean(z3.mask, 0.02, 0.28, 0);
        const double miG = columnMean(z3.mask, 0.38, 0.60, 1), miB = columnMean(z3.mask, 0.38, 0.60, 2), miR = columnMean(z3.mask, 0.38, 0.60, 0);
        const double frR = columnMean(z3.mask, 0.72, 0.98, 0), frG = columnMean(z3.mask, 0.72, 0.98, 1), frB = columnMean(z3.mask, 0.72, 0.98, 2);
        std::printf("  3 zonas: esquerda B=%.0f G=%.0f R=%.0f | meio G=%.0f B=%.0f R=%.0f | direita R=%.0f G=%.0f B=%.0f\n", bgB, bgG, bgR, miG, miB, miR, frR, frG, frB);
        CHECK_MSG(bgB > 250 && bgG < 5 && bgR < 5, "Fundo ocupa a faixa mais longe (esquerda)");
        CHECK_MSG(miG > 250 && miB < 5 && miR < 5, "Meio ocupa a faixa do meio");
        CHECK_MSG(frR > 250 && frG < 5 && frB < 5, "Frente ocupa a faixa mais perto (direita)");
        CHECK_MSG(columnMean(z3.mask, 0.0, 1.0, 3) < 2.0, "com 3 zonas a Zona 4 fica vazia");
        // as fronteiras ficam nos limiares (66 % e 33 %)
        auto edgeX = [&](int channel, bool rising) { // primeira coluna onde o canal passa de 128
            for (int x = 0; x < z3.mask.w; ++x) {
                const bool on = z3.mask.at(x, z3.mask.h / 2)[channel] > 128;
                if (on == rising)
                    return static_cast<double>(x) / z3.mask.w;
            }
            return -1.0;
        };
        const double fEdge = edgeX(0, true), bEdge = edgeX(2, false);
        std::printf("  fronteiras: Frente comeca em %.3f (limiar 0.66) | Fundo termina em %.3f (limiar 0.33)\n", fEdge, bEdge);
        CHECK_MSG(std::abs(fEdge - 0.66) < 0.03 && std::abs(bEdge - 0.33) < 0.03, "as fronteiras seguem os limiares");
        // a soma dos pesos e 1
        bool sumOk = true;
        for (int x = 0; x < z3.mask.w; x += 7) {
            const unsigned char* p = z3.mask.at(x, 3);
            sumOk = sumOk && std::abs(p[0] + p[1] + p[2] + p[3] - 255) <= 3;
        }
        CHECK_MSG(sumOk, "os pesos das zonas somam 1");
    }
    // sem visualizacao ("Imagem inteira") a imagem passa como esta, mas a mascara e publicada
    CHECK_MSG(meanDiff(z3.image, src) < 0.01, "imagem inteira: a saida e a entrada");

    // --- 4 zonas
    ZRun z4 = runZSlice(pool, {{"zones", Json("4")}, {"t1", Json(75.0)}, {"t2", Json(50.0)}, {"t3", Json(25.0)}, {"feather", Json(0.0)}}, gray, ramp, w, h);
    CHECK(z4.published && z4.info.zones == 4);
    if (z4.published) {
        const double a4 = columnMean(z4.mask, 0.02, 0.20, 3), bB = columnMean(z4.mask, 0.30, 0.45, 2), mG = columnMean(z4.mask, 0.55, 0.70, 1), fR = columnMean(z4.mask, 0.80, 0.98, 0);
        std::printf("  4 zonas: Zona 4 %.0f | Fundo %.0f | Meio %.0f | Frente %.0f\n", a4, bB, mG, fR);
        CHECK_MSG(a4 > 250 && bB > 250 && mG > 250 && fR > 250, "4 zonas: cada uma na sua faixa");
        CHECK_MSG(columnMean(z4.mask, 0.02, 0.20, 2) < 5 && columnMean(z4.mask, 0.30, 0.45, 3) < 5, "4 zonas: sem mistura fora da faixa");
    }
    // limiares fora de ordem sao reordenados (Meio/Fundo acima de Frente/Meio vira igual a ele)
    {
        ZRun bad = runZSlice(pool, {{"t1", Json(40.0)}, {"t2", Json(80.0)}, {"feather", Json(0.0)}}, gray, ramp, w, h);
        CHECK_MSG(bad.published && columnMean(bad.mask, 0.0, 1.0, 1) < 5.0, "limiares fora de ordem: o Meio fica vazio, sem inversao");
    }

    // --- feather: transicao suave entre zonas vizinhas
    {
        ZRun soft = runZSlice(pool, {{"feather", Json(30.0)}}, gray, ramp, w, h);
        CHECK(soft.published);
        if (soft.published) {
            int mixedCols = 0;
            for (int x = 0; x < soft.mask.w; ++x) {
                const unsigned char* p = soft.mask.at(x, 3);
                mixedCols += (p[0] > 25 && p[0] < 230) ? 1 : 0;
            }
            const double widthFrac = static_cast<double>(mixedCols) / soft.mask.w;
            std::printf("  feather 30 %%: transicao Frente/Meio ocupa %.3f da largura\n", widthFrac);
            CHECK_MSG(widthFrac > 0.12 && widthFrac < 0.35, "feather alarga a transicao");
        }
    }

    // --- visualizacoes
    {
        ZRun solo = runZSlice(pool, {{"view", Json("solo")}, {"solo", Json("front")}, {"feather", Json(0.0)}}, gray, ramp, w, h);
        CHECK_MSG(columnMean(solo.image, 0.02, 0.5, 3) < 3.0 && columnMean(solo.image, 0.75, 0.98, 3) > 252.0, "mostrar so a Frente: o resto fica transparente");
        CHECK_MSG(columnMean(solo.image, 0.8, 0.98, 0) > 198.0, "a zona mostrada mantem a cor");
        ZRun soloB = runZSlice(pool, {{"view", Json("solo")}, {"solo", Json("back")}, {"feather", Json(0.0)}}, gray, ramp, w, h);
        CHECK_MSG(columnMean(soloB.image, 0.02, 0.25, 3) > 252.0 && columnMean(soloB.image, 0.45, 0.98, 3) < 3.0, "mostrar so o Fundo");
        ZRun colors = runZSlice(pool, {{"view", Json("colors")}, {"feather", Json(0.0)}, {"tint", Json(100.0)}}, gray, ramp, w, h);
        CHECK_MSG(meanDiff(colors.image, src) > 5.0, "zonas coloridas mudam a imagem");
        CHECK_MSG(columnMean(colors.image, 0.8, 0.98, 0) > columnMean(colors.image, 0.8, 0.98, 2) + 40.0, "Frente = vermelho");
        CHECK_MSG(columnMean(colors.image, 0.02, 0.28, 2) > columnMean(colors.image, 0.02, 0.28, 0) + 40.0, "Fundo = azul");
        CHECK_MSG(columnMean(colors.image, 0.4, 0.58, 1) > columnMean(colors.image, 0.4, 0.58, 0) + 40.0, "Meio = verde");
    }

    // --- quando publica e quando nao
    {
        const Values v = {{"feather", Json(0.0)}};
        CHECK_MSG(runZSlice(pool, v, gray, ramp, w, h, fx::ChainKind::Layer, 2).published, "camada 3 publica para a fonte 2");
        CHECK_MSG(runZSlice(pool, v, gray, ramp, w, h, fx::ChainKind::Master, -1).published, "master publica para a fonte do master");
        CHECK_MSG(!runZSlice(pool, v, gray, ramp, w, h, fx::ChainKind::Clip, 0).published, "cadeia do clip nao publica");
        CHECK_MSG(!runZSlice(pool, v, gray, ramp, w, h, fx::ChainKind::Unknown, 0).published, "fora do Compositor nao publica");
        CHECK_MSG(!runZSlice(pool, v, gray, ramp, w, h, fx::ChainKind::Layer, 0, true).published, "textura com linha 0 no topo nao publica");
        CHECK_MSG(!runZSlice(pool, v, gray, ramp, w, h, fx::ChainKind::Layer, 0, false, true).published, "copia da Previa nao publica");
        // so vale no quadro em que foi publicado; efeito destruido: some na hora
        {
            ai::AiEngine::instance().setOverride(kAiTestSource, ramp, 0);
            std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kZSliceId);
            e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
            Fbo out(w, h);
            fx::EffectContext ctx;
            ctx.pool = &pool;
            ctx.sourceId = kAiTestSource;
            ctx.chain = fx::ChainKind::Layer;
            ctx.layerIndex = 4;
            ctx.topOrigin = false;
            ZoneMasks::instance().beginFrame();
            e->process(ctx, gray, w, h, out);
            CHECK_MSG(ZoneMasks::instance().available(4), "publicada no quadro");
            ZoneMasks::instance().beginFrame();
            CHECK_MSG(!ZoneMasks::instance().available(4), "a mascara vale so ate o proximo quadro do Compositor");
            e->process(ctx, gray, w, h, out);
            CHECK_MSG(ZoneMasks::instance().available(4), "publicada de novo no quadro seguinte");
            e.reset();
            CHECK_MSG(!ZoneMasks::instance().available(4), "efeito destruido: sem mascara");
            ai::AiEngine::instance().setOverride(kAiTestSource, 0, 0);
        }
        // status do clip
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kZSliceId);
        ai::AiEngine::instance().setOverride(kAiTestSource, ramp, 0);
        e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
        Fbo out(w, h);
        fx::EffectContext ctx;
        ctx.pool = &pool;
        ctx.sourceId = kAiTestSource;
        ctx.chain = fx::ChainKind::Clip;
        e->process(ctx, gray, w, h, out);
        CHECK_MSG(e->status().text.find("camada ou do master") != std::string::npos, "na cadeia do clip o status explica: " + e->status().text);
        ai::AiEngine::instance().setOverride(kAiTestSource, 0, 0);
    }
    // --- Eco: sem IA, nada e publicado e a imagem passa direto
    {
        ai::AiEngine& eng = ai::AiEngine::instance();
        eng.setProfileRate(0);
        ZoneMasks::instance().beginFrame();
        std::unique_ptr<fx::IEffect> e = fx::EffectRegistry::instance().create(bl::kZSliceId);
        e->setParams(media::ParamSet(e->paramDescs(), media::ParamValues()));
        Fbo out(w, h);
        fx::EffectContext ctx;
        ctx.pool = &pool;
        ctx.sourceId = kAiTestSource + 7;
        ctx.chain = fx::ChainKind::Layer;
        ctx.layerIndex = 0;
        ctx.topOrigin = false;
        e->process(ctx, gray, w, h, out);
        glFinish();
        CHECK_MSG(meanDiff(readTexture(out.texture(), w, h), src) < 0.01, "Eco: a imagem passa direto");
        CHECK_MSG(e->status().text == "IA desligada no perfil Eco", "status: " + e->status().text);
        CHECK_MSG(!ZoneMasks::instance().available(0), "Eco: nenhuma mascara publicada");
        eng.setProfileRate(15);
    }
    glDeleteTextures(1, &gray);
    glDeleteTextures(1, &ramp);
}

// Fonte de teste: uma cor chapada (linha 0 = topo)
class FlatSource final : public media::ISource
{
public:
    FlatSource(int w, int h, unsigned char r, unsigned char g, unsigned char b) : w_(w), h_(h)
    {
        tex_ = makeTexture(w, h, [&](int, int, unsigned char* p) { p[0] = r; p[1] = g; p[2] = b; p[3] = 255; });
    }
    ~FlatSource() override { glDeleteTextures(1, &tex_); }
    bool open(const fs::path&, std::string*) override { return open_ = true; }
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

void testZoneRouting(GLFWwindow* window)
{
    section("roteamento por zona nos slices (OutputRenderer + Compositor + Z-Slice)");
    constexpr int W = 320, H = 180;
    ai::AiEngine& eng = ai::AiEngine::instance();
    Composition comp = Composition::createDefault(1, 1);
    comp.width = W;
    comp.height = H;
    Compositor compositor(W, H);
    Clip& clip = comp.clip(0, 0);
    clip.type = Clip::Type::File;
    clip.name = "teste";
    clip.fit = FitMode::Stretch;
    clip.source = std::make_unique<FlatSource>(W, H, 200, 120, 60);
    comp.layers[0].activeDeck = 0;
    comp.layers[0].activeColumn = 0;
    const GLuint ramp = makeDepthTexture(64, 36, [](int x, int) { return static_cast<float>(x) / 63.0f; });
    const std::uint64_t layerSource = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&comp.layers[0]));
    eng.setOverride(layerSource, ramp, 0);
    eng.setOverride(1, ramp, 0); // master

    fx::EffectInstance& zs = comp.layers[0].effects.add(bl::kZSliceId);
    zs.setParam("feather", Json(0.0));

    OutputRenderer renderer(window);
    OutputSetup setup = OutputSetup::createDefault(W, H, {});
    Screen& screen = setup.screens.front();
    screen.kind = OutputKind::Disabled;
    screen.width = W;
    screen.height = H;
    screen.slices.front() = makeFullSlice("Slice 1", W, H, W, H);
    Slice& slice = screen.slices.front();
    const int screenId = screen.id;

    auto shoot = [&]() {
        compositor.render(comp, 1.0f);
        renderer.render(setup, compositor, screenId);
        std::vector<unsigned char> rgba;
        int sw = 0, sh = 0;
        if (!renderer.readScreen(screenId, rgba, sw, sh)) {
            report(false, "readScreen");
            rgba.assign(static_cast<size_t>(W) * H * 4, 0);
        }
        return rgba;
    };
    auto px = [&](const std::vector<unsigned char>& rgba, double fx, double fy) {
        const size_t i = (static_cast<size_t>(fy * H) * W + static_cast<size_t>(fx * W)) * 4;
        return std::array<int, 3>{rgba[i], rgba[i + 1], rgba[i + 2]};
    };
    auto isColor = [](const std::array<int, 3>& p) { return std::abs(p[0] - 200) <= 3 && std::abs(p[1] - 120) <= 3 && std::abs(p[2] - 60) <= 3; };
    auto isBlack = [](const std::array<int, 3>& p) { return p[0] <= 2 && p[1] <= 2 && p[2] <= 2; };
    const double L = 0.15, M = 0.50, R = 0.85;

    // Referencia ("antes"): efeito em bypass e slice sem filtro
    zs.bypass = true;
    const std::vector<unsigned char> before = shoot();
    CHECK_MSG(isColor(px(before, L, 0.5)) && isColor(px(before, R, 0.5)), "referencia: a imagem inteira nos slices");

    // Todas (padrao): com o Z-Slice ativo o resultado e identico a antes, byte a byte
    zs.bypass = false;
    CHECK(slice.zFilter == kZoneAll && slice.zSource == kZoneSourceMaster);
    slice.zSource = 0;
    const std::vector<unsigned char> allZones = shoot();
    CHECK_MSG(allZones == before, "zFilter = Todas: identico a antes");
    CHECK(ZoneMasks::instance().available(0)); // o Z-Slice da camada publicou

    // Frente / Meio / Fundo: so os pixels da zona
    slice.zFilter = kZoneFront;
    const std::vector<unsigned char> front = shoot();
    CHECK_MSG(isColor(px(front, R, 0.5)) && isBlack(px(front, M, 0.5)) && isBlack(px(front, L, 0.5)), "Frente: so a faixa mais perto (direita)");
    slice.zFilter = kZoneMiddle;
    const std::vector<unsigned char> middle = shoot();
    CHECK_MSG(isColor(px(middle, M, 0.5)) && isBlack(px(middle, L, 0.5)) && isBlack(px(middle, R, 0.5)), "Meio: so a faixa do meio");
    slice.zFilter = kZoneBack;
    const std::vector<unsigned char> back = shoot();
    CHECK_MSG(isColor(px(back, L, 0.5)) && isBlack(px(back, M, 0.5)) && isBlack(px(back, R, 0.5)), "Fundo: so a faixa mais longe (esquerda)");
    slice.zFilter = kZone4;
    CHECK_MSG(isBlack(px(shoot(), M, 0.5)), "Zona 4 com 3 zonas: vazia");

    // Os tres slices juntos formam a imagem inteira (as zonas sao complementares)
    {
        int ok = 0, total = 0;
        for (size_t i = 0; i + 3 < front.size(); i += 4 * 9) {
            for (int c = 0; c < 3; ++c) {
                const int sum = front[i + static_cast<size_t>(c)] + middle[i + static_cast<size_t>(c)] + back[i + static_cast<size_t>(c)];
                ok += std::abs(sum - before[i + static_cast<size_t>(c)]) <= 4 ? 1 : 0;
                ++total;
            }
        }
        CHECK_MSG(ok == total, "Frente + Meio + Fundo = a imagem inteira");
    }

    // Sem Z-Slice ativo: o slice mostra tudo
    slice.zFilter = kZoneFront;
    zs.bypass = true;
    const std::vector<unsigned char> noFx = shoot();
    CHECK_MSG(noFx == before && !ZoneMasks::instance().available(0), "Z-Slice em bypass: o slice mostra tudo");
    zs.bypass = false;
    slice.zSource = 5; // camada que nao tem Z-Slice
    CHECK_MSG(shoot() == before, "fonte sem Z-Slice (camada 6): o slice mostra tudo");
    slice.zSource = kZoneSourceMaster; // so ha Z-Slice na camada 1, nao no master
    CHECK_MSG(shoot() == before, "fonte Master sem Z-Slice: o slice mostra tudo");

    // Master: o Z-Slice do master alimenta os slices com zSource = Master
    comp.masterEffects.add(bl::kZSliceId).setParam("feather", Json(0.0));
    {
        const std::vector<unsigned char> m = shoot();
        CHECK(ZoneMasks::instance().available(kZoneSourceMaster));
        CHECK_MSG(isColor(px(m, R, 0.5)) && isBlack(px(m, L, 0.5)), "Master: Frente so na faixa mais perto");
    }
    slice.zSource = 0;
    CHECK_MSG(isColor(px(shoot(), R, 0.5)), "as duas fontes convivem");

    // Slice desligado do filtro depois de usar: volta ao normal
    slice.zFilter = kZoneAll;
    CHECK_MSG(shoot() == before, "voltar para Todas restaura a saida");

    eng.setOverride(layerSource, 0, 0);
    eng.setOverride(1, 0, 0);
    glDeleteTextures(1, &ramp);
    clip.source.reset();
}

void dumpNative(const fs::path& dir, FboPool& pool)
{
    section("imagens dos plugins nativos");
    const int w = 960, h = 540;
    const GLuint cam = makeCamera(false, true, w, h);
    const GLuint cut = makeCamera(true, true, w, h);
    RunOptions opt;
    opt.w = w;
    opt.h = h;
    opt.canvasW = w;
    opt.canvasH = h;
    opt.frames = 40;
    RunOptions loud = opt;
    loud.audio = loudBass();
    auto save = [&](const std::string& name, const Img& img) {
        const bool ok = savePng(dir / (name + ".png"), img, Img{});
        std::printf("  %s %s\n", name.c_str(), ok ? "ok" : "ERRO");
    };
    save("alpha-reactor", runEffect(pool, bl::kAlphaReactorId, {{"trailZoom", Json(6.0)}}, cut, loud));
    save("datamosh", runEffect(pool, bl::kDatamoshId, {{"sortThreshold", Json(45.0)}, {"sortLength", Json(60.0)}, {"neonEdges", Json(35.0)},
                                                   {"moshAmount", Json(40.0)}, {"protectRadius", Json(18.0)}}, cam, loud));
    for (const char* mode : {"boiler", "marquee", "glitch", "mask"}) {
        const std::string blend = std::string(mode) == "mask" ? "cutout" : (std::string(mode) == "glitch" ? "add" : "difference");
        save(std::string("audio-typo-") + mode, runEffect(pool, bl::kTypoId, {{"mode", Json(mode)}, {"blendMode", Json(blend)}, {"djText", Json("DJ BSL")}}, cam, opt));
    }
    save("audio-typo-sem-hud", runEffect(pool, bl::kTypoId, {{"hud", Json(0.0)}, {"bwRave", Json(0.0)}, {"blendMode", Json("overlay")}, {"posY", Json(50.0)}}, cam, opt));
    OutputSetup setup;
    const int si = setup.addScreen(w, h);
    setup.screens[static_cast<size_t>(si)].slices.clear();
    for (int i = 0; i < 4; ++i) {
        Slice s = makeFullSlice("S" + std::to_string(i), w, h, w, h);
        s.input = {40 + (i % 2) * 450, 40 + (i / 2) * 250, 400, 220};
        setup.screens[static_cast<size_t>(si)].slices.push_back(s);
    }
    bl::SliceFeed::instance().set(bl::sliceFeedFromOutput(setup), w, h);
    save("slice-refiner-modos", runEffect(pool, bl::kSliceRefinerId,
                                        {{"s1Mode", Json("neon")}, {"s2Mode", Json("gradient")}, {"s2Roundness", Json(40.0)},
                                         {"s3Mode", Json("rgbwave")}, {"s4Mode", Json("radar")}, {"s4Hue", Json(300.0)}}, cam, opt));
    for (const auto& d : bl::describeGenBackgrounds())
        if (d.id == "style")
            for (const auto& o : d.options)
                save("bg-" + o.value, runSource(bl::kGenBackgroundsId, {{"style", Json(o.value)}}, 90, 480, 270));
    for (const char* preset : {"worn", "cam90", "glitch", "subtle"})
        save(std::string("vhs-") + preset, runEffect(pool, bl::kVhsId, {{"preset", Json(preset)}, {"playText", Json(true)}, {"dateStamp", Json(true)}}, cam, opt));
    {
        // Relight com uma "esfera" sintetica de profundidade e 2 luzes
        const GLuint sphere = makeDepthTexture(96, 54, [](int x, int y) {
            const float dx = (static_cast<float>(x) - 48.0f) / 20.0f, dy = (static_cast<float>(y) - 27.0f) / 20.0f, r2 = dx * dx + dy * dy;
            return r2 < 1.0f ? std::sqrt(1.0f - r2) * 0.9f + 0.1f : 0.1f;
        });
        const Values two = {{"l1_on", Json(true)}, {"l1_x", Json(20.0)}, {"l1_y", Json(30.0)}, {"l1_color", media::param::colorJson({1.0f, 0.5f, 0.2f, 1.0f})},
                            {"l2_on", Json(true)}, {"l2_x", Json(80.0)}, {"l2_y", Json(70.0)}, {"l2_color", media::param::colorJson({0.2f, 0.5f, 1.0f, 1.0f})},
                            {"relief", Json(60.0)}, {"bump", Json(150.0)}};
        save("relight-2-luzes", runAiEffect(pool, bl::kRelightId, two, cam, sphere, 0, w, h));
        save("relight-profundidade", runAiEffect(pool, bl::kRelightId, {{"debug", Json("depth")}}, cam, sphere, 0, w, h));
        save("relight-normais", runAiEffect(pool, bl::kRelightId, {{"debug", Json("normal")}, {"relief", Json(60.0)}}, cam, sphere, 0, w, h));
        const GLuint mask = makeMaskTexture(96, 54, [](int x, int y) {
            const float dx = (static_cast<float>(x) - 48.0f) / 24.0f, dy = (static_cast<float>(y) - 27.0f) / 24.0f;
            return dx * dx + dy * dy < 1.0f ? 1.0f : 0.0f;
        });
        for (const char* mode : {"datamosh", "clones", "wireframe", "slicer"})
            save(std::string("glitch-") + mode, runAiEffect(pool, bl::kBodyGlitchId, {{"mode", Json(mode)}, {"cloneOffset", Json(6.0)}}, cam, sphere, mask, w, h, 10, true, cut));
        glDeleteTextures(1, &sphere);
        glDeleteTextures(1, &mask);
    }
    save("led-mapper", runSource(bl::kLedMapperId, {{"columns", Json(4)}, {"rows", Json(3)}, {"cabinetW", Json(180)}, {"cabinetH", Json(120)}, {"gapX", Json(8)}, {"gapY", Json(8)}}, 2, w, h));
    glDeleteTextures(1, &cam);
    glDeleteTextures(1, &cut);
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // a saida aparece na hora, mesmo redirecionada
    fs::path compareDir, dumpDir;
    fs::path srcDir;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--src" && i + 1 < argc)
            srcDir = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));
        if (std::string(argv[i]) == "--compare-motion" && i + 1 < argc) {
            g_motionOnly = true;
            compareDir = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));
        }
        if (std::string(argv[i]) == "--compare" && i + 1 < argc)
            compareDir = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));
        if (std::string(argv[i]) == "--dump" && i + 1 < argc)
            dumpDir = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));
    }

    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "bsltest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    FboPool pool;
    int rc = 0;
    if (!dumpDir.empty()) {
        dumpNative(dumpDir, pool);
        pool.clear();
        glfwDestroyWindow(win);
        glfwTerminate();
        return 0;
    }
    if (!compareDir.empty()) {
        if (srcDir.empty()) {
            char* home = nullptr;
            size_t len = 0;
            _dupenv_s(&home, &len, "USERPROFILE");
            srcDir = fs::path(home ? home : "") / L"Desktop" / L"ffgl-master" / L"source" / L"plugins";
            std::free(home);
        }
        rc = runCompare(compareDir, pool, srcDir);
        ffgl::Catalog::instance().shutdown();
        pool.clear();
        glfwDestroyWindow(win);
        glfwTerminate();
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(rc)); // plugins com threads proprias nao seguram o encerramento
        return rc;
    }

    bool onlyB = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--only-b") // so os testes da etapa B (Slice Refiner 3, PPT Manager, WebBackgrounds, WebMotion)
            onlyB = true;
    if (onlyB) {
        testSliceRefiner3(pool);
        testPptManager();
        testWebSources();
        std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
        std::printf(g_failures == 0 ? "BSLTEST OK\n" : "BSLTEST FALHOU\n");
        rc = g_failures == 0 ? 0 : 1;
        pool.clear();
        glfwDestroyWindow(win);
        glfwTerminate();
        return rc;
    }

    testAudio();
    testRegistry();
    testEffectsBasic(pool);
    testParamsChangeOutput(pool);
    testAudioReaction(pool);
    testOrientation(pool);
    testSlices(pool);
    testLedExport();
    testSliceRefiner3(pool);
    testPptManager();
    testWebSources();
    testVhs(pool);
    testRelight(pool);
    testBodyGlitch(pool);
    testAura(pool);
    testZSlice(pool);
    testZoneRouting(win);

    GLenum err;
    int glErrors = 0;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::printf("  erro de GL pendente: 0x%04X\n", err);
        ++glErrors;
    }
    report(glErrors == 0, "nenhum erro de GL");
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "BSLTEST OK\n" : "BSLTEST FALHOU\n");
    rc = g_failures == 0 ? 0 : 1;

    pool.clear();
    glfwDestroyWindow(win);
    glfwTerminate();
    return rc;
}
