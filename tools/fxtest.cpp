#include "core/Composition.h"
#include "fx/ChainRunner.h"
#include "fx/EffectChain.h"
#include "fx/EffectRegistry.h"
#include "fx/IsfDoc.h"
#include "fx/IsfLibrary.h"
#include "fx/IsfRenderer.h"
#include "media/ISource.h"
#include "render/Compositor.h"
#include "render/FboPool.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stb_image_write.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
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
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol
           && std::abs(a.a - b.a) <= tol;
}

void expectPx(const char* what, const Img& img, int w, int x, int y, Px want, int tol = 3)
{
    const Px got = img[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    const bool ok = closeTo(got, want, tol);
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHOU: %s em (%d,%d): obtido %s, esperado %s\n", what, x, y, str(got).c_str(), str(want).c_str());
    }
}

Px at(const Img& img, int w, int x, int y)
{
    return img[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
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

Img readTexture(GLuint tex, int w, int h)
{
    std::vector<unsigned char> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    Img img(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < img.size(); ++i)
        img[i] = {buf[i * 4], buf[i * 4 + 1], buf[i * 4 + 2], buf[i * 4 + 3]};
    return img;
}

Px quad(int x, int y, int n)
{
    const bool left = x < n / 2, top = y < n / 2;
    if (top)
        return left ? Px{255, 0, 0, 255} : Px{0, 255, 0, 255};
    return left ? Px{0, 0, 255, 255} : Px{255, 255, 255, 255};
}

fx::EffectContext makeCtx(FboPool& pool, bool top = true)
{
    fx::EffectContext ctx;
    ctx.pool = &pool;
    ctx.topOrigin = top;
    ctx.dt = 1.0 / 60.0;
    return ctx;
}

using ParamList = std::vector<std::pair<std::string, Json>>;

Img runFx(const std::string& type, const ParamList& params, GLuint tex, int w, int h, bool top = true, double beat = 0.0,
          double time = 0.0, bool eco = false)
{
    fx::EffectInstance inst(type);
    for (const auto& [k, v] : params)
        inst.setParam(k, v);
    fx::IEffect* e = inst.ensureEffect();
    if (!e) {
        report(false, "efeito nao criado: " + type);
        return Img(static_cast<size_t>(w) * static_cast<size_t>(h));
    }
    inst.syncParams();
    FboPool pool;
    fx::EffectContext ctx = makeCtx(pool, top);
    ctx.beat = beat;
    ctx.time = time;
    ctx.eco = eco;
    Fbo out(w, h);
    e->process(ctx, tex, w, h, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return readTexture(out.texture(), w, h);
}

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void testRegistry()
{
    section("registro");
    const auto types = fx::EffectRegistry::instance().types();
    int native = 0;
    for (const auto& t : types)
        if (t.section == fx::EffectSection::Native)
            ++native;
    CHECK(native == 15);
    for (const char* id : {"native:transform", "native:color", "native:levels", "native:blur", "native:chroma",
                           "native:luma", "native:mask", "native:crop", "native:mirror", "native:kaleido",
                           "native:pixelate", "native:strobe", "native:invert", "native:vignette", "native:rgbsplit"}) {
        const auto info = fx::EffectRegistry::instance().find(id);
        report(info.has_value() && !info->name.empty() && !info->category.empty() && info->icon && info->icon[0],
               std::string("tipo ") + id + " no catalogo");
        report(!fx::EffectRegistry::instance().describe(id).empty(), std::string("parametros de ") + id);
        fx::EffectInstance inst(id);
        report(inst.ensureEffect() != nullptr, std::string("cria ") + id);
        report(inst.status().text.empty(), std::string("sem erro: ") + id);
    }
    CHECK(!fx::EffectRegistry::instance().find("native:nao-existe").has_value());
    fx::EffectInstance missing("native:nao-existe");
    CHECK(missing.ensureEffect() == nullptr);
    CHECK(!missing.status().text.empty());
}

void testNative()
{
    const int n = 16;
    const GLuint q = makeTexture(n, n, [&](int x, int y) { return quad(x, y, n); });

    section("inverter");
    {
        const Img o = runFx("native:invert", {}, q, n, n);
        expectPx("inverter vermelho", o, n, 4, 4, {0, 255, 255, 255});
        expectPx("inverter branco", o, n, 12, 12, {0, 0, 0, 255});
        const GLuint half = makeTexture(n, n, [](int, int) { return Px{200, 100, 50, 128}; });
        expectPx("inverter mantem o alfa", runFx("native:invert", {}, half, n, n), n, 3, 3, {55, 155, 205, 128});
        expectPx("inverter 50%", runFx("native:invert", {{"amount", 0.5}}, half, n, n), n, 3, 3, {128, 128, 128, 128});
    }

    section("espelho");
    {
        Img o = runFx("native:mirror", {{"mode", "h"}}, q, n, n);
        expectPx("h: direita recebe a esquerda (topo)", o, n, 12, 4, {255, 0, 0, 255});
        expectPx("h: direita recebe a esquerda (base)", o, n, 12, 12, {0, 0, 255, 255});
        o = runFx("native:mirror", {{"mode", "v"}}, q, n, n);
        expectPx("v: base recebe o topo (esq.)", o, n, 4, 12, {255, 0, 0, 255});
        expectPx("v: base recebe o topo (dir.)", o, n, 12, 12, {0, 255, 0, 255});
        o = runFx("native:mirror", {{"mode", "quad"}}, q, n, n);
        expectPx("quatro quadrantes", o, n, 12, 12, {255, 0, 0, 255});
        expectPx("quatro quadrantes (2)", o, n, 4, 12, {255, 0, 0, 255});
        o = runFx("native:mirror", {{"mode", "h"}, {"flip", true}}, q, n, n);
        expectPx("h invertido: esquerda recebe a direita", o, n, 4, 4, {0, 255, 0, 255});
        o = runFx("native:mirror", {{"mode", "v"}}, q, n, n, false);
        expectPx("v em textura de base-primeiro", o, n, 4, 2, {0, 0, 255, 255});
        expectPx("v em textura de base-primeiro (2)", o, n, 12, 2, {255, 255, 255, 255});
    }

    section("recortar");
    {
        Img o = runFx("native:crop", {{"unit", "percent"}, {"left", 50.0}}, q, n, n);
        expectPx("recorte esquerdo 50%", o, n, 4, 8, {0, 0, 0, 0});
        expectPx("o resto fica", o, n, 12, 4, {0, 255, 0, 255});
        o = runFx("native:crop", {{"unit", "px"}, {"top", 4.0}, {"right", 4.0}}, q, n, n);
        expectPx("topo 4 px", o, n, 8, 2, {0, 0, 0, 0});
        expectPx("depois do topo", o, n, 8, 6, {0, 255, 0, 255}, 3);
        expectPx("direita 4 px", o, n, 14, 8, {0, 0, 0, 0});
        expectPx("antes da direita", o, n, 10, 8, {255, 255, 255, 255}, 3);
    }

    section("chroma key");
    {
        const GLuint t = makeTexture(n, n, [](int x, int y) {
            if (y < 4)
                return x < 8 ? Px{30, 220, 40, 255} : Px{255, 0, 0, 255};
            if (y >= 12)
                return Px{255, 255, 255, 255};
            return x < 8 ? Px{0, 255, 0, 255} : Px{255, 0, 0, 255};
        });
        const Img o = runFx("native:chroma", {}, t, n, n);
        CHECK(at(o, n, 4, 8).a <= 4);
        CHECK(at(o, n, 4, 1).a <= 8);
        CHECK(at(o, n, 12, 8).a >= 252);
        CHECK(at(o, n, 8, 14).a >= 252);
        CHECK(at(o, n, 12, 8).r >= 250 && at(o, n, 12, 8).g <= 8);
        const Img o2 = runFx("native:chroma", {{"tolerance", 0.0}, {"softness", 0.0}}, t, n, n);
        CHECK(at(o2, n, 4, 8).a <= 4);
        CHECK(at(o2, n, 4, 1).a >= 252);
    }

    section("luma key");
    {
        const GLuint t = makeTexture(n, n, [](int x, int) { return x < 8 ? Px{0, 0, 0, 255} : Px{255, 255, 255, 255}; });
        Img o = runFx("native:luma", {}, t, n, n);
        CHECK(at(o, n, 3, 8).a <= 4 && at(o, n, 12, 8).a >= 252);
        o = runFx("native:luma", {{"invert", true}}, t, n, n);
        CHECK(at(o, n, 3, 8).a >= 252 && at(o, n, 12, 8).a <= 4);
    }

    section("mascara");
    {
        Img o = runFx("native:mask", {{"shape", "ellipse"}}, q, n, n);
        CHECK(at(o, n, 8, 8).a >= 252);
        CHECK(at(o, n, 0, 0).a <= 4);
        CHECK(at(o, n, 15, 15).a <= 4);
        o = runFx("native:mask", {{"shape", "ellipse"}, {"invert", true}}, q, n, n);
        CHECK(at(o, n, 8, 8).a <= 4 && at(o, n, 0, 0).a >= 252);
        o = runFx("native:mask", {{"shape", "rect"}}, q, n, n);
        CHECK(at(o, n, 8, 8).a >= 252 && at(o, n, 2, 8).a <= 4 && at(o, n, 8, 2).a <= 4);
        o = runFx("native:mask", {{"shape", "rect"}, {"feather", 30.0}}, q, n, n);
        const int edge = at(o, n, 3, 8).a;
        CHECK(at(o, n, 8, 8).a >= 252);
        CHECK(edge < 252);
    }

    section("transformar");
    {
        const GLuint t = makeTexture(n, n, [](int x, int) { return x < 4 ? Px{255, 0, 0, 255} : Px{0, 0, 0, 255}; });
        Img o = runFx("native:transform", {{"posX", 25.0}}, t, n, n);
        expectPx("deslocou para a direita", o, n, 5, 8, {255, 0, 0, 255});
        expectPx("a area vazia fica transparente", o, n, 1, 8, {0, 0, 0, 0});
        expectPx("o preto foi junto", o, n, 12, 8, {0, 0, 0, 255});

        o = runFx("native:transform", {{"rotation", 90.0}}, q, n, n);
        expectPx("rotacao 90: vermelho vai ao canto superior direito", o, n, 12, 4, {255, 0, 0, 255});
        expectPx("rotacao 90: verde desce", o, n, 12, 12, {0, 255, 0, 255});
        expectPx("rotacao 90: branco vai a esquerda", o, n, 4, 12, {255, 255, 255, 255});
        expectPx("rotacao 90: azul sobe", o, n, 4, 4, {0, 0, 255, 255});

        o = runFx("native:transform", {{"opacity", 50.0}}, q, n, n);
        expectPx("opacidade 50%", o, n, 4, 4, {255, 0, 0, 128}, 2);

        o = runFx("native:transform", {{"scale", 200.0}}, q, n, n);
        expectPx("escala 200%: o vermelho cobre o quadrante", o, n, 2, 2, {255, 0, 0, 255});
        expectPx("escala 200%: ... e o canto oposto vira outra cor", o, n, 13, 13, {255, 255, 255, 255});

        const GLuint tb = makeTexture(n, n, [](int, int y) { return y == 15 ? Px{255, 0, 0, 255} : Px{0, 0, 0, 255}; });
        o = runFx("native:transform", {{"posY", 25.0}}, tb, n, n, false);
        expectPx("posY desce (base-primeiro)", o, n, 8, 11, {255, 0, 0, 255});
        expectPx("posY desce (base-primeiro): vazio no topo", o, n, 8, 15, {0, 0, 0, 0});
        glDeleteTextures(1, &tb);
        glDeleteTextures(1, &t);
    }

    section("ajuste de cor");
    {
        const GLuint gray = makeTexture(n, n, [](int, int) { return Px{128, 128, 128, 255}; });
        const GLuint red = makeTexture(n, n, [](int, int) { return Px{255, 0, 0, 255}; });
        const GLuint dark = makeTexture(n, n, [](int, int) { return Px{64, 64, 64, 255}; });
        expectPx("sem ajuste = igual", runFx("native:color", {}, gray, n, n), n, 3, 3, {128, 128, 128, 255}, 1);
        expectPx("brilho +0,2", runFx("native:color", {{"brightness", 0.2}}, gray, n, n), n, 3, 3, {179, 179, 179, 255});
        expectPx("saturacao 0", runFx("native:color", {{"saturation", 0.0}}, red, n, n), n, 3, 3, {54, 54, 54, 255}, 2);
        expectPx("matiz +120", runFx("native:color", {{"hue", 120.0}}, red, n, n), n, 3, 3, {0, 255, 0, 255}, 4);
        expectPx("gamma 2", runFx("native:color", {{"gamma", 2.0}}, dark, n, n), n, 3, 3, {128, 128, 128, 255}, 3);
        expectPx("temperatura quente", runFx("native:color", {{"temperature", 1.0}}, gray, n, n), n, 3, 3, {167, 133, 90, 255}, 4);
        const Img c = runFx("native:color", {{"contrast", 0.5}}, dark, n, n);
        CHECK(at(c, n, 3, 3).r < 64);
        glDeleteTextures(1, &gray);
        glDeleteTextures(1, &red);
        glDeleteTextures(1, &dark);
    }

    section("niveis");
    {
        const GLuint t = makeTexture(n, n, [](int x, int) { return x < 8 ? Px{128, 128, 128, 255} : Px{255, 255, 255, 255}; });
        Img o = runFx("native:levels", {{"inBlack", 0.5}}, t, n, n);
        expectPx("preto de entrada 0,5 zera o cinza medio", o, n, 3, 3, {1, 1, 1, 255}, 2);
        expectPx("branco continua", o, n, 12, 3, {255, 255, 255, 255});
        o = runFx("native:levels", {{"outWhite", 0.5}}, t, n, n);
        expectPx("branco de saida 0,5", o, n, 12, 3, {128, 128, 128, 255}, 2);
        o = runFx("native:levels", {{"outBlack", 0.5}}, t, n, n);
        expectPx("preto de saida 0,5", o, n, 3, 3, {191, 191, 191, 255}, 3);
        const GLuint dark = makeTexture(n, n, [](int, int) { return Px{64, 64, 64, 255}; });
        expectPx("gamma 2", runFx("native:levels", {{"gamma", 2.0}}, dark, n, n), n, 3, 3, {128, 128, 128, 255}, 3);
        glDeleteTextures(1, &dark);
        glDeleteTextures(1, &t);
    }

    section("desfoque");
    {
        const int m = 32;
        auto block = [](int x, int y) { return (x >= 14 && x <= 17 && y >= 14 && y <= 17) ? Px{255, 255, 255, 255} : Px{0, 0, 0, 255}; };
        const GLuint dot = makeTexture(m, m, block);
        Img o = runFx("native:blur", {{"radius", 6.0}}, dot, m, m);
        const Px c = at(o, m, 16, 16);
        CHECK(c.r < 150 && c.r > 30);
        CHECK(at(o, m, 22, 16).r > 0);
        CHECK(std::abs(at(o, m, 20, 16).r - at(o, m, 11, 16).r) <= 3);
        CHECK(std::abs(at(o, m, 16, 20).r - at(o, m, 20, 16).r) <= 3);
        long sum = 0;
        for (const Px& p : o)
            sum += p.r;
        CHECK(sum > 3400 && sum < 4800);
        o = runFx("native:blur", {{"radius", 0.0}}, dot, m, m);
        expectPx("raio 0 = copia", o, m, 16, 16, {255, 255, 255, 255}, 1);
        expectPx("raio 0 = copia (2)", o, m, 19, 16, {0, 0, 0, 255}, 1);

        const GLuint alphaDot = makeTexture(m, m, [](int x, int y) { return (x >= 14 && x <= 17 && y >= 14 && y <= 17) ? Px{255, 255, 255, 255} : Px{0, 0, 0, 0}; });
        o = runFx("native:blur", {{"radius", 6.0}}, alphaDot, m, m);
        const Px e = at(o, m, 19, 16);
        CHECK(e.a > 20 && e.a < 200);
        CHECK(e.r >= 230 && e.g >= 230 && e.b >= 230);
        const GLuint big = makeTexture(64, 64, [](int x, int y) { return ((x / 8 + y / 8) % 2) ? Px{255, 255, 255, 255} : Px{0, 0, 0, 255}; });
        o = runFx("native:blur", {{"radius", 120.0}}, big, 64, 64);
        CHECK(std::abs(at(o, 64, 32, 32).r - 128) < 40);
        o = runFx("native:blur", {{"radius", 10.0}, {"quality", "high"}}, big, 64, 64, true, 0, 0, true );
        CHECK(at(o, 64, 32, 32).a == 255);
        glDeleteTextures(1, &big);
        glDeleteTextures(1, &alphaDot);
        glDeleteTextures(1, &dot);
    }

    section("caleidoscopio");
    {
        const GLuint grad = makeTexture(n, n, [](int x, int y) { return Px{x * 16, y * 16, 40, 255}; });
        Img o = runFx("native:kaleido", {{"segments", 2}}, grad, n, n);
        for (int y : {3, 5, 9, 12})
            CHECK(closeTo(at(o, n, 3, y), at(o, n, 12, y), 6));
        bool differs = false;
        for (size_t i = 0; i < o.size(); ++i)
            differs = differs || !closeTo(o[i], runFx("native:invert", {{"amount", 0.0}}, grad, n, n)[i], 8);
        CHECK(differs);
        o = runFx("native:kaleido", {{"segments", 6}, {"rotation", 30.0}, {"zoom", 1.5}}, grad, n, n);
        CHECK(at(o, n, 8, 8).a == 255);
        glDeleteTextures(1, &grad);
    }

    section("pixelizar");
    {
        const GLuint grad = makeTexture(n, n, [](int x, int) { return Px{x * 16, 0, 0, 255}; });
        Img o = runFx("native:pixelate", {{"size", 4.0}}, grad, n, n);
        CHECK(std::abs(at(o, n, 0, 5).r - at(o, n, 3, 5).r) <= 1);
        CHECK(std::abs(at(o, n, 4, 5).r - at(o, n, 7, 5).r) <= 1);
        CHECK(at(o, n, 4, 5).r - at(o, n, 0, 5).r >= 40);
        const GLuint white = makeTexture(n, n, [](int, int) { return Px{255, 255, 255, 255}; });
        o = runFx("native:pixelate", {{"size", 4.0}, {"led", true}, {"gap", 0.4}}, white, n, n);
        CHECK(at(o, n, 5, 5).r >= 190);
        CHECK(at(o, n, 4, 4).r <= 80);
        glDeleteTextures(1, &white);
        glDeleteTextures(1, &grad);
    }

    section("strobe");
    {
        const GLuint white = makeTexture(n, n, [](int, int) { return Px{255, 255, 255, 255}; });
        CHECK(at(runFx("native:strobe", {{"mode", "sync"}, {"division", "1"}, {"duty", 0.5}}, white, n, n, true, 0.25), n, 3, 3).a == 255);
        CHECK(at(runFx("native:strobe", {{"mode", "sync"}, {"division", "1"}, {"duty", 0.5}}, white, n, n, true, 0.75), n, 3, 3).a == 0);
        CHECK(at(runFx("native:strobe", {{"mode", "sync"}, {"division", "2"}, {"duty", 0.5}}, white, n, n, true, 0.1), n, 3, 3).a == 255);
        CHECK(at(runFx("native:strobe", {{"mode", "sync"}, {"division", "2"}, {"duty", 0.5}}, white, n, n, true, 0.3), n, 3, 3).a == 0);
        CHECK(at(runFx("native:strobe", {{"mode", "sync"}, {"division", "0.25"}, {"duty", 0.5}}, white, n, n, true, 1.0), n, 3, 3).a == 255);
        CHECK(at(runFx("native:strobe", {{"mode", "sync"}, {"division", "0.25"}, {"duty", 0.5}}, white, n, n, true, 3.0), n, 3, 3).a == 0);
        CHECK(at(runFx("native:strobe", {{"mode", "free"}, {"hz", 2.0}, {"duty", 0.5}}, white, n, n, true, 0.0, 0.2), n, 3, 3).a == 255);
        CHECK(at(runFx("native:strobe", {{"mode", "free"}, {"hz", 2.0}, {"duty", 0.5}}, white, n, n, true, 0.0, 0.3), n, 3, 3).a == 0);
        glDeleteTextures(1, &white);
    }

    section("vinheta");
    {
        const int m = 32;
        const GLuint white = makeTexture(m, m, [](int, int) { return Px{255, 255, 255, 255}; });
        const Img o = runFx("native:vignette", {}, white, m, m);
        CHECK(at(o, m, 16, 16).r >= 245);
        CHECK(at(o, m, 0, 0).r <= 150);
        CHECK(at(o, m, 0, 0).a == 255);
        glDeleteTextures(1, &white);
    }

    section("deslocamento de cor");
    {
        const GLuint dot = makeTexture(n, n, [](int x, int y) { return (x == 8 && y == 8) ? Px{255, 255, 255, 255} : Px{0, 0, 0, 255}; });
        const Img o = runFx("native:rgbsplit", {{"amount", 2.0}, {"angle", 0.0}}, dot, n, n);
        expectPx("vermelho a esquerda", o, n, 6, 8, {255, 0, 0, 255}, 4);
        expectPx("verde no centro", o, n, 8, 8, {0, 255, 0, 255}, 4);
        expectPx("azul a direita", o, n, 10, 8, {0, 0, 255, 255}, 4);
        glDeleteTextures(1, &dot);
    }
    glDeleteTextures(1, &q);
}

void testChain()
{
    section("cadeia: operacoes");
    {
        fx::EffectChain c;
        c.add("native:invert");
        c.add("native:blur");
        c.add("native:color");
        CHECK(c.size() == 3 && c.hasActive());
        CHECK(c.move(0, 2));
        CHECK(c.items[0].typeId == "native:blur" && c.items[2].typeId == "native:invert");
        CHECK(c.move(2, 0));
        CHECK(c.items[0].typeId == "native:invert");
        CHECK(c.duplicate(1));
        CHECK(c.size() == 4 && c.items[2].typeId == "native:blur");
        c.items[1].setParam("radius", 33.0);
        CHECK(c.duplicate(1) && c.items[2].params.json().value("radius", 0.0) == 33.0);
        CHECK(c.items[1].uid != c.items[2].uid);
        c.insert(1, "native:mirror");
        CHECK(c.items[1].typeId == "native:mirror");
        CHECK(c.remove(1) && !c.remove(99));
        CHECK(c.setBypass(0, true) && c.items[0].bypass);
        for (int i = 0; i < c.size(); ++i)
            c.setBypass(i, true);
        CHECK(!c.hasActive());
        fx::EffectChain d = fx::EffectChain::fromJson(c.toJson());
        CHECK(d.size() == c.size() && d.items[0].bypass && d.items[1].params.json() == c.items[1].params.json());
        CHECK(d.toJson() == c.toJson());
        const fx::EffectChain copy = c;
        CHECK(copy.size() == c.size() && copy.items[0].effect() == nullptr);
        std::vector<std::string> warn;
        fx::EffectChain bad = fx::EffectChain::fromJson(Json::parse(R"([{"type":"native:invert"},{"nada":1},5,{"type":"ffgl:x","bypass":true}])"), &warn);
        CHECK(bad.size() == 2 && bad.items[1].typeId == "ffgl:x" && bad.items[1].bypass && !warn.empty());
    }

    section("cadeia: ordem, bypass, pool");
    const int n = 8;
    const GLuint gray = makeTexture(n, n, [](int, int) { return Px{100, 100, 100, 255}; });
    FboPool pool;
    fx::EffectContext ctx = makeCtx(pool);

    auto runChain = [&](fx::EffectChain& chain) {
        FboPool::Lease l = fx::ChainRunner::run(chain, ctx, gray, n, n);
        return l ? readTexture(l->texture(), n, n) : readTexture(gray, n, n);
    };
    {
        fx::EffectChain a;
        a.add("native:color").setParam("brightness", 0.3);
        a.add("native:invert");
        fx::EffectChain b;
        b.add("native:invert");
        b.add("native:color").setParam("brightness", 0.3);
        const Px pa = at(runChain(a), n, 3, 3);
        const Px pb = at(runChain(b), n, 3, 3);
        CHECK(closeTo(pa, {78, 78, 78, 255}, 3));
        CHECK(closeTo(pb, {232, 232, 232, 255}, 3));
        CHECK(!closeTo(pa, pb, 20));

        const size_t before = pool.created();
        for (auto& e : a.items)
            e.bypass = true;
        {
            FboPool::Lease l = fx::ChainRunner::run(a, ctx, gray, n, n);
            CHECK(!l);
        }
        CHECK(pool.created() == before);
        const Img same = runChain(a);
        CHECK(closeTo(at(same, n, 3, 3), {100, 100, 100, 255}, 0));
        a.items[1].bypass = false;
        CHECK(closeTo(at(runChain(a), n, 3, 3), {155, 155, 155, 255}, 2));
        fx::EffectChain ii;
        ii.add("native:invert");
        ii.add("native:invert");
        CHECK(closeTo(at(runChain(ii), n, 3, 3), {100, 100, 100, 255}, 2));
        fx::EffectChain unk;
        unk.add("ffgl:inexistente");
        unk.add("native:invert");
        CHECK(closeTo(at(runChain(unk), n, 3, 3), {155, 155, 155, 255}, 2));
    }
    {
        fx::EffectChain c;
        c.add("native:invert");
        c.add("native:color").setParam("brightness", 0.1);
        c.add("native:blur").setParam("radius", 8.0);
        runChain(c);
        const size_t created = pool.created();
        for (int i = 0; i < 20; ++i) {
            FboPool::Lease l = fx::ChainRunner::run(c, ctx, gray, n, n);
            pool.endFrame();
        }
        CHECK(pool.created() == created);
        CHECK(pool.inUse() == 0);
    }
    glDeleteTextures(1, &gray);
}

class TestSource final : public media::ISource
{
public:
    TestSource(int w, int h, const std::function<Px(int, int)>& f) : w_(w), h_(h), tex_(makeTexture(w, h, f)) {}
    ~TestSource() override { glDeleteTextures(1, &tex_); }
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
    GLuint tex_;
    bool open_ = true;
};

struct Scene
{
    Composition comp = Composition::createDefault(1, 1);
    std::unique_ptr<Compositor> comper;
    int cw, ch;

    Scene(int canvasW, int canvasH, int srcW, int srcH, const std::function<Px(int, int)>& f) : cw(canvasW), ch(canvasH)
    {
        comp.width = canvasW;
        comp.height = canvasH;
        comper = std::make_unique<Compositor>(canvasW, canvasH);
        Clip& clip = comp.clip(0, 0);
        clip.type = Clip::Type::File;
        clip.name = "teste";
        clip.fit = FitMode::Stretch;
        clip.source = std::make_unique<TestSource>(srcW, srcH, f);
        comp.layers[0].activeDeck = 0;
        comp.layers[0].activeColumn = 0;
    }
    Clip& clip() { return comp.clip(0, 0); }
    Layer& layer() { return comp.layers[0]; }

    Px visual(const Img& img, int x, int y, int rh) const { return img[static_cast<size_t>(rh - 1 - y) * static_cast<size_t>(comper->width()) + static_cast<size_t>(x)]; }
    Img frame(float scale = 1.0f, bool eco = false)
    {
        comper->render(comp, scale, eco);
        return readTexture(comper->texture(), comper->width(), comper->height());
    }
};

Px redTopLeft(int x, int y)
{
    return (x < 16 && y < 16) ? Px{255, 0, 0, 255} : Px{0, 0, 0, 255};
}

bool isRed(Px p)
{
    return p.r > 240 && p.g < 15 && p.b < 15;
}

bool isBlack(Px p)
{
    return p.r < 15 && p.g < 15 && p.b < 15;
}

void testCompositor()
{
    section("compositor: orientacao e cadeias");
    {
        Scene s(32, 32, 32, 32, redTopLeft);
        Img base = s.frame();
        CHECK(isRed(s.visual(base, 4, 4, 32)) && isBlack(s.visual(base, 4, 28, 32)) && isBlack(s.visual(base, 28, 4, 32)));

        for (int where = 0; where < 3; ++where) {
            Scene t(32, 32, 32, 32, redTopLeft);
            fx::EffectChain& chain = where == 0 ? t.clip().effects : (where == 1 ? t.layer().effects : t.comp.masterEffects);
            chain.add("native:transform").setParam("posY", 25.0);
            const Img o = t.frame();
            const std::string tag = where == 0 ? "clip" : (where == 1 ? "camada" : "master");
            report(isRed(t.visual(o, 4, 12, 32)), tag + ": o vermelho desceu");
            report(isBlack(t.visual(o, 4, 4, 32)), tag + ": o topo ficou vazio (preto)");
            report(isBlack(t.visual(o, 28, 12, 32)), tag + ": a direita continua preta");
        }
        {
            Scene t(32, 32, 32, 32, redTopLeft);
            t.clip().effects.add("native:transform").setParam("posY", 25.0);
            t.layer().effects.add("native:transform").setParam("posY", 25.0);
            const Img o = t.frame();
            CHECK(isRed(t.visual(o, 4, 20, 32)) && isBlack(t.visual(o, 4, 12, 32)));
        }
        {
            Scene t(32, 32, 32, 32, redTopLeft);
            t.comp.masterEffects.add("native:transform").setParam("posX", 50.0);
            const Img o = t.frame();
            CHECK(isRed(t.visual(o, 20, 4, 32)) && isBlack(t.visual(o, 4, 4, 32)));
        }
        {
            Scene t(32, 32, 32, 32, redTopLeft);
            t.clip().effects.add("native:invert").bypass = true;
            t.layer().effects.add("native:blur").bypass = true;
            t.comp.masterEffects.add("native:color").bypass = true;
            const Img o = t.frame();
            bool same = o.size() == base.size();
            for (size_t i = 0; same && i < o.size(); ++i)
                same = closeTo(o[i], base[i], 0);
            CHECK(same);
        }
        {
            Scene t(32, 32, 32, 32, redTopLeft);
            t.clip().effects.add("native:invert");
            const Img o = t.frame();
            CHECK(closeTo(t.visual(o, 4, 4, 32), {0, 255, 255, 255}, 3) && closeTo(t.visual(o, 28, 28, 32), {255, 255, 255, 255}, 3));
        }
        {
            Scene t(32, 32, 32, 32, redTopLeft);
            t.comp.masterOpacity = 0.5f;
            t.comp.masterEffects.add("native:invert").bypass = true;
            const Img o = t.frame();
            CHECK(closeTo(t.visual(o, 4, 4, 32), {128, 0, 0, 255}, 4));
        }
    }

    section("compositor: mascara por luminancia");
    {
        auto build = [](BlendMode mode, float opacity) {
            auto t = std::make_unique<Scene>(32, 32, 32, 32, [](int, int) { return Px{255, 0, 0, 255}; });
            t->comp.addLayer();
            Clip& mask = t->comp.clip(1, 0);
            mask.type = Clip::Type::File;
            mask.name = "mascara";
            mask.fit = FitMode::Stretch;
            mask.source = std::make_unique<TestSource>(32, 32, [](int x, int) {
                return x < 16 ? Px{255, 255, 255, 255} : Px{0, 0, 0, 255};
            });
            t->comp.layers[1].activeDeck = 0;
            t->comp.layers[1].activeColumn = 0;
            t->comp.layers[1].blendMode = mode;
            t->comp.layers[1].opacity = opacity;
            return t;
        };
        {
            auto t = build(BlendMode::LumaMask, 1.0f);
            const Img o = t->frame();
            CHECK(isRed(t->visual(o, 4, 16, 32)) && isBlack(t->visual(o, 28, 16, 32)));
        }
        {
            auto t = build(BlendMode::LumaMaskInv, 1.0f);
            const Img o = t->frame();
            CHECK(isBlack(t->visual(o, 4, 16, 32)) && isRed(t->visual(o, 28, 16, 32)));
        }
        {
            auto t = build(BlendMode::LumaMask, 0.5f);
            const Img o = t->frame();
            CHECK(isRed(t->visual(o, 4, 16, 32)) && closeTo(t->visual(o, 28, 16, 32), {128, 0, 0, 255}, 4));
        }
        {
            auto t = build(BlendMode::LumaMask, 0.0f);
            const Img o = t->frame();
            CHECK(isRed(t->visual(o, 4, 16, 32)) && isRed(t->visual(o, 28, 16, 32)));
        }
        {
            auto t = build(BlendMode::Normal, 1.0f);
            const Img o = t->frame();
            CHECK(closeTo(t->visual(o, 4, 16, 32), {255, 255, 255, 255}, 3) && isBlack(t->visual(o, 28, 16, 32)));
        }
    }

    section("compositor: encaixe com efeitos na camada");
    {
        Scene t(32, 32, 32, 16, [](int, int) { return Px{255, 0, 0, 255}; });
        t.clip().fit = FitMode::Fit;
        const Img plain = t.frame();
        t.layer().effects.add("native:invert");
        const Img inv = t.frame();
        CHECK(isRed(t.visual(plain, 16, 16, 32)) && isBlack(t.visual(plain, 16, 2, 32)));
        CHECK(closeTo(t.visual(inv, 16, 16, 32), {0, 255, 255, 255}, 3));
        CHECK(isBlack(t.visual(inv, 16, 2, 32)));
    }

    section("compositor: previa reduzida e Eco");
    {
        Scene t(64, 64, 64, 64, [](int x, int y) { return (x < 32 && y < 32) ? Px{255, 0, 0, 255} : Px{0, 0, 0, 255}; });
        t.clip().effects.add("native:transform").setParam("posY", 25.0);
        t.layer().effects.add("native:color");
        t.comp.masterEffects.add("native:invert");
        t.comp.masterEffects.add("native:invert");
        const Img o = t.frame(0.5f);
        CHECK(isRed(t.visual(o, 4, 12, 32)) && isBlack(t.visual(o, 4, 2, 32)));
        const Img eco = t.frame(0.5f, true);
        CHECK(isRed(t.visual(eco, 4, 12, 32)));
        t.clip().effects.add("native:blur").setParam("radius", 10.0);
        const Img eco2 = t.frame(1.0f, true);
        CHECK(eco2[0].a == 255);
    }

    section("compositor: sem realocar por quadro");
    {
        Scene t(64, 64, 64, 64, redTopLeft);
        t.clip().effects.add("native:blur").setParam("radius", 12.0);
        t.layer().effects.add("native:color");
        t.comp.masterEffects.add("native:vignette");
        t.frame();
        const size_t created = t.comper->pool().created();
        for (int i = 0; i < 10; ++i)
            t.frame();
        CHECK(t.comper->pool().created() == created);
        CHECK(t.comper->pool().inUse() == 0);
    }
}

fs::path findIsfDir(int argc, char** argv)
{
    std::error_code ec;
    if (argc > 1 && fs::is_directory(argv[1], ec))
        return fs::absolute(argv[1], ec);
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path dir = fs::path(std::wstring(buf, len)).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        if (fs::is_directory(dir / "assets" / "isf", ec))
            return dir / "assets" / "isf";
        dir = dir.parent_path();
    }
    return {};
}

void testIsfParse(const fs::path& isfDir)
{
    section("ISF: expressoes e macros");
    {
        const fx::IsfVariableLookup vars = [](const std::string& n, double& v) {
            if (n == "WIDTH") { v = 100; return true; }
            if (n == "HEIGHT") { v = 40; return true; }
            if (n == "div") { v = 4; return true; }
            return false;
        };
        double r = 0;
        CHECK(fx::evalIsfExpression("$WIDTH/2+1", vars, r) && r == 51.0);
        CHECK(fx::evalIsfExpression("($WIDTH + $HEIGHT) * 2 - 10 / 5", vars, r) && r == 278.0);
        CHECK(fx::evalIsfExpression("max($WIDTH, 3) / $div", vars, r) && r == 25.0);
        CHECK(fx::evalIsfExpression("floor(10.7)", vars, r) && r == 10.0);
        CHECK(fx::evalIsfExpression("-$HEIGHT", vars, r) && r == -40.0);
        CHECK(fx::evalIsfExpression("64", vars, r) && r == 64.0);
        CHECK(!fx::evalIsfExpression("1 +", vars, r));
        CHECK(!fx::evalIsfExpression("$nada", vars, r));
        CHECK(!fx::evalIsfExpression("1/0", vars, r));
        CHECK(!fx::evalIsfExpression("2 3", vars, r));

        CHECK(fx::rewriteIsfSource("vec4 c = IMG_NORM_PIXEL(inputImage, uv + vec2(0.1, f(2.0)));", true)
              == "vec4 c = _isf_normPixel(inputImage, _inputImage_flip, uv + vec2(0.1, f(2.0)));");
        CHECK(fx::rewriteIsfSource("vec2 s = IMG_SIZE(img);", true) == "vec2 s = _img_imgSize;");
        CHECK(fx::rewriteIsfSource("IMG_THIS_PIXEL(a)", true) == "_isf_normPixel(a, _a_flip, isf_FragNormCoord)");
        CHECK(fx::rewriteIsfSource("IMG_PIXEL(a, gl_FragCoord.xy)", true)
              == "_isf_pixel(a, _a_imgSize, _a_flip, gl_FragCoord.xy)");
        CHECK(fx::rewriteIsfSource("gl_FragColor = texture2D(t, uv); varying vec2 v;", true)
              == "isf_FragColor = texture(t, uv); in vec2 v;");
        CHECK(fx::rewriteIsfSource("varying vec2 v;", false) == "out vec2 v;");
        CHECK(fx::rewriteIsfSource("// IMG_SIZE(x) gl_FragColor\nfloat x = 1.0e-3;", true) == "// IMG_SIZE(x) gl_FragColor\nfloat x = 1.0e-3;");
        const std::string multi = "a = IMG_NORM_PIXEL(\n   inputImage,\n   uv);\nb;";
        const std::string rew = fx::rewriteIsfSource(multi, true);
        CHECK(std::count(rew.begin(), rew.end(), '\n') == std::count(multi.begin(), multi.end(), '\n'));

        fx::IsfCompileLog l = fx::remapGlslLog("0(37) : error C1008: undefined variable \"x\"\n", 29, "fragment");
        CHECK(l.firstLine == 8 && l.text.find("Linha 8:") != std::string::npos);
        l = fx::remapGlslLog("ERROR: 0:37: 'x' : undeclared identifier\nERROR: 1 compilation errors.  No code generated.\n", 29, "fragment");
        CHECK(l.firstLine == 8 && l.text.find("Linha 8:") == 0);
        l = fx::remapGlslLog("0:37(5): error: `x' undeclared\n", 29, "fragment");
        CHECK(l.firstLine == 8);
    }

    section("ISF: cabecalho");
    {
        fx::IsfParseResult r = fx::parseIsf("/*{\n \"A\": 1,\n \"B\": ,\n}*/\nvoid main(){}", "", "x");
        CHECK(!r.ok && r.error.find("linha 3") != std::string::npos);
        r = fx::parseIsf("void main(){}", "", "x");
        CHECK(!r.ok);
        r = fx::parseIsf("/*{ \"INPUTS\": [", "", "x");
        CHECK(!r.ok);

        r = fx::parseIsf(R"(/*{ "INPUTS": [ {"NAME":"mode","TYPE":"long","VALUES":[10,20,30],"LABELS":["A","B","C"],"DEFAULT":20} ] }*/
void main(){ gl_FragColor = vec4(mode==10?1.0:0.0, mode==20?1.0:0.0, mode==30?1.0:0.0, 1.0); })", "", "longo");
        CHECK(r.ok && r.doc && r.doc->inputs.size() == 1);
        if (r.ok && r.doc) {
            const fx::IsfInput& in = r.doc->inputs[0];
            CHECK(in.type == fx::IsfInputType::Long && in.values == std::vector<long long>({10, 20, 30}));
            CHECK(in.labels.size() == 3 && in.labels[1] == "B" && in.defV == 20.0);
            CHECK(!r.doc->isEffect());
            const auto descs = fx::isfParamDescs(*r.doc);
            CHECK(descs.size() == 1 && descs[0].type == media::ParamType::Enum && descs[0].options.size() == 3);
            CHECK(descs[0].options[2].value == "30" && descs[0].options[2].label == "C" && descs[0].def == Json("20"));
        }

        r = fx::parseIsf(R"(/*{
  "DESCRIPTION": "d", "CREDIT": "c", "CATEGORIES": ["Feedback"],
  "INPUTS": [
    {"NAME":"inputImage","TYPE":"image"},
    {"NAME":"amt","TYPE":"float","MIN":-2,"MAX":4,"DEFAULT":1},
    {"NAME":"on","TYPE":"bool","DEFAULT":true},
    {"NAME":"col","TYPE":"color","DEFAULT":[0.1,0.2,0.3,0.4]},
    {"NAME":"pt","TYPE":"point2D","DEFAULT":[10,20],"MIN":[0,0],"MAX":[100,200]},
    {"NAME":"go","TYPE":"event"},
    {"NAME":"snd","TYPE":"audio"},
    {"NAME":"fft","TYPE":"audioFFT"},
    {"NAME":"bad name","TYPE":"float"}
  ],
  "PASSES": [ {"TARGET":"bufA","PERSISTENT":true,"FLOAT":true,"WIDTH":"$WIDTH/2","HEIGHT":"$HEIGHT/2"}, {} ],
  "IMPORTED": { "tex": {"PATH":"tex.png"} }
}*/
void main(){})", "", "multi");
        CHECK(r.ok && r.doc);
        if (r.ok && r.doc) {
            const fx::IsfDoc& d = *r.doc;
            CHECK(d.isEffect() && d.categories.size() == 1 && d.description == "d" && d.credit == "c");
            CHECK(d.passes.size() == 2 && d.passes[0].target == "bufA" && d.passes[0].persistent && d.passes[0].isFloat);
            CHECK(d.passes[0].widthExpr == "$WIDTH/2" && d.passes[1].target.empty() && !d.passes[1].persistent);
            CHECK(d.imported.size() == 1 && d.imported[0].name == "tex" && d.imported[0].path == "tex.png");
            CHECK(d.inputs.size() == 8);
            CHECK(d.warnings.size() >= 2);
            const auto descs = fx::isfParamDescs(d);
            CHECK(descs.size() == 5);
            CHECK(descs[0].min == -2.0 && descs[0].max == 4.0 && descs[0].def.get<double>() == 1.0);
            CHECK(descs[3].type == media::ParamType::Custom && descs[3].custom == "point2d" && descs[3].max == 200.0);
            CHECK(descs[4].custom == "event");
            const fx::IsfSources src = fx::buildIsfSources(d);
            CHECK(src.imageNames.size() == 5);
            CHECK(src.fragment.find("uniform sampler2D bufA;") != std::string::npos);
            CHECK(src.fragment.find("uniform vec2 pt;") != std::string::npos);
        }
    }

    section("ISF: shaders proprios");
    if (isfDir.empty()) {
        report(false, "pasta assets/isf nao encontrada");
        return;
    }
    struct Expect { const char* file; bool effect; size_t passes; };
    for (const Expect& e : {Expect{"Gradiente animado.fs", false, 0}, Expect{"Grade LED de teste.fs", false, 0},
                            Expect{"Ondas.fs", true, 0}, Expect{"Feedback.fs", true, 2}}) {
        const fs::path f = isfDir / e.file;
        const std::string text = readAll(f);
        const fx::IsfParseResult r = fx::parseIsf(text, "", f.stem().string());
        report(r.ok, std::string("le ") + e.file + (r.ok ? "" : ": " + r.error));
        if (!r.ok)
            continue;
        report(r.doc->isEffect() == e.effect, std::string("classificacao de ") + e.file);
        report(r.doc->passes.size() == e.passes, std::string("passes de ") + e.file);
        report(!r.doc->description.empty() && !r.doc->inputs.empty(), std::string("descricao e inputs de ") + e.file);
        report(r.doc->warnings.empty(), std::string("sem avisos em ") + e.file);
    }
}

struct IsfRun
{
    std::unique_ptr<fx::IsfRenderer> renderer;
    bool ok = false;
    std::string error;
    int errorLine = 0;
    Img img;
};

IsfRun runIsf(const std::string& source, const ParamList& params, GLuint input, int w, int h, int frames = 1,
              const fs::path& baseDir = {}, bool eco = false)
{
    IsfRun run;
    const fx::IsfParseResult r = fx::parseIsf(source, "", "teste");
    if (!r.ok) {
        run.error = r.error;
        return run;
    }
    run.renderer = std::make_unique<fx::IsfRenderer>(r.doc, baseDir);
    media::ParamValues values;
    for (const auto& [k, v] : params)
        values.set(k, v);
    run.renderer->setParams(media::ParamSet(fx::isfParamDescs(*r.doc), values));
    Fbo out(w, h);
    fx::EffectContext ctx;
    ctx.dt = 1.0 / 60.0;
    ctx.eco = eco;
    for (int i = 0; i < frames; ++i)
        run.ok = run.renderer->render(ctx, input, w, h, w, h, out);
    run.error = run.renderer->error();
    run.errorLine = run.renderer->firstErrorLine();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (run.ok)
        run.img = readTexture(out.texture(), w, h);
    return run;
}

void testIsfRender(const fs::path& isfDir)
{
    section("ISF: execucao");
    const int n = 16;
    const GLuint q = makeTexture(n, n, [&](int x, int y) { return quad(x, y, n); });

    {
        const std::string src = R"(/*{ "INPUTS": [ {"NAME":"mode","TYPE":"long","VALUES":[10,20,30],"LABELS":["A","B","C"],"DEFAULT":20} ] }*/
void main(){ gl_FragColor = vec4(mode==10?1.0:0.0, mode==20?1.0:0.0, mode==30?1.0:0.0, 1.0); })";
        IsfRun r = runIsf(src, {}, 0, n, n);
        CHECK(r.ok);
        if (r.ok)
            expectPx("long padrao = 20", r.img, n, 4, 4, {0, 255, 0, 255});
        r = runIsf(src, {{"mode", "30"}}, 0, n, n);
        if (r.ok)
            expectPx("long = 30", r.img, n, 4, 4, {0, 0, 255, 255});
        r = runIsf(src, {{"mode", "10"}}, 0, n, n);
        if (r.ok)
            expectPx("long = 10", r.img, n, 4, 4, {255, 0, 0, 255});
    }
    {
        const std::string src = R"(/*{
  "PASSES": [ {"TARGET":"acc","PERSISTENT":true,"FLOAT":true,"WIDTH":"$WIDTH/2","HEIGHT":"$HEIGHT/2"}, {} ]
}*/
void main(){
  if (PASSINDEX == 0) { vec4 p = IMG_THIS_NORM_PIXEL(acc); gl_FragColor = vec4(p.r + 0.1, 0.0, 0.0, 1.0); }
  else gl_FragColor = IMG_THIS_NORM_PIXEL(acc);
})";
        IsfRun r = runIsf(src, {}, 0, n, n, 3);
        CHECK(r.ok);
        if (r.ok)
            expectPx("persistente acumulou 3 quadros", r.img, n, 8, 8, {77, 0, 0, 255}, 3);
        r = runIsf(src, {}, 0, n, n, 6);
        if (r.ok)
            expectPx("persistente acumulou 6 quadros", r.img, n, 3, 12, {153, 0, 0, 255}, 3);
    }
    {
        const std::string src = "/*{ \"INPUTS\": [ {\"NAME\":\"inputImage\",\"TYPE\":\"image\"} ] }*/\nvoid main(){ gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage); }";
        IsfRun r = runIsf(src, {}, q, n, n);
        CHECK(r.ok);
        if (r.ok) {
            expectPx("passa-direto (topo)", r.img, n, 4, 4, {255, 0, 0, 255});
            expectPx("passa-direto (base)", r.img, n, 4, 12, {0, 0, 255, 255});
        }
        const std::string top = "/*{ }*/\nvoid main(){ gl_FragColor = isf_FragNormCoord.y > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0); }";
        r = runIsf(top, {}, 0, n, n);
        if (r.ok) {
            expectPx("gerador: parte de cima (linha 0 = topo)", r.img, n, 8, 2, {255, 0, 0, 255});
            expectPx("gerador: parte de baixo", r.img, n, 8, 13, {0, 0, 255, 255});
        }
    }
    {
        const std::string bad = "/*{\n  \"DESCRIPTION\": \"x\"\n}*/\n\nvoid main()\n{\n    float a = 1.0;\n    gl_FragColor = vec4(undeclaredThing, a, 0.0, 1.0);\n}\n";
        const IsfRun r = runIsf(bad, {}, 0, n, n);
        CHECK(!r.ok);
        std::printf("  erro do driver (linha esperada 8, obtida %d):\n    %s\n", r.errorLine, r.error.c_str());
        CHECK(r.errorLine == 8);
        CHECK(r.error.find("Linha 8") != std::string::npos);
        const std::string bad2 = "/*{ }*/\n#define K 1.0\nvoid main()\n{\n    gl_FragColor = vec4(K, 0.0, 0.0, 1.0)\n}\n";
        const IsfRun r2 = runIsf(bad2, {}, 0, n, n);
        CHECK(!r2.ok && r2.errorLine >= 5 && r2.errorLine <= 6);
        fx::EffectInstance inst("native:invert");
        (void)inst;
    }

    {
        const std::string src = R"(/*{ "INPUTS": [
  {"NAME":"amt","TYPE":"float","MIN":0,"MAX":2,"DEFAULT":0.25},
  {"NAME":"on","TYPE":"bool","DEFAULT":false},
  {"NAME":"col","TYPE":"color","DEFAULT":[0.0,0.0,1.0,1.0]},
  {"NAME":"pt","TYPE":"point2D","DEFAULT":[0,0],"MIN":[0,0],"MAX":[100,100]} ] }*/
