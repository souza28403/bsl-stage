#include "core/Composition.h"
#include "core/LayerTransform.h"
#include "core/TransformHistory.h"
#include "output/OutputRenderer.h"
#include "output/OutputSetup.h"
#include "render/Compositor.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace bsl;

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

constexpr int W = 160;
constexpr int H = 90;

using Rgb = std::array<int, 3>;

struct Img
{
    int w = 0, h = 0;
    std::vector<unsigned char> px;
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

Rgb at(const Img& img, int x, int yTop)
{
    const int px = std::clamp(x, 0, img.w - 1);
    const int row = std::clamp(img.h - 1 - yTop, 0, img.h - 1);
    const size_t i = (static_cast<size_t>(row) * static_cast<size_t>(img.w) + static_cast<size_t>(px)) * 4;
    return {img.px[i], img.px[i + 1], img.px[i + 2]};
}

bool near(const Rgb& p, const Rgb& c, int tol = 2)
{
    return std::abs(p[0] - c[0]) <= tol && std::abs(p[1] - c[1]) <= tol && std::abs(p[2] - c[2]) <= tol;
}

std::string str(const Rgb& p)
{
    return "(" + std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + ")";
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

class PatternSource final : public media::ISource
{
public:
    PatternSource(int w, int h, const std::function<void(int, int, unsigned char*)>& f) : w_(w), h_(h)
    {
        tex_ = makeTexture(w, h, f);
    }
    ~PatternSource() override { glDeleteTextures(1, &tex_); }
    bool open(const std::filesystem::path&, std::string*) override { return open_ = true; }
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

std::unique_ptr<media::ISource> flat(int r, int g, int b)
{
    return std::make_unique<PatternSource>(W, H, [=](int, int, unsigned char* p) {
        p[0] = static_cast<unsigned char>(r);
        p[1] = static_cast<unsigned char>(g);
        p[2] = static_cast<unsigned char>(b);
        p[3] = 255;
    });
}

std::unique_ptr<media::ISource> gradient()
{
    return std::make_unique<PatternSource>(W, H, [](int x, int y, unsigned char* p) {
        p[0] = static_cast<unsigned char>(x * 255 / (W - 1));
        p[1] = static_cast<unsigned char>(y * 255 / (H - 1));
        p[2] = 128;
        p[3] = 255;
    });
}

std::unique_ptr<media::ISource> halves()
{
    return std::make_unique<PatternSource>(W, H, [](int x, int, unsigned char* p) {
        p[0] = x < W / 2 ? 255 : 0;
        p[1] = 0;
        p[2] = x < W / 2 ? 0 : 255;
        p[3] = 255;
    });
}

void putClip(Composition& comp, int layer, std::unique_ptr<media::ISource> src)
{
    Clip& clip = comp.clip(layer, 0);
    clip.type = Clip::Type::File;
    clip.name = "teste";
    clip.fit = FitMode::Stretch;
    clip.source = std::move(src);
    comp.layers[static_cast<size_t>(layer)].activeDeck = 0;
    comp.layers[static_cast<size_t>(layer)].activeColumn = 0;
}

Composition makeComp(int layers)
{
    Composition comp = Composition::createDefault(layers, 1);
    comp.width = W;
    comp.height = H;
    return comp;
}

Img shoot(Compositor& compositor, Composition& comp, float scale = 1.0f, const std::vector<LayerViewRequest>& views = {})
{
    compositor.render(comp, scale, false, views);
    return readTexture(compositor.texture(), W, H);
}

constexpr Rgb kBlack{0, 0, 0};
constexpr Rgb kRed{220, 40, 40};
constexpr Rgb kGreen{40, 200, 60};
constexpr Rgb kBlue{50, 70, 230};
constexpr Rgb kYellow{230, 210, 30};

void testGeometry()
{
    section("geometria do Transform (sem GL)");
    const LayerTransform id;
    CHECK(id.isDefault() && id.isIdentity(W, H));
    CHECK_MSG(!id.gpu(W, H).on, "identidade: o shader nem entra no caminho do Transform");

    LayerTransform t;
    t.w = W;
    t.h = H;
    CHECK_MSG(t.isIdentity(W, H) && !t.gpu(W, H).on, "L/A iguais ao canvas continuam sendo identidade");
    CHECK_MSG(t.settled(W, H).isDefault(), "settled leva L/A do canvas de volta ao padrao (0 = canvas)");
    t.anchorX = 0.0f;
    CHECK_MSG(t.isIdentity(W, H) && !t.isDefault(), "so a ancora mudou: nao muda a imagem, mas e diferente do padrao (e salvo)");
    t = LayerTransform::rect(10, 20, 80, 40);
    CHECK(!t.isIdentity(W, H) && t.gpu(W, H).on);

    LayerTransform r = LayerTransform::rect(30, 10, 80, 40);
    r.rotation = 30.0f;
    r.anchorX = 0.25f;
    r.anchorY = 0.75f;
    const OrientedBox b = r.box(W, H);
    LayerTransform back;
    back.anchorX = r.anchorX;
    back.anchorY = r.anchorY;
    back.setBox(b, W, H);
    CHECK_MSG(std::abs(back.x - r.x) < 1e-3f && std::abs(back.y - r.y) < 1e-3f && std::abs(back.w - 80.0f) < 1e-3f
                  && std::abs(back.h - 40.0f) < 1e-3f && std::abs(back.rotation - 30.0f) < 1e-3f,
              "box -> setBox devolve os mesmos campos (rotacao e ancora fora do centro)");
    LayerTransform c = LayerTransform::rect(0, 0, 100, 50);
    c.rotation = 90.0f;
    c.anchorX = 0.0f;
    c.anchorY = 0.0f;
    const OrientedBox cb = c.box(W, H);
    CHECK_MSG(std::abs(cb.cx - (-25.0f)) < 1e-3f && std::abs(cb.cy - 50.0f) < 1e-3f, "centro girado em volta da ancora (0,0)");

    LayerTransform q = LayerTransform::rect(80, 0, 80, 45);
    CHECK(q.contains(100, 10, W, H) && !q.contains(40, 10, W, H) && !q.contains(100, 60, W, H));
    q.cropL = 20;
    CHECK_MSG(!q.contains(90, 10, W, H) && q.contains(110, 10, W, H), "o recorte tira a faixa da esquerda");
    LayerTransform rot = LayerTransform::rect(60, 35, 40, 20);
    rot.rotation = 90.0f;
    CHECK_MSG(rot.contains(80, 60, W, H) && !rot.contains(105, 45, W, H) && !rot.contains(80, 70, W, H), "contem com rotacao");

    CHECK(std::string(transformPresetName(static_cast<int>(TransformPreset::Grid2x2))).find("2×2") != std::string::npos);
    const LayerTransform c0 = gridCellTransform(2, 2, 1, 1, W, H);
    CHECK(c0.x == 80 && c0.y == 45 && c0.w == 80 && c0.h == 45);
    const LayerTransform c3 = gridCellTransform(3, 3, 2, 2, 100, 100);
    CHECK_MSG(c3.x == 67 && c3.w == 33 && c3.y == 67 && c3.h == 33, "grade 3x3 num canvas que nao divide: as celulas fecham o canvas");
}

void testIdentity()
{
    section("identidade igual a antes (pixel a pixel)");
    Compositor compositor(W, H);
    Composition comp = makeComp(1);
    putClip(comp, 0, gradient());

    const Img base = shoot(compositor, comp);
    bool exact = true;
    for (int y = 0; y < H && exact; ++y)
        for (int x = 0; x < W && exact; ++x) {
            const Rgb p = at(base, x, y);
            exact = near(p, {x * 255 / (W - 1), y * 255 / (H - 1), 128}, 1);
        }
    CHECK_MSG(exact, "sem Transform a camada sai igual a fonte, pixel a pixel");

    LayerTransform& t = comp.layers[0].transform;
    t.w = W;
    t.h = H;
    const Img same = shoot(compositor, comp);
    CHECK_MSG(same.px == base.px, "L/A iguais ao canvas: imagem identica byte a byte");
    t.anchorX = 0.1f;
    t.anchorY = 0.9f;
    CHECK_MSG(shoot(compositor, comp).px == base.px, "so a ancora: imagem identica byte a byte");
    t = LayerTransform{};

    const size_t created = compositor.pool().created();
    shoot(compositor, comp);
    CHECK_MSG(compositor.pool().created() == created && compositor.pool().inUse() == 0, "identidade: nenhum FBO extra no pool");

    t = LayerTransform::rect(10, 5, 0, 0);
    const Img moved = shoot(compositor, comp);
    bool ok = true;
    for (int y = 0; y < H && ok; ++y)
        for (int x = 0; x < W && ok; ++x) {
            const Rgb p = at(moved, x, y);
            if (x >= 10 && y >= 5)
                ok = near(p, {(x - 10) * 255 / (W - 1), (y - 5) * 255 / (H - 1), 128}, 1);
            else
                ok = near(p, kBlack, 0);
        }
    CHECK_MSG(ok, "posicao (10, 5): a imagem inteira anda 10 px para a direita e 5 para baixo, o resto fica preto");
    GLenum err = glGetError();
    CHECK_MSG(err == GL_NO_ERROR, "sem erro de GL na identidade");
}

void testGeometryOnScreen()
{
    section("posicao, escala, rotacao, ancora e recorte");
    Compositor compositor(W, H);
    Composition comp = makeComp(1);
    putClip(comp, 0, flat(kRed[0], kRed[1], kRed[2]));
    LayerTransform& t = comp.layers[0].transform;

    t = LayerTransform::rect(40, 20, 80, 40);
    Img img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 80, 40), kRed) && near(at(img, 40, 20), kRed) && near(at(img, 119, 59), kRed),
              "caixa 40,20 80x40: o interior e a cor da camada, inclusive os pixels de canto");
    CHECK_MSG(near(at(img, 39, 40), kBlack) && near(at(img, 120, 40), kBlack) && near(at(img, 80, 19), kBlack)
                  && near(at(img, 80, 60), kBlack),
              "caixa 40,20 80x40: um pixel para fora de cada lado e preto");

