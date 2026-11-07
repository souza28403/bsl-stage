#include "core/Generators.h"
#include "ffgl/FfglCatalog.h"
#include "ffgl/FfglImport.h"
#include "ffgl/FfglInstance.h"
#include "ffgl/FfglPe.h"
#include "fx/ChainRunner.h"
#include "fx/EffectChain.h"
#include "fx/EffectRegistry.h"
#include "media/WorkerProcess.h"
#include "render/FboPool.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
using Json = media::Json;

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

struct Px
{
    int r = 0, g = 0, b = 0, a = 255;
};
using Img = std::vector<Px>;

std::string str(Px p)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%d,%d,%d,%d)", p.r, p.g, p.b, p.a);
    return buf;
}

bool closeTo(Px a, Px b, int tol)
{
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol && std::abs(a.a - b.a) <= tol;
}

Px at(const Img& img, int w, int x, int y)
{
    return img[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
}

void expectPx(const char* what, const Img& img, int w, int x, int y, Px want, int tol = 4)
{
    const Px got = at(img, w, x, y);
    ++g_checks;
    if (!closeTo(got, want, tol)) {
        ++g_failures;
        std::printf("  FALHOU: %s em (%d,%d): obtido %s, esperado %s\n", what, x, y, str(got).c_str(), str(want).c_str());
    }
}

GLuint makeTexture(int w, int h, const std::function<Px(int, int)>& f)
{
    std::vector<unsigned char> data(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Px p = f(x, y);
            unsigned char* d = &data[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
            d[0] = static_cast<unsigned char>(p.r);
            d[1] = static_cast<unsigned char>(p.g);
            d[2] = static_cast<unsigned char>(p.b);
            d[3] = static_cast<unsigned char>(p.a);
        }
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

Img readFbo(const Fbo& fbo)
{
    Img img(static_cast<size_t>(fbo.width()) * static_cast<size_t>(fbo.height()));
    std::vector<unsigned char> data(img.size() * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbo.width(), fbo.height(), GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (size_t i = 0; i < img.size(); ++i)
        img[i] = {data[i * 4], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]};
    return img;
}

fs::path exeDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
}

fs::path testPluginDir()
{
    return exeDir() / L"ffgl-test-plugins";
}

std::string id(const char* pluginId, const char* file)
{
    return ffgl::makeTypeId(pluginId, file);
}

bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

fx::EffectContext makeCtx(FboPool& pool, bool topOrigin, double time = 0.0)
{
    fx::EffectContext ctx;
    ctx.time = time;
    ctx.bpm = 140.0f;
    ctx.barPhase = 0.25f;
    ctx.topOrigin = topOrigin;
    ctx.pool = &pool;
    return ctx;
}

void applyParams(ffgl::FfglInstance& inst, const std::vector<std::pair<std::string, Json>>& values)
{
    media::ParamValues v;
    for (const auto& [key, val] : values)
        v.set(key, val);
    inst.setParams(media::ParamSet(inst.paramDescs(), v));
}

ffgl::EntryPtr need(const std::string& typeId)
{
    ffgl::EntryPtr e = ffgl::Catalog::instance().find(typeId);
    CHECK(e != nullptr);
    return e;
}

void testPeFilter()
{
    section("filtro de PE");
    const fs::path dir = testPluginDir();
    CHECK(ffgl::inspectPe(dir / L"TestAddSubtract.dll") == ffgl::PeVerdict::Candidate);
    CHECK(ffgl::inspectPe(dir / L"TestGradients.dll") == ffgl::PeVerdict::Candidate);
    CHECK(ffgl::inspectPe(dir / L"TestNoMain.dll") == ffgl::PeVerdict::NoPlugMain);
    CHECK(ffgl::inspectPe(exeDir() / L"pdfium.dll") == ffgl::PeVerdict::NoPlugMain);
    CHECK(ffgl::inspectPe(dir / L"nao-existe.dll") == ffgl::PeVerdict::Unreadable);
    const fs::path fake = fs::temp_directory_path() / L"bsl-ffgltest-fake.dll";
    {
        std::ofstream(fake, std::ios::binary) << "isto nao e um executavel";
    }
    CHECK(ffgl::inspectPe(fake) == ffgl::PeVerdict::NotPe);
    std::error_code ec;
    fs::remove(fake, ec);
}

void testScan()
{
    section("varredura em processo separado, cache e bloqueio");
    ffgl::Catalog& cat = ffgl::Catalog::instance();
    const fs::path cache = fs::temp_directory_path() / L"bsl-ffgltest-cache.json";
    std::error_code ec;
    fs::remove(cache, ec);
    cat.setCachePath(cache);
    cat.setUseCache(true);
    cat.setScanTimeoutMs(4000);
    cat.configure({{testPluginDir(), "Teste"}});

    const auto t0 = std::chrono::steady_clock::now();
    cat.rescan();
    const double callMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(callMs < 250.0);
    CHECK(cat.scanning());
    CHECK(cat.waitIdle(90000));
    std::printf("  primeira varredura: %zu plugin(s), %zu bloqueado(s), %d ignorada(s)\n", cat.entries().size(),
                cat.blocked().size(), cat.ignoredCount());

    const ffgl::EntryPtr add = need(id("RE01", "TestAddSubtract.dll"));
    if (add) {
        CHECK(add->info.type == ffgl::kTypeEffect);
        CHECK(add->info.apiMajor == 2);
        CHECK(add->info.name == "AddSub Example");
        CHECK(contains(add->info.description, "Add and Subtract"));
        CHECK(add->info.params.size() == 3);
        CHECK(add->info.params.size() == 3 && add->info.params[0].type == ffgl::kPRed && add->info.params[2].type == ffgl::kPBlue);
        CHECK(add->map.descs.size() == 1 && add->map.descs[0].type == media::ParamType::Color);
        CHECK(add->origin == "Teste");
        CHECK(add->info.minInputs == 1 && add->info.maxInputs == 1);
    }
    const ffgl::EntryPtr grad = need(id("RS01", "TestGradients.dll"));
    if (grad) {
        CHECK(grad->info.type == ffgl::kTypeSource);
        CHECK(grad->info.params.size() == 8);
        CHECK(grad->map.descs.size() == 2);
        CHECK(grad->map.descs.size() == 2 && grad->map.descs[0].type == media::ParamType::Color
              && grad->map.descs[1].type == media::ParamType::Color);
        CHECK(grad->map.bindings.size() == 2 && grad->map.bindings[0].kind == ffgl::BindKind::Rgb
              && grad->map.bindings[1].kind == ffgl::BindKind::Hsb && grad->map.bindings[1].count == 4);
    }
    const ffgl::EntryPtr probe = need(id("TPEF", "TestProbeFx.dll"));
    if (probe) {
        CHECK(probe->map.descs.size() == 3);
        CHECK(probe->map.descs.size() == 3 && probe->map.descs[0].id == "level" && probe->map.descs[0].type == media::ParamType::Float);
    }
    need(id("TPSR", "TestProbeSrc.dll"));
    need(id("FAIL", "TestCrash.dll"));

    bool hangBlocked = false;
    for (const ffgl::BlockedPlugin& b : cat.blocked())
        if (b.fileName == "TestHang.dll") {
            hangBlocked = true;
            CHECK(!b.session);
            CHECK(contains(b.reason, "não respondeu") || contains(b.reason, "nao respondeu"));
            std::printf("  TestHang.dll bloqueado: %s\n", b.reason.c_str());
        }
    CHECK(hangBlocked);
    for (const ffgl::EntryPtr& e : cat.entries())
        CHECK(e->info.fileName != "TestHang.dll");

    CHECK(cat.ignoredCount() >= 1);
    CHECK(GetModuleHandleW(L"TestNoMain.dll") == nullptr);
    for (const ffgl::BlockedPlugin& b : cat.blocked())
        CHECK(b.fileName != "TestNoMain.dll");
    CHECK(GetModuleHandleW(L"TestAddSubtract.dll") == nullptr);

    CHECK(fs::exists(cache, ec));
    {
        std::ifstream in(cache);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(contains(text, "TestAddSubtract.dll") && contains(text, "\"size\"") && contains(text, "\"mtime\""));
    }
    const size_t before = cat.entries().size();
    const auto t1 = std::chrono::steady_clock::now();
    cat.rescan();
    CHECK(cat.waitIdle(30000));
    const double cachedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::printf("  segunda varredura (cache): %.0f ms\n", cachedMs);
    CHECK(cachedMs < 2500.0);
    CHECK(cat.entries().size() == before);
    CHECK(cat.blocked().size() >= 1);

    CHECK(cat.find(id("RE01", "TestAddSubtract.dll")) != nullptr);
    CHECK(cat.find(id("RE01", "Renomeado.dll")) != nullptr);
    CHECK(cat.find(id("XXXX", "TestAddSubtract.dll")) != nullptr);
    CHECK(cat.find(id("XXXX", "outro.dll")) == nullptr);
    CHECK(cat.find("native:blur") == nullptr);
}

void testProcessEffects(FboPool& pool)
{
    section("efeito do SDK (AddSubtract): pixels, alpha, parametros");
    const ffgl::EntryPtr add = ffgl::Catalog::instance().find(id("RE01", "TestAddSubtract.dll"));
    if (!add)
        return;
    const int w = 64, h = 64;
    const GLuint src = makeTexture(w, h, [](int, int) { return Px{100, 100, 100, 255}; });
    Fbo out(w, h);
    ffgl::FfglInstance inst(add);
    CHECK(inst.needsInput());
    CHECK(inst.id() == add->typeId);

    for (const bool top : {false, true}) {
        applyParams(inst, {});
        inst.process(makeCtx(pool, top), src, w, h, out);
        CHECK(!inst.faulted());
        CHECK(inst.status().text.empty());
        Img img = readFbo(out);
        expectPx(top ? "AddSubtract sem soma (topo)" : "AddSubtract sem soma", img, w, 10, 10, {100, 100, 100, 255});
        applyParams(inst, {{"brightness", media::param::colorJson({1.0f, 0.5f, 0.5f, 1.0f})}});
        inst.process(makeCtx(pool, top), src, w, h, out);
        img = readFbo(out);
        expectPx(top ? "mudar o parametro altera a saida (topo)" : "mudar o parametro altera a saida", img, w, 10, 10,
                 {255, 100, 100, 255});
        expectPx("a outra metade", img, w, 50, 50, {255, 100, 100, 255});
        applyParams(inst, {{"brightness", media::param::colorJson({0.5f, 0.0f, 0.5f, 1.0f})}});
        inst.process(makeCtx(pool, top), src, w, h, out);
        expectPx("verde cai a zero", readFbo(out), w, 5, 5, {100, 0, 100, 255});
    }

    {
        const GLuint half = makeTexture(w, h, [](int, int) { return Px{200, 100, 40, 128}; });
        applyParams(inst, {});
        inst.process(makeCtx(pool, true), half, w, h, out);
        expectPx("alpha reto preservado", readFbo(out), w, 7, 7, {200, 100, 40, 128}, 5);
        glDeleteTextures(1, &half);
    }
    glDeleteTextures(1, &src);
}

void testSdkTemplate(FboPool& pool)
{
    section("modelo de plugin FFGL do SDK (sdk/ffgl-template): BSL Exemplo");
    const ffgl::EntryPtr ex = ffgl::Catalog::instance().find(id("BSEX", "BSLExample.dll"));
    CHECK(ex != nullptr);
    if (!ex)
        return;
    CHECK(ex->info.type == ffgl::kTypeEffect);
    CHECK(ex->info.apiMajor == 2);
    CHECK(ex->info.name == "BSL Exemplo");
    CHECK(ex->info.params.size() == 2);
    CHECK(ex->map.descs.size() == 2 && ex->map.descs[0].type == media::ParamType::Float
          && ex->map.descs[1].type == media::ParamType::Float);
    if (ex->map.descs.size() != 2)
        return;
    const std::string brightness = ex->map.descs[0].id, saturation = ex->map.descs[1].id;

    const int w = 64, h = 64;
    const GLuint gray = makeTexture(w, h, [](int, int) { return Px{100, 100, 100, 255}; });
    const GLuint color = makeTexture(w, h, [](int, int) { return Px{200, 100, 50, 255}; });
    Fbo out(w, h);
    ffgl::FfglInstance inst(ex);
    for (const bool top : {false, true}) {
        applyParams(inst, {});
        inst.process(makeCtx(pool, top), color, w, h, out);
        CHECK(!inst.faulted());
        CHECK(inst.status().text.empty());
        expectPx("modelo: valores padrao nao mudam a imagem", readFbo(out), w, 10, 10, {200, 100, 50, 255});
        applyParams(inst, {{brightness, 1.0}});
        inst.process(makeCtx(pool, top), gray, w, h, out);
        expectPx("modelo: brilho 1,0 deixa branco", readFbo(out), w, 20, 20, {255, 255, 255, 255});
        applyParams(inst, {{brightness, 0.0}});
        inst.process(makeCtx(pool, top), gray, w, h, out);
        expectPx("modelo: brilho 0,0 deixa preto", readFbo(out), w, 20, 20, {0, 0, 0, 255});
        applyParams(inst, {{brightness, 0.5}, {saturation, 0.0}});
        inst.process(makeCtx(pool, top), color, w, h, out);
        expectPx("modelo: saturacao 0 vira cinza", readFbo(out), w, 30, 30, {118, 118, 118, 255}, 3);
    }
}

void testOrientation(FboPool& pool)
{
    section("orientacao (plugin que mantem a metade de baixo)");
    const ffgl::EntryPtr fxe = ffgl::Catalog::instance().find(id("TPEF", "TestProbeFx.dll"));
    const ffgl::EntryPtr srce = ffgl::Catalog::instance().find(id("TPSR", "TestProbeSrc.dll"));
    if (!fxe || !srce)
        return;
    const int w = 64, h = 64;
    const GLuint src = makeTexture(w, h, [](int x, int y) { return Px{x * 4, y * 4, 50, 255}; });
    Fbo out(w, h);
    {
        ffgl::FfglInstance inst(fxe);
        inst.process(makeCtx(pool, false), src, w, h, out);
        CHECK(!inst.faulted());
        Img img = readFbo(out);
        expectPx("sem inversao: baixo = linhas 0..31", img, w, 10, 5, {40, 20, 50, 255});
        expectPx("sem inversao: cima = azul do nivel", img, w, 10, 50, {0, 0, 128, 255});
        inst.process(makeCtx(pool, true), src, w, h, out);
        img = readFbo(out);
        expectPx("com inversao: baixo = linhas 32..63", img, w, 10, 50, {40, 200, 50, 255});
        expectPx("com inversao: cima = azul do nivel", img, w, 10, 5, {0, 0, 128, 255});
        applyParams(inst, {{"level", 1.0}});
        inst.process(makeCtx(pool, true), src, w, h, out);
        expectPx("nivel 1 -> azul 255", readFbo(out), w, 10, 5, {0, 0, 255, 255});
        applyParams(inst, {{"level", 0.0}});
        inst.process(makeCtx(pool, true), src, w, h, out);
        expectPx("nivel 0 -> azul 0", readFbo(out), w, 10, 5, {0, 0, 0, 255});
    }
    {
        ffgl::FfglInstance inst(srce);
        CHECK(!inst.needsInput());
        inst.process(makeCtx(pool, false), 0, w, h, out);
        CHECK(!inst.faulted());
        Img img = readFbo(out);
        expectPx("fonte sem inversao: linha 0 = base (r~0)", img, w, 8, 0, {2, 0, 128, 255}, 6);
        expectPx("fonte sem inversao: ultima linha = topo (r~255)", img, w, 8, 63, {253, 0, 128, 255}, 6);
        inst.process(makeCtx(pool, true), 0, w, h, out);
        img = readFbo(out);
        expectPx("fonte com inversao: linha 0 = topo (r~255)", img, w, 8, 0, {253, 0, 128, 255}, 6);
        expectPx("fonte com inversao: ultima linha = base (r~0)", img, w, 8, 63, {2, 0, 128, 255}, 6);
    }
    glDeleteTextures(1, &src);
}

void testBeatAndReadback(FboPool& pool)
{
    section("informacao de batida e leitura de volta dos parametros");
    const ffgl::EntryPtr fxe = ffgl::Catalog::instance().find(id("TPEF", "TestProbeFx.dll"));
    if (!fxe)
        return;
    const int w = 32, h = 32;
    const GLuint src = makeTexture(w, h, [](int, int) { return Px{10, 20, 30, 255}; });
    Fbo out(w, h);
    ffgl::FfglInstance inst(fxe);
    fx::EffectContext ctx = makeCtx(pool, false);
    ctx.bpm = 140.0f;
    ctx.barPhase = 0.25f;
    inst.process(ctx, src, w, h, out);
    CHECK(inst.lastBpm() == 140.0f && inst.lastBarPhase() == 0.25f);
    media::ParamValues changed;
    CHECK(inst.pollParamChanges(changed));
    const Json* bpm = changed.find("bpm");
    const Json* bar = changed.find("barphase");
    CHECK(bpm && bpm->is_number() && std::fabs(bpm->get<double>() - 0.140) < 1e-4);
    CHECK(bar && bar->is_number() && std::fabs(bar->get<double>() - 0.25) < 1e-4);
    CHECK(changed.find("level") == nullptr);
    media::ParamValues again;
    CHECK(!inst.pollParamChanges(again));
    ctx.bpm = 90.0f;
    inst.process(ctx, src, w, h, out);
    Sleep(130);
    media::ParamValues third;
    CHECK(inst.pollParamChanges(third));
    const Json* bpm2 = third.find("bpm");
    CHECK(bpm2 && std::fabs(bpm2->get<double>() - 0.090) < 1e-4);
    CHECK(inst.framesProcessed() == 2);
    glDeleteTextures(1, &src);
}

void testGradientSource(FboPool& pool)
{
    section("fonte do SDK (Gradients) como gerador");
    const ffgl::EntryPtr grad = ffgl::Catalog::instance().find(id("RS01", "TestGradients.dll"));
    if (!grad)
        return;
    const GeneratorInfo* info = findGenerator(grad->typeId);
    CHECK(info != nullptr);
    if (!info)
        return;
    CHECK(info->describe(media::ParamValues()).size() == 2);
    auto src = info->create(64, 32);
    CHECK(src != nullptr);
    media::ParamValues values;
    values.fillDefaults(info->describe(values));
    src->setParams(values);
    std::string error;
    CHECK(src->open({}, &error));
    CHECK(src->isOpen() && src->texture() != 0 && src->width() == 64 && src->height() == 32);
    src->update(0.016);
    CHECK(src->status().text.empty());
    Fbo copy(64, 32);
    {
        fx::TexBlitter blit;
        blit.copy(src->texture(), 64, 32, copy);
    }
    Img img = readFbo(copy);
    expectPx("gradiente a esquerda", img, 64, 0, 16, {255, 255, 0, 255}, 10);
    expectPx("gradiente a direita", img, 64, 63, 16, {0, 255, 255, 255}, 10);
    values.set("cor_1", media::param::colorJson({1.0f, 0.0f, 0.0f, 1.0f}));
    values.set("cor_2", media::param::colorJson({0.0f, 0.0f, 1.0f, 1.0f}));
    src->setParams(values);
    src->update(0.016);
    {
        fx::TexBlitter blit;
        blit.copy(src->texture(), 64, 32, copy);
    }
    img = readFbo(copy);
    expectPx("vermelho a esquerda", img, 64, 0, 16, {255, 0, 0, 255}, 10);
    expectPx("azul (HSB) a direita", img, 64, 63, 16, {0, 0, 255, 255}, 10);
    src->close();
    (void)pool;
}

void testFaults(FboPool& pool)
{
    section("plugin com violacao de acesso: o host sobrevive");
    ffgl::Catalog& cat = ffgl::Catalog::instance();
    const ffgl::EntryPtr crash = cat.find(id("FAIL", "TestCrash.dll"));
    CHECK(crash != nullptr);
    if (!crash)
        return;
    const int w = 32, h = 32;
    const GLuint src = makeTexture(w, h, [](int, int) { return Px{10, 20, 30, 255}; });
    Fbo out(w, h);
    {
        ffgl::FfglInstance a(crash);
        a.process(makeCtx(pool, true), src, w, h, out);
        CHECK(a.faulted());
        const media::SourceStatus st = a.status();
        CHECK(st.level == media::SourceStatus::Level::Error);
        CHECK(st.text.rfind("Plugin falhou: ", 0) == 0 && contains(st.text, "0xC0000005"));
        std::printf("  status: %s\n", st.text.c_str());
        expectPx("passagem direta depois da falha", readFbo(out), w, 3, 3, {10, 20, 30, 255});
        CHECK(cat.sessionBlocked(crash->info.path));
        bool listed = false;
        for (const ffgl::BlockedPlugin& b : cat.blocked())
            listed = listed || (b.session && b.fileName == "TestCrash.dll" && contains(b.reason, "0xC0000005"));
        CHECK(listed);
        a.process(makeCtx(pool, true), src, w, h, out);
        CHECK(a.faulted());
        expectPx("segue em passagem direta", readFbo(out), w, 3, 3, {10, 20, 30, 255});

        ffgl::FfglInstance b(crash);
        b.process(makeCtx(pool, true), src, w, h, out);
        CHECK(b.faulted());
        expectPx("segunda instancia tambem passa direto", readFbo(out), w, 3, 3, {10, 20, 30, 255});
    }
    cat.retry(crash->info.path);
    CHECK(cat.waitIdle(30000));
    CHECK(!cat.sessionBlocked(crash->info.path));
    {
        fx::EffectChain chain;
        chain.add(crash->typeId);
        fx::EffectContext ctx = makeCtx(pool, true);
        FboPool::Lease res = fx::ChainRunner::run(chain, ctx, src, w, h);
        CHECK(res.get() != nullptr);
        expectPx("cadeia com plugin que cai", readFbo(*res), w, 3, 3, {10, 20, 30, 255});
        CHECK(cat.sessionBlocked(crash->info.path));
        CHECK(chain.items[0].known());
        CHECK(chain.items[0].status().level == media::SourceStatus::Level::Error);
    }
    cat.retry(crash->info.path);
    CHECK(cat.waitIdle(30000));
    glDeleteTextures(1, &src);
}

void testRegistry(FboPool& pool)
{
    section("registro de efeitos e projeto");
    fx::EffectRegistry& reg = fx::EffectRegistry::instance();
    const std::string typeId = id("RE01", "TestAddSubtract.dll");
    const auto info = reg.find(typeId);
    CHECK(info.has_value());
    if (info) {
        CHECK(info->section == fx::EffectSection::Ffgl);
        CHECK(info->name == "AddSub Example" && info->category == "Teste" && !info->generator);
        CHECK(contains(info->description, "Autor: ") && contains(info->description, "TestAddSubtract.dll"));
    }
    CHECK(reg.describe(typeId).size() == 1);
    const auto g = reg.find(id("RS01", "TestGradients.dll"));
    CHECK(g && g->generator);
    CHECK(reg.find(id("RE01", "AddSubtractAntigo.dll")).has_value());
    CHECK(reg.find(id("ZZZZ", "TestAddSubtract.dll")).has_value());
    fx::EffectInstance missing(id("MISS", "Sumiu.dll"));
    missing.setParam("brilho", 0.7);
    CHECK(!missing.known());
    CHECK(missing.status().level == media::SourceStatus::Level::Warning);
    const Json j = missing.toJson();
    CHECK(j["type"] == id("MISS", "Sumiu.dll") && j["params"]["brilho"] == 0.7);
    fx::EffectInstance real(typeId);
    real.setParam("brightness", media::param::colorJson({1.0f, 0.5f, 0.5f, 1.0f}));
    const fx::EffectInstance back = fx::EffectInstance::fromJson(real.toJson());
    CHECK(back.typeId == typeId && back.known());
    (void)pool;
}

void testImport()
{
    section("importar plugins");
    const fs::path base = fs::temp_directory_path() / L"bsl-ffgltest-import";
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path source = base / L"origem";
    const fs::path dest = base / L"destino";
    fs::create_directories(source / L"web_app" / L"sub", ec);
    fs::create_directories(source / L"OutrosPlugins", ec);
    fs::copy_file(testPluginDir() / L"TestAddSubtract.dll", source / L"MeuPlugin.dll", ec);
    fs::copy_file(testPluginDir() / L"TestGradients.dll", source / L"OutrosPlugins" / L"Gradiente.dll", ec);
    std::ofstream(source / L"web_app" / L"index.html") << "<html></html>";
    std::ofstream(source / L"web_app" / L"sub" / L"dado.txt") << "x";
    std::ofstream(source / L"WebBrowser.dll") << "nao importa";
    std::ofstream(source / L"leia-me.txt") << "x";

    ffgl::ImportResult r = ffgl::importPlugins(source / L"MeuPlugin.dll", dest);
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok() && r.dlls == 1 && r.folders == 1);
    CHECK(fs::exists(dest / L"MeuPlugin.dll", ec));
    CHECK(fs::exists(dest / L"web_app" / L"sub" / L"dado.txt", ec));
    CHECK(!fs::exists(dest / L"OutrosPlugins", ec));
    CHECK(!fs::exists(dest / L"WebBrowser.dll", ec));
    CHECK(fs::exists(source / L"MeuPlugin.dll", ec) && fs::exists(source / L"web_app" / L"index.html", ec));

    fs::remove_all(dest, ec);
    r = ffgl::importPlugins(source, dest);
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok() && r.dlls == 2);
    CHECK(fs::exists(dest / L"origem" / L"OutrosPlugins" / L"Gradiente.dll", ec));
    CHECK(!fs::exists(dest / L"origem" / L"WebBrowser.dll", ec));

    r = ffgl::importPlugins(source / L"WebBrowser.dll", dest);
    CHECK(!r.ok() && contains(r.message, "fonte Web nativa"));
    r = ffgl::importPlugins(testPluginDir() / L"TestNoMain.dll", dest);
    CHECK(!r.ok());

    ffgl::Catalog& cat = ffgl::Catalog::instance();
    ffgl::ImportJob job;
    job.start(source / L"MeuPlugin.dll", dest / L"job");
    for (int i = 0; i < 200 && job.running(); ++i)
        Sleep(20);
    ffgl::ImportResult jr;
    CHECK(job.takeResult(jr) && jr.ok());
    cat.configure({{dest / L"job", "Importados"}});
    cat.rescan();
    CHECK(cat.waitIdle(60000));
    CHECK(cat.find(id("RE01", "MeuPlugin.dll")) != nullptr);
    fs::remove_all(base, ec);
    cat.configure({{testPluginDir(), "Teste"}});
    cat.rescan();
    cat.waitIdle(60000);
}