void main(){ gl_FragColor = on ? vec4(col.rg + pt / 100.0, amt, 1.0) : vec4(col.rgb * 0.5, 1.0); })";
        IsfRun r = runIsf(src, {}, 0, n, n);
        if (r.ok)
            expectPx("padroes: bool desligado", r.img, n, 8, 8, {0, 0, 128, 255});
        r = runIsf(src, {{"on", true}, {"amt", 1.0}, {"col", media::param::colorJson({0.2f, 0.0f, 0.0f, 1.0f})}, {"pt", Json::array({50, 100})}}, 0, n, n);
        if (r.ok)
            expectPx("bool, float, cor e point2D", r.img, n, 8, 8, {179, 255, 255, 255}, 3);
    }
    {
        const std::string src = R"(/*{ "INPUTS": [ {"NAME":"go","TYPE":"event"} ] }*/
void main(){ gl_FragColor = go ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0); })";
        const fx::IsfParseResult p = fx::parseIsf(src, "", "ev");
        CHECK(p.ok);
        if (p.ok) {
            fx::IsfRenderer r(p.doc, fs::path());
            const auto descs = fx::isfParamDescs(*p.doc);
            auto setCounter = [&](int c) { media::ParamValues v; v.set("go", c); r.setParams(media::ParamSet(descs, v)); };
            Fbo out(n, n);
            fx::EffectContext ctx;
            setCounter(0);
            r.render(ctx, 0, 0, 0, n, n, out);
            expectPx("evento: parado", readTexture(out.texture(), n, n), n, 4, 4, {0, 255, 0, 255});
            setCounter(1);
            r.render(ctx, 0, 0, 0, n, n, out);
            expectPx("evento: disparou", readTexture(out.texture(), n, n), n, 4, 4, {255, 0, 0, 255});
            r.render(ctx, 0, 0, 0, n, n, out);
            expectPx("evento: so um quadro", readTexture(out.texture(), n, n), n, 4, 4, {0, 255, 0, 255});
            setCounter(5);
            setCounter(5);
            r.render(ctx, 0, 0, 0, n, n, out);
            expectPx("evento: contador alto dispara", readTexture(out.texture(), n, n), n, 4, 4, {255, 0, 0, 255});
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "bslstage_fxtest_imp";
        fs::create_directories(dir, ec);
        const unsigned char px[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        FILE* f = nullptr;
        if (_wfopen_s(&f, (dir / "tex.png").c_str(), L"wb") == 0 && f) {
            stbi_write_png_to_func([](void* ctx, void* data, int size) { std::fwrite(data, 1, static_cast<size_t>(size), static_cast<FILE*>(ctx)); },
                                   f, 2, 2, 4, px, 8);
            std::fclose(f);
        }
        const std::string src = R"(/*{ "IMPORTED": { "tex": {"PATH": "tex.png"} } }*/
void main(){ gl_FragColor = IMG_NORM_PIXEL(tex, isf_FragNormCoord); })";
        IsfRun r = runIsf(src, {}, 0, 8, 8, 1, dir);
        CHECK(r.ok);
        if (r.ok) {
            expectPx("imported: canto superior esquerdo", r.img, 8, 1, 1, {255, 0, 0, 255});
            expectPx("imported: canto superior direito", r.img, 8, 6, 1, {0, 255, 0, 255});
            expectPx("imported: canto inferior esquerdo", r.img, 8, 1, 6, {0, 0, 255, 255});
        }
        r = runIsf(R"(/*{ "IMPORTED": { "falta": {"PATH": "nao-existe.png"} } }*/
void main(){ gl_FragColor = IMG_NORM_PIXEL(falta, isf_FragNormCoord) + vec4(0.0, 0.0, 0.0, 1.0); })", {}, 0, 8, 8, 1, dir);
        CHECK(r.ok && !r.renderer->warnings().empty());
        fs::remove_all(dir, ec);
    }
    {
        const std::string src = R"(/*{ }*/
void main(){ gl_FragColor = vec4(isf_FragNormCoord.x, 0.0, 1.0 - isf_FragNormCoord.x, 1.0); })";
        IsfRun r = runIsf(src, {}, 0, 640, 360, 1, {}, true);
        CHECK(r.ok);
        if (r.ok) {
            CHECK(r.img.size() == 640u * 360u);
            expectPx("eco: gradiente a esquerda", r.img, 640, 5, 180, {2, 0, 253, 255}, 6);
            expectPx("eco: gradiente a direita", r.img, 640, 634, 180, {253, 0, 2, 255}, 6);
        }
    }

    if (isfDir.empty()) {
        glDeleteTextures(1, &q);
        return;
    }
    for (const char* name : {"Gradiente animado.fs", "Grade LED de teste.fs", "Ondas.fs", "Feedback.fs"}) {
        const std::string text = readAll(isfDir / name);
        const IsfRun r = runIsf(text, {}, q, 64 , 64, 3);
        report(r.ok, std::string("compila e roda ") + name + (r.ok ? "" : ": " + r.error));
        if (!r.ok)
            continue;
        bool anyAlpha = false, varied = false;
        for (const Px& p : r.img) {
            anyAlpha = anyAlpha || p.a > 0;
            varied = varied || !closeTo(p, r.img[0], 10);
        }
        report(anyAlpha, std::string(name) + " desenha algo");
        report(varied, std::string(name) + " nao e uma cor so");
    }
    {
        const std::string text = readAll(isfDir / "Ondas.fs");
        IsfRun r = runIsf(text, {{"amount", 0.0}}, q, n, n);
        CHECK(r.ok);
        if (r.ok)
            expectPx("ondas sem intensidade = entrada", r.img, n, 4, 4, {255, 0, 0, 255});
        const std::string fb = readAll(isfDir / "Feedback.fs");
        r = runIsf(fb, {{"rotate", 0.0}, {"zoom", 1.0}}, q, n, n, 1);
        CHECK(r.ok);
        if (r.ok)
            expectPx("feedback: quadro 1 = entrada", r.img, n, 4, 4, {255, 0, 0, 255});
    }
    glDeleteTextures(1, &q);
}