    comp.clip(0, 0).source = halves();
    t = LayerTransform::rect(40, 22.5f, 80, 45);
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 60, 45), {255, 0, 0}) && near(at(img, 100, 45), {0, 0, 255}) && near(at(img, 20, 45), kBlack),
              "escala 50%: esquerda vermelha, direita azul, borda preta em volta");

    t = LayerTransform::rect(40, 25, 80, 40);
    t.rotation = 90.0f;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 80, 25), {255, 0, 0}) && near(at(img, 80, 65), {0, 0, 255}),
              "rotacao 90 graus horario: a metade esquerda (vermelha) vai para cima, a direita (azul) para baixo; got "
                  + str(at(img, 80, 25)) + " " + str(at(img, 80, 65)));
    CHECK_MSG(near(at(img, 80 + 35, 45), kBlack) && near(at(img, 80 - 35, 45), kBlack),
              "rotacao 90 graus: fora da caixa girada e preto");
    CHECK_MSG(near(at(img, 80, 8), {255, 0, 0}) && near(at(img, 80, 83), {0, 0, 255}) && near(at(img, 80, 3), kBlack),
              "rotacao 90 graus: a caixa 80x40 vira 40x80 (cobre y de 5 a 85)");

    comp.clip(0, 0).source = flat(kGreen[0], kGreen[1], kGreen[2]);
    t = LayerTransform::rect(100, 60, 40, 20);
    t.anchorX = 0.0f;
    t.anchorY = 0.0f;
    t.rotation = 180.0f;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 80, 50), kGreen) && near(at(img, 62, 42), kGreen) && near(at(img, 120, 70), kBlack),
              "ancora no canto + 180 graus: a caixa gira em volta do canto, nao do centro");

    comp.clip(0, 0).source = gradient();
    t = LayerTransform{};
    t.cropL = 40;
    t.cropT = 10;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 39, 50), kBlack) && near(at(img, 80, 9), kBlack),
              "recorte: a faixa cortada fica preta");
    CHECK_MSG(near(at(img, 40, 10), {40 * 255 / (W - 1), 10 * 255 / (H - 1), 128}, 1)
                  && near(at(img, 120, 60), {120 * 255 / (W - 1), 60 * 255 / (H - 1), 128}, 1),
              "recorte: o resto continua no mesmo lugar e no mesmo tamanho (nao estica)");
    t.cropR = 60;
    t.cropB = 30;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 99, 59), {99 * 255 / (W - 1), 59 * 255 / (H - 1), 128}, 1) && near(at(img, 100, 59), kBlack)
                  && near(at(img, 99, 60), kBlack),
              "recorte nos 4 lados: a janela termina em x=100, y=60");

    t = LayerTransform::rect(0, 0, 80, 45);
    comp.clip(0, 0).source = flat(200, 200, 200);
    comp.layers[0].opacity = 0.5f;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 20), {100, 100, 100}, 2) && near(at(img, 120, 60), kBlack),
              "a opacidade da camada continua valendo com o Transform");
    comp.layers[0].opacity = 1.0f;

    comp.layers[0].effects.add("native:invert");
    comp.clip(0, 0).source = flat(kRed[0], kRed[1], kRed[2]);
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 20), {255 - kRed[0], 255 - kRed[1], 255 - kRed[2]}, 3) && near(at(img, 120, 60), kBlack),
              "efeito da camada (inverter) + Transform: invertido dentro da caixa, preto fora (o efeito nao pinta o resto)");
    comp.layers[0].effects.items.clear();
    GLenum err = glGetError();
    CHECK_MSG(err == GL_NO_ERROR, "sem erro de GL na geometria");
}