int runScan(const fs::path& dir, const std::string& only, FboPool& pool)
{
    if (dir.empty()) {
        std::printf("Informe a pasta: --scan <pasta>\n");
        return 0;
    }
    std::printf("Pasta de plugins (somente leitura): %s\n", dir.string().c_str());
    ffgl::Catalog& cat = ffgl::Catalog::instance();
    cat.setCachePath(fs::temp_directory_path() / L"bsl-ffgltest-scan-cache.json");
    cat.clearCache();
    cat.setScanTimeoutMs(10000);
    cat.configure({{dir, "Pasta externa"}});
    cat.rescan();
    cat.waitIdle(600000);

    struct Row
    {
        std::string name, type, note;
        size_t params = 0;
        bool ok = false;
    };
    std::vector<Row> rows;
    const char* typeNames[] = {"efeito", "fonte", "mixer"};
    for (const ffgl::EntryPtr& e : cat.entries()) {
        if (!only.empty() && only != e->info.fileName)
            continue;
        Row row;
        row.name = ffgl::pluginDisplayName(e->info) + " [" + e->info.fileName + "]";
        row.type = typeNames[std::min<unsigned>(e->info.type, 2)];
        row.params = e->info.params.size();
        if (e->info.type == ffgl::kTypeMixer) {
            row.note = "mixer: nao suportado";
            rows.push_back(row);
            continue;
        }
        const int w = 512, h = 288;
        const GLuint src = makeTexture(w, h, [](int x, int y) { return Px{x % 256, y % 256, 90, 255}; });
        Fbo out(w, h);
        {
            ffgl::FfglInstance inst(e);
            applyParams(inst, {});
            for (int f = 0; f < 60; ++f) {
                fx::EffectContext ctx = makeCtx(pool, f % 2 == 0, f / 60.0);
                ctx.frameIndex = static_cast<std::uint64_t>(f);
                inst.process(ctx, e->info.type == ffgl::kTypeSource ? 0 : src, w, h, out);
                media::ParamValues polled;
                inst.pollParamChanges(polled);
                if (f % 10 == 0)
                    Sleep(16);
            }
            row.ok = !inst.faulted() && inst.framesProcessed() == 60;
            if (inst.faulted())
                row.note = "falhou: " + inst.faultReason();
            else if (!inst.status().text.empty())
                row.note = inst.status().text;
            else if (inst.framesProcessed() != 60)
                row.note = "so " + std::to_string(inst.framesProcessed()) + " de 60 quadros";
        }
        GLenum err = glGetError();
        if (err != GL_NO_ERROR && row.ok)
            row.note = "erro de GL pendente 0x" + std::to_string(err);
        glDeleteTextures(1, &src);
        rows.push_back(row);
    }
    for (const ffgl::BlockedPlugin& b : cat.blocked()) {
        Row row;
        row.name = b.fileName;
        row.type = "?";
        row.note = "bloqueado na varredura: " + b.reason;
        rows.push_back(row);
    }
    std::printf("\n%-52s %-8s %-8s %-4s %s\n", "nome", "tipo", "params", "ok", "observacao");
    int bad = 0;
    for (const Row& r : rows) {
        std::printf("%-52s %-8s %-8zu %-4s %s\n", r.name.c_str(), r.type.c_str(), r.params, r.ok ? "sim" : "NAO", r.note.c_str());
        bad += r.ok ? 0 : 1;
    }
    std::printf("\n%zu plugin(s), %d sem sucesso\n", rows.size(), bad);
    return bad == 0 ? 0 : 1;
}

}