void testIsfLibrary(const fs::path& isfDir)
{
    section("ISF: pastas e catalogo");
    if (isfDir.empty()) {
        report(false, "pasta assets/isf nao encontrada");
        return;
    }
    std::error_code ec;
    const fs::path user = fs::temp_directory_path(ec) / "bslstage_fxtest_isf" / "ISF";
    fs::remove_all(user.parent_path(), ec);
    fx::IsfLibrary& lib = fx::IsfLibrary::instance();
    lib.configure({user}, isfDir);
    CHECK(fs::exists(user / "Ondas.fs") && fs::exists(user / "Feedback.fs") && fs::exists(user / "Gradiente animado.fs"));
    const fs::path extra = user.parent_path() / "extra";
    fs::create_directories(extra / "sub", ec);
    {
        std::ofstream(extra / "quebrado.fs") << "/*{ \"INPUTS\": [ }*/\nvoid main(){}";
        std::ofstream(extra / "sub" / "azul.fs") << "/*{ \"CATEGORIES\": [\"Generator\"] }*/\nvoid main(){ gl_FragColor = vec4(0.0,0.0,1.0,1.0); }";
        std::ofstream(extra / "sub" / "azul.vs") << "void main(){ isf_vertShaderInit(); }";
        std::ofstream(extra / "uv.fs") << "/*{ \"CATEGORIES\": [\"Generator\"] }*/\nvarying vec2 myUv;\nvoid main(){ gl_FragColor = vec4(myUv, 0.0, 1.0); }";
        std::ofstream(extra / "uv.vs") << "varying vec2 myUv;\nvoid main()\n{\n    isf_vertShaderInit();\n    myUv = isf_FragNormCoord * 0.5;\n}";
    }
    lib.configure({user, extra});
    lib.rescan();
    lib.waitScan();
    const auto entries = lib.entries();
    CHECK(entries.size() == 7);
    const auto ondas = lib.find("isf:Ondas.fs");
    CHECK(ondas && ondas->isEffect && ondas->doc && ondas->error.empty());
    const auto grad = lib.find("isf:Gradiente animado.fs");
    CHECK(grad && !grad->isEffect && grad->category == "Geradores");
    const auto bad = lib.find("isf:quebrado.fs");
    CHECK(bad && !bad->error.empty() && !bad->doc);
    const auto azul = lib.find("isf:sub/azul.fs");
    CHECK(azul && azul->doc && !azul->doc->vertexSource.empty());
    CHECK(!lib.find("isf:nao-existe.fs").has_value());
    {
        const int s = 16;
        for (const char* id : {"isf:sub/azul.fs", "isf:uv.fs"}) {
            const auto e = lib.find(id);
            CHECK(e && e->doc);
            if (!e || !e->doc)
                continue;
            fx::IsfRenderer r(e->doc, e->file.parent_path());
            Fbo out(s, s);
            fx::EffectContext ctx;
            const bool ok = r.render(ctx, 0, 0, 0, s, s, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            report(ok, std::string("renderiza ") + id + (ok ? "" : ": " + r.error()));
            if (!ok)
                continue;
            const Img img = readTexture(out.texture(), s, s);
            if (std::string(id) == "isf:sub/azul.fs") {
                expectPx("vs padrao do usuario", img, s, 8, 8, {0, 0, 255, 255});
            } else {
                expectPx("varying do vs ao fs (canto inferior esquerdo)", img, s, 0, s - 1, {4, 4, 0, 255}, 6);
                expectPx("varying do vs ao fs (canto superior direito)", img, s, s - 1, 0, {123, 123, 0, 255}, 8);
            }
        }
    }

    auto& reg = fx::EffectRegistry::instance();
    const auto info = reg.find("isf:Ondas.fs");
    CHECK(info && info->section == fx::EffectSection::Isf && !info->generator && info->category == "Distorção");
    CHECK(reg.find("isf:Gradiente animado.fs")->generator);
    CHECK(!reg.find("isf:quebrado.fs")->error.empty());
    const auto descs = reg.describe("isf:Ondas.fs");
    CHECK(descs.size() == 5 && descs[0].id == "mode");
    CHECK(reg.create("isf:Gradiente animado.fs") == nullptr);
    CHECK(reg.create("isf:quebrado.fs") == nullptr);

    const int n = 16;
    const GLuint q = makeTexture(n, n, [&](int x, int y) { return quad(x, y, n); });
    {
        const Img o = runFx("isf:Ondas.fs", {{"amount", 0.0}}, q, n, n);
        expectPx("efeito ISF na cadeia (sem intensidade)", o, n, 4, 4, {255, 0, 0, 255});
        const Img o2 = runFx("isf:Ondas.fs", {{"amount", 0.0}}, q, n, n, false);
        expectPx("efeito ISF em textura de base-primeiro", o2, n, 4, 4, {255, 0, 0, 255});
        expectPx("efeito ISF em textura de base-primeiro (2)", o2, n, 4, 12, {0, 0, 255, 255});
    }
    glDeleteTextures(1, &q);
    fs::remove_all(user.parent_path(), ec);
    lib.configure({isfDir});
    lib.rescan();
    lib.waitScan();
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
    GLFWwindow* win = glfwCreateWindow(64, 64, "fxtest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    const fs::path isfDir = findIsfDir(argc, argv);
    std::printf("ISF: %s\n", isfDir.empty() ? "(nao encontrada)" : isfDir.string().c_str());
    if (!isfDir.empty()) {
        fx::IsfLibrary::instance().configure({isfDir});
        fx::IsfLibrary::instance().rescan();
    }

    testRegistry();
    testNative();
    testChain();
    testCompositor();
    testIsfParse(isfDir);
    testIsfRender(isfDir);
    fx::IsfLibrary::instance().waitScan();
    testIsfLibrary(isfDir);

    {
        GLenum err;
        int glErrors = 0;
        while ((err = glGetError()) != GL_NO_ERROR) {
            std::printf("  erro de GL pendente: 0x%04X\n", err);
            ++glErrors;
        }
        report(glErrors == 0, "nenhum erro de GL");
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "FXTEST OK\n" : "FXTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