void testScaledPreview()
{
    section("previa em escala reduzida (Eco)");
    Compositor compositor(W, H);
    Composition comp = makeComp(1);
    putClip(comp, 0, flat(kBlue[0], kBlue[1], kBlue[2]));
    comp.layers[0].transform = gridCellTransform(2, 2, 1, 0, W, H);
    const Img full = shoot(compositor, comp, 1.0f);
    CHECK(near(at(full, 120, 20), kBlue) && near(at(full, 40, 20), kBlack));
    const Img half = shoot(compositor, comp, 0.5f);
    const int rw = static_cast<int>(W * compositor.uvMax()), rh = static_cast<int>(H * compositor.uvMax());
    auto cell = [&](int x, int yTop) {
        const size_t row = static_cast<size_t>(rh - 1 - yTop);
        const size_t i = (row * static_cast<size_t>(half.w) + static_cast<size_t>(x)) * 4;
        return Rgb{half.px[i], half.px[i + 1], half.px[i + 2]};
    };
    CHECK_MSG(near(cell(rw * 3 / 4, rh / 4), kBlue) && near(cell(rw / 4, rh / 4), kBlack) && near(cell(rw * 3 / 4, rh * 3 / 4), kBlack),
              "escala 50%: o Transform acompanha o canvas (o quadrante continua no quadrante)");
}