int main(int argc, char** argv)
{
    bool scan = false;
    fs::path scanDir;
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--scan") {
            scan = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                scanDir = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));
        } else if (std::string(argv[i]) == "--only" && i + 1 < argc) {
            only = argv[++i];
        }
    }

    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "ffgltest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major * 10 + minor < 41)
        std::printf("AVISO: os plugins pedem GLSL 4.10; o contexto e %d.%d\n", major, minor);

    FboPool pool;
    int rc = 0;
    if (scan) {
        rc = runScan(scanDir, only, pool);
    } else {
        testPeFilter();
        testScan();
        testProcessEffects(pool);
        testSdkTemplate(pool);
        testOrientation(pool);
        testBeatAndReadback(pool);
        testGradientSource(pool);
        testFaults(pool);
        testRegistry(pool);
        testImport();
        GLenum err;
        int glErrors = 0;
        while ((err = glGetError()) != GL_NO_ERROR) {
            std::printf("  erro de GL pendente: 0x%04X\n", err);
            ++glErrors;
        }
        report(glErrors == 0, "nenhum erro de GL");
        std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
        std::printf(g_failures == 0 ? "FFGLTEST OK\n" : "FFGLTEST FALHOU\n");
        rc = g_failures == 0 ? 0 : 1;
    }

    ffgl::Catalog::instance().shutdown();
    pool.clear();
    glfwDestroyWindow(win);
    glfwTerminate();
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(rc));
    return rc;
}