void testGridPreset()
{
    section("preset Grade 2x2 com as camadas 1-4");
    Compositor compositor(W, H);
    Composition comp = makeComp(4);
    const std::array<Rgb, 4> colors{kRed, kGreen, kBlue, kYellow};
    for (int i = 0; i < 4; ++i)
        putClip(comp, i, flat(colors[static_cast<size_t>(i)][0], colors[static_cast<size_t>(i)][1], colors[static_cast<size_t>(i)][2]));
    CHECK(near(at(shoot(compositor, comp), 40, 20), kYellow));

    const int changed = comp.applyTransformPreset(0, static_cast<int>(TransformPreset::Grid2x2));
    CHECK_MSG(changed == 4, "a grade 2x2 mexe nas 4 camadas de uma vez");
    const Img img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 20), kRed) && near(at(img, 120, 20), kGreen) && near(at(img, 40, 65), kBlue)
                  && near(at(img, 120, 65), kYellow),
              "camadas 1-4 nos quadrantes (cima-esq, cima-dir, baixo-esq, baixo-dir)");
    CHECK_MSG(near(at(img, 79, 44), kRed) && near(at(img, 80, 44), kGreen) && near(at(img, 79, 45), kBlue) && near(at(img, 80, 45), kYellow),
              "os quadrantes se encontram sem fresta nem sobreposicao no centro");
    CHECK_MSG(comp.applyTransformPreset(0, static_cast<int>(TransformPreset::Grid2x2)) == 0, "repetir o preset nao muda nada");

    CHECK_MSG(comp.layerAtPoint(40, 20) == 0 && comp.layerAtPoint(120, 20) == 1 && comp.layerAtPoint(40, 65) == 2
                  && comp.layerAtPoint(120, 65) == 3,
              "clicar numa area acha a camada que esta ali");
    comp.layers[3].bypass = true;
    CHECK_MSG(comp.layerAtPoint(120, 65) == -1, "camada em bypass nao e achada (area vazia)");
    comp.layers[3].bypass = false;
    comp.layers[3].transform = LayerTransform{};
    CHECK_MSG(comp.layerAtPoint(40, 20) == 3, "uma camada de tela cheia por cima ganha de todas");
    comp.layers[3].opacity = 0.0f;
    CHECK_MSG(comp.layerAtPoint(40, 20) == 0, "opacidade 0 nao conta");
    comp.layers[3].opacity = 1.0f;

    CHECK(comp.applyTransformPreset(2, static_cast<int>(TransformPreset::LeftHalf)) == 1);
    CHECK(comp.layers[2].transform.x == 0 && comp.layers[2].transform.w == 80 && comp.layers[2].transform.h == 0);
    CHECK(comp.applyTransformPreset(2, static_cast<int>(TransformPreset::Quad4)) == 1);
    CHECK(comp.layers[2].transform.x == 80 && comp.layers[2].transform.y == 45);
    CHECK(comp.applyTransformPreset(2, static_cast<int>(TransformPreset::Full)) == 1 && comp.layers[2].transform.isDefault());
    CHECK_MSG(comp.applyTransformPreset(-1, static_cast<int>(TransformPreset::Quad1)) == 0 && comp.applyTransformPreset(2, 99) == 0,
              "camada ou preset invalido: nada acontece");

    Composition fitComp = makeComp(1);
    putClip(fitComp, 0, std::make_unique<PatternSource>(80, 90, [](int, int, unsigned char* p) {
                p[0] = 10; p[1] = 20; p[2] = 30; p[3] = 255; }));
    fitComp.clip(0, 0).fit = FitMode::Stretch;
    CHECK(fitComp.applyTransformPreset(0, static_cast<int>(TransformPreset::Fit)) == 1);
    const LayerTransform& ft = fitComp.layers[0].transform;
    CHECK_MSG(std::abs(ft.w - 80.0f) < 0.01f && std::abs(ft.width(W) - 80.0f) < 0.01f && std::abs(ft.x - 40.0f) < 0.01f && ft.y == 0.0f,
              "Ajustar: a fonte 80x90 estica-se ao proprio aspecto e fica centrada (80x90 em x=40)");
    CHECK(fitComp.applyTransformPreset(0, static_cast<int>(TransformPreset::Fill)) == 1);
    CHECK_MSG(std::abs(fitComp.layers[0].transform.width(W) - 160.0f) < 0.01f || std::abs(fitComp.layers[0].transform.height(H) - 180.0f) < 0.01f,
              "Preencher: a fonte cobre o canvas (160x180, passando da borda)");

    Composition nine = makeComp(9);
    Compositor c9(W, H);
    for (int i = 0; i < 9; ++i)
        putClip(nine, i, flat(20 + i * 25, 255 - i * 25, 40 + i * 10));
    CHECK(nine.applyTransformPreset(0, static_cast<int>(TransformPreset::Grid3x3)) == 9);
    const Img n = shoot(c9, nine);
    bool ok = true;
    for (int i = 0; i < 9; ++i)
        ok = ok && near(at(n, (i % 3) * W / 3 + W / 6, (i / 3) * H / 3 + H / 6), {20 + i * 25, 255 - i * 25, 40 + i * 10});
    CHECK_MSG(ok, "grade 3x3: as camadas 1-9 nas 9 celulas, da esquerda para a direita e de cima para baixo");
}

void testClipTransform()
{
    section("Transform do clip x Transform da camada");
    Compositor compositor(W, H);
    Composition comp = makeComp(1);
    putClip(comp, 0, flat(kRed[0], kRed[1], kRed[2]));
    Layer& layer = comp.layers[0];
    Clip& clip = comp.clip(0, 0);
    layer.transform = gridCellTransform(2, 1, 0, 0, W, H);
    Img img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 45), kRed) && near(at(img, 120, 45), kBlack), "so o Transform da camada: metade esquerda");

    clip.transform = gridCellTransform(2, 1, 1, 0, W, H);
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 45), kRed) && near(at(img, 120, 45), kBlack), "Transform do clip desligado nao vale");

    clip.ownTransform = true;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 45), kBlack) && near(at(img, 120, 45), kRed), "o clip traz o proprio enquadramento: vence o da camada");
    CHECK_MSG(comp.transformRef(0) == &clip.transform, "o editor mexe no Transform do clip enquanto ele esta ligado");
    CHECK_MSG(comp.layerAtPoint(120, 45) == 0 && comp.layerAtPoint(40, 45) == -1, "layerAtPoint segue o Transform do clip");
    CHECK_MSG(comp.resetTransform(0) && clip.transform.isDefault() && !layer.transform.isDefault(),
              "resetar mexe so no Transform em vigor (o do clip); o da camada continua guardado");

    clip.ownTransform = false;
    img = shoot(compositor, comp);
    CHECK_MSG(near(at(img, 40, 45), kRed) && near(at(img, 120, 45), kBlack), "desligando, volta o Transform da camada");
}

void testLinkedSlices(GLFWwindow* window)
{
    section("slice vinculado a camada: sem e com 'Aplicar transform da camada'");
    Compositor compositor(W, H);
    Composition comp = makeComp(2);
    putClip(comp, 0, flat(kBlue[0], kBlue[1], kBlue[2]));
    putClip(comp, 1, flat(kRed[0], kRed[1], kRed[2]));
    comp.layers[1].transform = gridCellTransform(2, 2, 0, 0, W, H);
    const int redId = comp.layers[1].id;

    OutputRenderer renderer(window);
    OutputSetup setup = OutputSetup::createDefault(W, H, {});
    Screen& screen = setup.screens.front();
    screen.kind = OutputKind::Disabled;
    screen.width = W;
    screen.height = H;
    screen.slices.clear();
    Slice s = makeFullSlice("Vermelho", W, H, W, H);
    s.source = kSliceFromLayer;
    s.layerId = redId;
    screen.slices.push_back(s);
    const int screenId = screen.id;
    Slice& slice = screen.slices.front();

    auto out = [&](int x, int yTop) {
        compositor.render(comp, 1.0f, false, collectLayerViews(setup, screenId));
        renderer.render(setup, compositor, screenId);
        std::vector<unsigned char> rgba;
        int sw = 0, sh = 0;
        if (!renderer.readScreen(screenId, rgba, sw, sh)) {
            report(false, "readScreen");
            return kBlack;
        }
        const size_t i = (static_cast<size_t>(yTop) * static_cast<size_t>(sw) + static_cast<size_t>(x)) * 4;
        return Rgb{rgba[i], rgba[i + 1], rgba[i + 2]};
    };

    CHECK_MSG(!slice.applyLayerTransform, "o padrao da opcao e desligado");
    CHECK_MSG(near(out(120, 70), kRed) && near(out(20, 10), kRed),
              "slice vinculado, sem aplicar: recebe a camada sem o Transform (cobre o slice inteiro)");
    const Img canvasImg = shoot(compositor, comp, 1.0f, collectLayerViews(setup, screenId));
    CHECK_MSG(near(at(canvasImg, 120, 70), kBlue) && near(at(canvasImg, 20, 10), kRed),
              "enquanto isso, o canvas principal usa o Transform da camada (a azul aparece fora do quadrante)");
    slice.applyLayerTransform = true;
    CHECK_MSG(collectLayerViews(setup, screenId).size() == 1 && collectLayerViews(setup, screenId)[0].applyTransform,
              "o pedido de vista leva a opcao");
    CHECK_MSG(near(out(20, 10), kRed) && near(out(120, 70), kBlack),
              "slice vinculado, aplicando: so o quadrante da camada (resto preto)");
    Slice other = slice;
    other.applyLayerTransform = false;
    screen.slices.push_back(other);
    compositor.render(comp, 1.0f, false, collectLayerViews(setup, screenId));
    CHECK_MSG(compositor.layerViewCount() == 2, "mesma camada com e sem Transform: duas vistas distintas");
    GLuint t0 = 0, t1 = 0;
    CHECK(compositor.layerView(redId, false, t0, true) && compositor.layerView(redId, false, t1, false) && t0 != t1);
    screen.slices.pop_back();
}

void testPreviewView()
{
    section("Previa em 'Camada selecionada': vista com os efeitos da camada e o Transform");
    Compositor compositor(W, H);
    Composition comp = makeComp(2);
    putClip(comp, 0, flat(kBlue[0], kBlue[1], kBlue[2]));
    putClip(comp, 1, flat(kRed[0], kRed[1], kRed[2]));
    comp.layers[1].effects.add("native:invert");
    comp.layers[1].transform = gridCellTransform(2, 2, 1, 1, W, H);
    const int id = comp.layers[1].id;
    const std::vector<LayerViewRequest> req{{id, false, true}};
    compositor.render(comp, 1.0f, false, req);
    GLuint tex = 0;
    CHECK_MSG(compositor.layerView(id, false, tex, true) && tex != 0, "a Previa pede a vista da camada selecionada (com Transform)");
    const Img view = readTexture(tex, W, H);
    CHECK_MSG(near(at(view, 120, 70), {255 - kRed[0], 255 - kRed[1], 255 - kRed[2]}, 3),
              "a Previa aplica os efeitos da camada (inverter): antes ela mostrava a camada sem eles");
    CHECK_MSG(near(at(view, 40, 20), kBlack) && near(at(view, 120, 20), kBlack),
              "a camada sozinha sobre preto: sem a camada azul de baixo");
    CHECK_MSG(near(at(readTexture(compositor.texture(), W, H), 40, 20), kBlue) && compositor.layerViewCount() == 1,
              "o canvas principal continua com a composicao inteira");
    comp.layers[1].opacity = 0.5f;
    compositor.render(comp, 1.0f, false, req);
    GLuint t2 = 0;
    compositor.layerView(id, false, t2, true);
    const Rgb half = at(readTexture(t2, W, H), 120, 70);
    CHECK_MSG(near(half, {(255 - kRed[0]) / 2, (255 - kRed[1]) / 2, (255 - kRed[2]) / 2}, 3), "opacidade da camada na Previa");
    comp.layers[1].opacity = 1.0f;
    compositor.render(comp, 1.0f, false, {});
    CHECK_MSG(compositor.layerViewCount() == 0 && compositor.pool().inUse() == 0, "Previa fora do modo Camada: nenhuma vista, nenhum FBO preso");
}

void testHistory()
{
    section("desfazer / refazer do Transform");
    Composition comp = makeComp(4);
    TransformHistory h;
    h.reset(comp);
    CHECK(!h.canUndo() && !h.canRedo());

    comp.layers[0].transform = LayerTransform::rect(10, 10, 50, 50);
    CHECK_MSG(!h.observe(comp, true) && h.undoCount() == 0, "gesto em andamento: a mudanca fica pendente");
    comp.layers[0].transform = LayerTransform::rect(20, 20, 50, 50);
    CHECK_MSG(h.observe(comp, false) && h.undoCount() == 1, "gesto terminou: tudo vira UMA operacao");
    CHECK(!h.observe(comp, false));

    comp.addLayer();
    CHECK_MSG(!h.observe(comp, false) && h.undoCount() == 1, "criar uma camada nao entra no historico do Transform");

    comp.applyTransformPreset(0, static_cast<int>(TransformPreset::Grid2x2));
    CHECK_MSG(h.observe(comp, false) && h.undoCount() == 2, "o preset de grade (4 camadas) e uma operacao so");
    Clip& clip = comp.clip(1, 0);
    clip.ownTransform = true;
    clip.transform = LayerTransform::rect(5, 5, 20, 20);
    CHECK_MSG(h.observe(comp, false) && h.undoCount() == 3, "o Transform do clip tambem entra");

    CHECK(h.undo(comp));
    CHECK_MSG(!clip.ownTransform && clip.transform.isDefault(), "desfazer: o clip volta ao padrao");
    CHECK(comp.layers[0].transform == gridCellTransform(2, 2, 0, 0, W, H));
    CHECK(h.undo(comp));
    CHECK_MSG(comp.layers[0].transform == LayerTransform::rect(20, 20, 50, 50) && comp.layers[1].transform.isDefault(),
              "desfazer a grade: as 4 camadas voltam de uma vez");
    CHECK(h.undo(comp));
    CHECK_MSG(comp.layers[0].transform == LayerTransform::rect(10, 10, 50, 50) || comp.layers[0].transform.isDefault(),
              "desfazer a ultima operacao: a camada volta");
    CHECK_MSG(comp.layerCount() == 5, "desfazer o Transform nao desfaz a criacao da camada");
    CHECK(h.redo(comp) && h.redo(comp) && h.redo(comp));
    CHECK_MSG(clip.ownTransform && clip.transform == LayerTransform::rect(5, 5, 20, 20), "refazer tudo: o clip volta");
    CHECK_MSG(!h.redo(comp), "nada mais a refazer");
    comp.layers[3].transform = LayerTransform::rect(1, 2, 3, 4);
    CHECK_MSG(h.observe(comp, false) && !h.canRedo(), "uma operacao nova limpa o refazer");

    comp.layers[3].transform = LayerTransform::rect(9, 9, 9, 9);
    CHECK(h.undo(comp));
    CHECK_MSG(comp.layers[3].transform == LayerTransform::rect(1, 2, 3, 4), "desfazer com mudanca pendente: desfaz so a pendente");

    TransformHistory small(2);
    small.reset(comp);
    for (int i = 0; i < 5; ++i) {
        comp.layers[0].transform = LayerTransform::rect(static_cast<float>(i), 0, 10, 10);
        small.observe(comp, false);
    }
    CHECK_MSG(small.undoCount() == 2, "o historico respeita o limite");
}

}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "transformtest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    testGeometry();
    testIdentity();
    testGeometryOnScreen();
    testScaledPreview();
    testGridPreset();
    testClipTransform();
    testLinkedSlices(win);
    testPreviewView();
    testHistory();

    GLenum err;
    int glErrors = 0;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::printf("  erro de GL pendente: 0x%04X\n", err);
        ++glErrors;
    }
    report(glErrors == 0, "nenhum erro de GL");
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "TRANSFORMTEST OK\n" : "TRANSFORMTEST FALHOU\n");
    const int rc = g_failures == 0 ? 0 : 1;
    glfwDestroyWindow(win);
    glfwTerminate();
    return rc;
}
