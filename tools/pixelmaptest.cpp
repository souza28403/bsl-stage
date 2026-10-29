// bslstage_pixelmaptest: o modo Pixel Map (BSL Light Pixel Mapping Pro dentro do BSL Stage).
// Uso: bslstage_pixelmaptest                   testes automaticos
//      bslstage_pixelmaptest --docs <pasta>    grava o padrao HD de exemplo (docs/pixelmap-padrao-exemplo.png)
//      bslstage_pixelmaptest --dump-live <png> grava o quadro animado (barra e contador) da fonte ao vivo
//
// Cobre: a conta de escala (igual a do HTML original, com Math.round), "Criar mapeamento" (slices certos, substituir e
// adicionar), ida e volta com "Importar do mapeamento", CSV de ida e volta (aspas, acentos, ';' e ','), o PNG do padrao com
// o tamanho fisico exato e pixels esperados (borda vermelha, xadrez), o overview, as validacoes (sobreposicao, fora da
// area, escala nao inteira) e a fonte ao vivo "Padrao de calibracao BSL" (janela GL oculta).

#include "core/Generators.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/PixelMapFeed.h"
#include "output/OutputSetup.h"
#include "output/PatternRaster.h"
#include "output/PixelMap.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
namespace pm = bsl::pixelmap;

namespace {

int g_failures = 0;
int g_checks = 0;
fs::path g_dumpLive; // --dump-live <arquivo.png>: grava o quadro animado da fonte ao vivo

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);                     \
        }                                                                                  \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

// Config do HTML: input 1920x1080, fisico 2048x640 e os tres paineis do exemplo
pm::Config sampleConfig()
{
    pm::Config c;
    c.inW = 1920;
    c.inH = 1080;
    c.physW = 2048;
    c.physH = 640;
    c.panels.push_back(pm::makePanel("Testeira Palco", 0, 0, 2048, 128));
    c.panels.push_back(pm::makePanel("Lateral Esquerda", 0, 128, 512, 512));
    c.panels.push_back(pm::makePanel("Lateral Direita", 1536, 128, 512, 512));
    return c;
}

bool near(int a, int b, int tol)
{
    return std::abs(a - b) <= tol;
}

bool pixelIs(const pm::Image& img, int x, int y, int r, int g, int b, int tol = 0)
{
    if (x < 0 || y < 0 || x >= img.w || y >= img.h)
        return false;
    const std::uint8_t* p = img.px(x, y);
    return near(p[0], r, tol) && near(p[1], g, tol) && near(p[2], b, tol);
}

// ------------------------------------------------------------------------------------------------ escala

void testScale()
{
    section("escala: igual a conta do HTML (Math.round)");
    // Valores gerados pelo proprio HTML (atualizarTabela, node): input 1920x1080, fisico 2048x640
    //   Testeira Palco   -> X 0     Y 0    1920 x 216
    //   Lateral Esquerda -> X 0     Y 216  480 x 864
    //   Lateral Direita  -> X 1440  Y 216  480 x 864
    //   Meio (100x50 em 8,3) -> X 8 Y 5   94 x 84   (7,5 arredonda para cima; 93,75 e 84,375)
    pm::Config c = sampleConfig();
    const pm::Scale s = pm::scaleOf(c);
    CHECK(std::abs(s.x - 0.9375) < 1e-12);
    CHECK(std::abs(s.y - 1.6875) < 1e-12);

    struct Expect { int x, y, w, h; };
    const Expect want[3] = {{0, 0, 1920, 216}, {0, 216, 480, 864}, {1440, 216, 480, 864}};
    for (size_t i = 0; i < 3; ++i) {
        const RectI r = pm::scaledRectRounded(c, c.panels[i]);
        CHECK(r.x == want[i].x && r.y == want[i].y && r.w == want[i].w && r.h == want[i].h);
    }
    const RectI meio = pm::scaledRectRounded(c, pm::makePanel("Meio", 8, 3, 100, 50));
    CHECK(meio.x == 8 && meio.y == 5 && meio.w == 94 && meio.h == 84);
    const pm::RectD raw = pm::scaledRect(c, c.panels[1]);
    CHECK(raw.x == 0.0 && raw.y == 216.0 && raw.w == 480.0 && raw.h == 864.0);

    // Math.round do JavaScript: meio para cima, tambem nos negativos
    CHECK(pm::jsRound(2.5) == 3.0 && pm::jsRound(-2.5) == -2.0 && pm::jsRound(0.5) == 1.0 && pm::jsRound(-0.4) == -0.0);
    CHECK(pm::jsRound(7.5) == 8.0 && pm::jsRound(93.75) == 94.0 && pm::jsRound(84.375) == 84.0);

    // Input ligado ao canvas
    pm::Config lk;
    lk.linkCanvas = true;
    pm::syncInput(lk, 1280, 720);
    CHECK(lk.inW == 1280 && lk.inH == 720);
    lk.linkCanvas = false;
    pm::syncInput(lk, 3840, 2160);
    CHECK(lk.inW == 1280 && lk.inH == 720); // livre: nao muda

    // Gabinetes: 16 x 1 de 128 px = 2048 x 128
    pm::Panel g = pm::makePanel("Gab", 0, 0, 1, 1);
    g.byCabinets = true;
    g.cols = 16;
    g.rows = 1;
    g.cabW = 128;
    g.cabH = 128;
    pm::applyCabinets(g);
    CHECK(g.w == 2048 && g.h == 128);
    g.rows = 5;
    pm::applyCabinets(g);
    CHECK(g.h == 640);
}

// ------------------------------------------------------------------------------------------------ mapeamento

void testMapping()
{
    section("criar mapeamento e importar do mapeamento");
    const pm::Config c = sampleConfig();
    OutputSetup out;
    out.addScreen(1920, 1080); // "Tela 1" ja existente

    // Substituir a tela existente
    const int idx = pm::createMapping(c, out, pm::MapMode::Replace, 0, 1920, 1080);
    CHECK(idx == 0 && out.screens.size() == 1);
    const Screen& s = out.screens[0];
    CHECK(s.width == 2048 && s.height == 640);
    CHECK(s.slices.size() == 3);
    const int want[3][4] = {{0, 0, 1920, 216}, {0, 216, 480, 864}, {1440, 216, 480, 864}};
    const float phys[3][4] = {{0, 0, 2048, 128}, {0, 128, 512, 512}, {1536, 128, 512, 512}};
    for (size_t i = 0; i < 3 && i < s.slices.size(); ++i) {
        const Slice& sl = s.slices[i];
        CHECK(sl.name == c.panels[i].name);
        CHECK(sl.input.x == want[i][0] && sl.input.y == want[i][1] && sl.input.w == want[i][2] && sl.input.h == want[i][3]);
        // saida = posicao fisica, cantos TL TR BR BL
        CHECK(sl.corners[kCornerTL] == (Point2{phys[i][0], phys[i][1]}));
        CHECK(sl.corners[kCornerTR] == (Point2{phys[i][0] + phys[i][2], phys[i][1]}));
        CHECK(sl.corners[kCornerBR] == (Point2{phys[i][0] + phys[i][2], phys[i][1] + phys[i][3]}));
        CHECK(sl.corners[kCornerBL] == (Point2{phys[i][0], phys[i][1] + phys[i][3]}));
    }

    // Adicionar: a tela nova nasce com nome proprio, a antiga fica intacta
    const OutputSetup before = out;
    const int idx2 = pm::createMapping(c, out, pm::MapMode::Add, -1, 1920, 1080);
    CHECK(idx2 == 1 && out.screens.size() == 2);
    CHECK(out.screens[0] == before.screens[0]);
    CHECK(out.screens[1].name == "Pixel Map" && out.screens[1].slices.size() == 3);
    CHECK(out.screens[1].id != out.screens[0].id);
    const int idx3 = pm::createMapping(c, out, pm::MapMode::Add, -1, 1920, 1080);
    CHECK(out.screens[static_cast<size_t>(idx3)].name == "Pixel Map 2");

    // Replace sem tela valida cria uma
    OutputSetup empty;
    CHECK(pm::createMapping(c, empty, pm::MapMode::Replace, 5, 1920, 1080) == 0 && empty.screens.size() == 1);

    // Canvas diferente do input do Pixel Map: a entrada vale para o canvas REAL
    OutputSetup small;
    pm::createMapping(c, small, pm::MapMode::Add, -1, 960, 540);
    CHECK(small.screens[0].slices[0].input.w == 960 && small.screens[0].slices[0].input.h == 108);

    // Ida e volta: importar do mapeamento devolve os mesmos paineis
    pm::Config back;
    back.inW = 1920;
    back.inH = 1080;
    CHECK(pm::importFromScreen(back, out.screens[0]));
    CHECK(back.physW == 2048 && back.physH == 640 && back.panels.size() == 3);
    for (size_t i = 0; i < 3 && i < back.panels.size(); ++i) {
        const pm::Panel& a = c.panels[i];
        const pm::Panel& b = back.panels[i];
        CHECK(a.name == b.name && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
    }
    // ... e de novo o mapeamento daria o mesmo resultado
    OutputSetup again;
    pm::createMapping(back, again, pm::MapMode::Add, -1, 1920, 1080);
    CHECK(again.screens[0].slices == out.screens[0].slices);

    // Quad deformado/girado: importa a caixa que envolve os cantos
    Screen warped;
    warped.width = 1000;
    warped.height = 500;
    Slice w = makeFullSlice("Torto", 1920, 1080, 1000, 500);
    w.corners[kCornerTL] = {10.0f, 20.0f};
    w.corners[kCornerTR] = {210.0f, 5.0f};
    w.corners[kCornerBR] = {190.0f, 120.0f};
    w.corners[kCornerBL] = {0.0f, 100.0f};
    warped.slices.push_back(w);
    pm::Config wc;
    CHECK(pm::importFromScreen(wc, warped));
    CHECK(wc.panels[0].x == 0 && wc.panels[0].y == 5 && wc.panels[0].w == 210 && wc.panels[0].h == 115);
    Screen none;
    CHECK(!pm::importFromScreen(wc, none));
}

// ------------------------------------------------------------------------------------------------ CSV

void testCsv()
{
    section("CSV de ida e volta");
    pm::Config c = sampleConfig();
    c.panels.push_back(pm::makePanel("Backstage; \"Sul\", ç", -16, 40, 300, 70)); // ';' aspas ',' e acento no nome
    c.panels.push_back(pm::makePanel(" Com espaços ", 5, 6, 7, 8));
    const std::string csv = pm::toCsv(c);
    CHECK(csv.rfind("\xEF\xBB\xBF" "nome;fisico_x;fisico_y;fisico_w;fisico_h;comp_x;comp_y;comp_w;comp_h", 0) == 0);
    CHECK(csv.find("Testeira Palco;0;0;2048;128;0;0;1920;216") != std::string::npos);
    CHECK(csv.find("Lateral Direita;1536;128;512;512;1440;216;480;864") != std::string::npos);

    std::vector<pm::Panel> back;
    std::string err;
    CHECK(pm::fromCsv(csv, back, &err));
    CHECK(back.size() == c.panels.size());
    for (size_t i = 0; i < back.size() && i < c.panels.size(); ++i) {
        CHECK(back[i].name == (i == 4 ? std::string("Com espaços") : c.panels[i].name)); // o nome sai aparado
        CHECK(back[i].x == c.panels[i].x && back[i].y == c.panels[i].y && back[i].w == c.panels[i].w && back[i].h == c.panels[i].h);
    }

    // Outros formatos: virgula, tab, sem cabecalho, virgula decimal, CRLF e linhas em branco
    std::vector<pm::Panel> p2;
    CHECK(pm::fromCsv("name,x,y,w,h\r\nA,1,2,30,40\r\n\r\nB,5,6,70.4,80\r\n", p2, &err) && p2.size() == 2);
    CHECK(p2.size() == 2 && p2[0].name == "A" && p2[0].w == 30 && p2[1].w == 70 && p2[1].h == 80);
    CHECK(pm::fromCsv("A\t1\t2\t30\t40\n", p2, &err) && p2.size() == 1 && p2[0].x == 1);
    CHECK(pm::fromCsv("A;1;2;30,5;40\n", p2, &err) && p2.size() == 1 && p2[0].w == 31); // 30,5 -> Math.round -> 31
    CHECK(pm::fromCsv("\xEF\xBB\xBFnome;a;b;c;d\nA;1;2;3;4", p2, &err) && p2.size() == 1);

    // Erros
    const std::vector<pm::Panel> keep = p2;
    CHECK(!pm::fromCsv("", p2, &err) && !err.empty());
    CHECK(!pm::fromCsv("nome;x;y;w;h\n", p2, &err));
    CHECK(pm::fromCsv("A;1;2;abc;4\nB;1;2;3;4\n", p2, &err) && p2.size() == 1 && p2[0].name == "B"); // 1a linha = cabecalho
    CHECK(pm::fromCsv("cab;a;b;c;d\nA;1;2;3;4\n", p2, &err) && p2.size() == 1);
    CHECK(!pm::fromCsv("A;1;2;3;4\nB;x;2;3;4\n", p2, &err));        // numero invalido depois da 1a linha
    CHECK(!pm::fromCsv("A;1;2;0;4\n", p2, &err));                   // largura < 1
    CHECK(!pm::fromCsv("A;1;2\n", p2, &err));                       // poucas colunas
    CHECK(!pm::fromCsv("A;1;2;99999;4\n", p2, &err));               // acima do limite
    (void)keep;

    CHECK(pm::patternFileName(pm::makePanel("Testeira Palco", 0, 0, 2048, 128)) == "BSLLight_Pattern_Testeira Palco_2048x128.png");
    CHECK(pm::patternFileName(pm::makePanel("A/B:C*", 0, 0, 10, 20)) == "BSLLight_Pattern_A_B_C__10x20.png");
    CHECK(pm::upperUtf8("lateral Esquerda \xC3\xA7\xC3\xA3o") == "LATERAL ESQUERDA \xC3\x87\xC3\x83O");
}

// ------------------------------------------------------------------------------------------------ validacao

void testValidation()
{
    section("validacao: sobreposicao, fora da area, escala nao inteira");
    pm::Config c = sampleConfig();
    pm::Validation v = pm::validate(c);
    CHECK(v.overlapCount == 0 && v.outsideCount == 0); // encostar (y = 128) nao e sobrepor
    CHECK(v.scaleWarn); // 0,9375 x 1,6875

    c.panels.push_back(pm::makePanel("Em cima", 100, 100, 300, 300)); // pega a Testeira e a Lateral Esquerda
    v = pm::validate(c);
    CHECK(v.overlap.size() == 4 && v.overlap[0] && v.overlap[1] && !v.overlap[2] && v.overlap[3]);
    CHECK(v.overlapCount == 3 && v.outsideCount == 0);

    c.panels.push_back(pm::makePanel("Fora", 2000, 600, 100, 100));
    c.panels.push_back(pm::makePanel("Negativo", -1, 0, 10, 10));
    v = pm::validate(c);
    CHECK(v.outside[4] && v.outside[5] && !v.outside[0] && v.outsideCount == 2);

    CHECK(pm::isCleanScale(1.0) && pm::isCleanScale(2.0) && pm::isCleanScale(3.0) && pm::isCleanScale(0.5) && pm::isCleanScale(0.25));
    CHECK(!pm::isCleanScale(0.9375) && !pm::isCleanScale(1.6875) && !pm::isCleanScale(1.5) && !pm::isCleanScale(0.0));
    pm::Config one;
    one.inW = one.physW = 2048;
    one.inH = one.physH = 640;
    CHECK(!pm::validate(one).scaleWarn);
    one.inW = 4096;
    one.inH = 1280; // 2x
    CHECK(!pm::validate(one).scaleWarn);
    one.inW = 3000;
    CHECK(pm::validate(one).scaleWarn);

    // Limites: normalize corrige o que veio do arquivo
    pm::Config bad;
    bad.inW = -5;
    bad.physH = 999999;
    bad.panels.push_back(pm::makePanel("", 0, 0, 0, -3));
    pm::normalize(bad);
    CHECK(bad.inW == 1 && bad.physH == pm::kMaxDim && bad.panels[0].w == 1 && bad.panels[0].h == 1 && !bad.panels[0].name.empty());
}

// ------------------------------------------------------------------------------------------------ PNG do padrao

void testPattern(const fs::path& tmp, const fs::path& docs)
{
    section("padrao HD: PNG com o tamanho fisico exato e pixels esperados");
    std::printf("  fontes: %s\n", pm::fontsAvailable() ? "ok" : "AUSENTES");
    CHECK(pm::fontsAvailable());
    pm::Image logo;
    CHECK(pm::loadBrandMonogram(logo) && logo.w > 0 && logo.h > 0);

    const pm::Panel testeira = pm::makePanel("Testeira Palco", 0, 0, 2048, 128);
    const pm::Image img = pm::renderPattern(testeira, &logo);
    CHECK(img.w == 2048 && img.h == 128 && img.rgba.size() == 2048u * 128u * 4u);

    // Borda vermelha BSL (#FF0A33) nas quatro bordas
    CHECK(pixelIs(img, 0, 64, 255, 10, 51));
    CHECK(pixelIs(img, 2047, 64, 255, 10, 51));
    CHECK(pixelIs(img, 700, 0, 255, 10, 51));
    CHECK(pixelIs(img, 700, 127, 255, 10, 51));
    CHECK(!pixelIs(img, 1, 64, 255, 10, 51, 30)); // so 1 px de borda visivel (o resto do traco cai fora da imagem)

    // Xadrez 20x20 do meio da esquerda: (20, cy-10) = (20, 54); casa (i + j) par = branco
    CHECK(pixelIs(img, 20, 54, 255, 255, 255));
    CHECK(pixelIs(img, 21, 54, 0, 0, 0));
    CHECK(pixelIs(img, 20, 55, 0, 0, 0));
    CHECK(pixelIs(img, 21, 55, 255, 255, 255));
    CHECK(pixelIs(img, 39, 73, 255, 255, 255)); // i = 19, j = 19
    // Xadrez do topo (cx - 10, 20) = (1014, 20) e da direita (w - 40, 54) = (2008, 54). O de baixo fica sob as barras RGBWK
    // (o HTML as desenha depois), entao nao entra na conferencia.
    CHECK(pixelIs(img, 1014, 20, 255, 255, 255) && pixelIs(img, 1015, 20, 0, 0, 0));
    CHECK(pixelIs(img, 2008, 54, 255, 255, 255) && pixelIs(img, 2009, 54, 0, 0, 0));

    // Barra vermelha da caixa central: tira de 5 px no topo da caixa (boxY = 64 - 32 = 32) em #FF0A33
    CHECK(pixelIs(img, 1024, 34, 255, 10, 51));
    // Fundo escuro perto do canto (degrade radial)
    {
        const std::uint8_t* p = img.px(305, 103); // fora das linhas da grade
        CHECK(p[0] < 40 && p[1] < 10 && p[2] < 10);
    }

    // PNG: grava, le de volta e compara tamanho e pixels
    fs::create_directories(tmp);
    const fs::path file = tmp / pm::patternFileName(testeira);
    std::string err;
    CHECK(pm::writePng(file, img, &err));
    pm::Image rd;
    CHECK(pm::loadImageFile(file, rd, &err));
    CHECK(rd.w == 2048 && rd.h == 128 && rd.rgba == img.rgba);

    // Painel quadrado com barras RGBWK (h > 120 e w > 300): vermelho, verde, azul, branco, preto
    const pm::Panel lat = pm::makePanel("Lateral Esquerda", 0, 128, 512, 512);
    const pm::Image sq = pm::renderPattern(lat, &logo);
    CHECK(sq.w == 512 && sq.h == 512);
    const double barW = 512 * 0.8, barH = std::min(512 * 0.12, 60.0);
    const double startX = 256 - barW / 2.0, startY = 512 - barH - 30;
    const double stepW = barW / 5.0;
    const int yy = static_cast<int>(startY + barH / 2.0);
    const int cols[5][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}};
    for (int i = 0; i < 5; ++i)
        CHECK(pixelIs(sq, static_cast<int>(startX + (i + 0.5) * stepW), yy, cols[i][0], cols[i][1], cols[i][2], 3));
    // Painel pequeno (sem barras) e sem logo ainda desenha
    const pm::Image tiny = pm::renderPattern(pm::makePanel("P", 3, 4, 64, 32), nullptr);
    CHECK(tiny.w == 64 && tiny.h == 32);
    CHECK(pm::renderPattern(pm::makePanel("X", 0, 0, 0, 0), nullptr).empty());

    // Sem logo o mesmo painel difere (o logo entra na caixa central)
    const pm::Image nologo = pm::renderPattern(lat, nullptr);
    CHECK(nologo.w == 512 && nologo.rgba != sq.rgba);

    if (!docs.empty()) {
        fs::create_directories(docs);
        CHECK(pm::writePng(docs / "pixelmap-padrao-exemplo.png", sq, &err));
        std::printf("  gravado %s\n", (docs / "pixelmap-padrao-exemplo.png").string().c_str());
    }
}

void testOverview(const fs::path& tmp)
{
    section("overview: tamanho do input, grade, paineis coloridos");
    pm::Config c = sampleConfig();
    pm::Image wm;
    pm::loadBrandMonogram(wm);
    const pm::Image ov = pm::renderOverview(c, &wm);
    CHECK(ov.w == 1920 && ov.h == 1080);
    // Fundo #050505 fora dos paineis, fora das linhas da grade (x = 1000 nao cai em multiplo de 20 e y = 1070 tampouco)
    CHECK(pixelIs(ov, 1007, 1073, 5, 5, 5, 2));
    // Dentro da Testeira (vermelho a 20%): 5*0,8 + 255*0,2 = 55; 5*0,8 + 10*0,2 = 6; 5*0,8 + 51*0,2 = 14
    CHECK(pixelIs(ov, 7, 107, 55, 6, 14, 3));
    // Lateral Esquerda e Direita: ciano (0, 229, 255) a 20% sobre #050505 e branco a 10%
    CHECK(pixelIs(ov, 7, 507, 4, 50, 55, 3));
    CHECK(pixelIs(ov, 1447, 507, 30, 30, 30, 3));
    // Contorno de 2 px na cor do painel (o da Lateral Esquerda cobre o da Testeira no encontro)
    CHECK(pixelIs(ov, 241, 216, 0, 229, 255, 3));
    // Sem marca o overview muda so no centro
    const pm::Image nowm = pm::renderOverview(c, nullptr);
    CHECK(nowm.rgba != ov.rgba);
    // Sobreposicao e fora da area nao mudam o PNG do HTML (so a interface os marca): tamanho continua o do input
    fs::create_directories(tmp);
    std::string err;
    CHECK(pm::writePng(tmp / "overview.png", ov, &err));
}

// ------------------------------------------------------------------------------------------------ fonte ao vivo

std::vector<std::uint8_t> readTexture(GLuint tex, int w, int h)
{
    std::vector<std::uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    return px;
}

void testLiveSource()
{
    section("fonte ao vivo: Padrao de calibracao BSL");
    namespace bl = bsl::fx::bsllight;
    const GeneratorInfo* info = findGenerator(bl::kPixelMapPatternId);
    CHECK(info != nullptr);
    if (!info)
        return;
    CHECK(std::string(info->name) == "Padrão de calibração BSL");
    CHECK(!info->describe({}).empty());

    const pm::Config c = sampleConfig();
    bl::PixelMapFeed::instance().set(c);
    auto src = info->create(1920, 1080);
    media::ParamValues values;
    values.fillDefaults(src->paramDescs());
    values.set("smooth", media::Json(false)); // pixel mais proximo: a borda vermelha sai exata
    src->setParams(values);
    src->setCanvasSize(1920, 1080);
    CHECK(src->open({}));
    src->update(1.0 / 60.0);
    CHECK(src->width() == 1920 && src->height() == 1080 && src->texture() != 0);

    auto px = readTexture(src->texture(), 1920, 1080);
    auto at = [&](int x, int y) { return px.data() + (static_cast<size_t>(y) * 1920u + static_cast<size_t>(x)) * 4; };
    // Borda esquerda da Testeira (retangulo 0,0 1920x216): vermelho BSL
    CHECK(near(at(0, 100)[0], 255, 1) && near(at(0, 100)[1], 10, 1) && near(at(0, 100)[2], 51, 1));
    // Entre as laterais (x = 960, y = 700): nada, preto
    CHECK(at(960, 700)[0] == 0 && at(960, 700)[1] == 0 && at(960, 700)[2] == 0 && at(960, 700)[3] == 255);
    // Dentro da Lateral Esquerda (0,216 480x864): a borda vermelha esquerda
    CHECK(near(at(0, 600)[0], 255, 1) && near(at(0, 600)[1], 10, 1));
    // A Lateral Direita comeca em x = 1440
    CHECK(near(at(1919, 600)[0], 255, 1) && near(at(1440, 600)[0], 255, 1));

    // Animacao: a barra que corre e o contador mudam o quadro; sem elas o quadro e estatico
    const auto still1 = readTexture(src->texture(), 1920, 1080);
    src->update(0.5);
    const auto still2 = readTexture(src->texture(), 1920, 1080);
    CHECK(still1 == still2);
    values.set("bar", media::Json(true));
    values.set("counter", media::Json(true));
    src->setParams(values);
    src->update(0.1);
    const auto a1 = readTexture(src->texture(), 1920, 1080);
    src->update(0.5);
    const auto a2 = readTexture(src->texture(), 1920, 1080);
    CHECK(a1 != a2);
    CHECK(a1 != still2);
    if (!g_dumpLive.empty()) {
        pm::Image dump(1920, 1080);
        dump.rgba = a2;
        std::string derr;
        CHECK(pm::writePng(g_dumpLive, dump, &derr));
        std::printf("  gravado %s\n", g_dumpLive.string().c_str());
    }

    // Mudar a configuracao refaz o desenho (um painel a mais)
    pm::Config c2 = c;
    c2.panels.push_back(pm::makePanel("Centro", 600, 200, 400, 200));
    bl::PixelMapFeed::instance().set(c2);
    values.set("bar", media::Json(false));
    values.set("counter", media::Json(false));
    src->setParams(values);
    src->update(0.016);
    const auto c2px = readTexture(src->texture(), 1920, 1080);
    CHECK(c2px != still2);

    // Canvas redimensionado: a textura acompanha
    src->setCanvasSize(960, 540);
    src->update(0.016);
    CHECK(src->width() == 960 && src->height() == 540);
    src->close();
    CHECK(!src->isOpen());
}

} // namespace

int main(int argc, char** argv)
{
    fs::path docs;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--docs" && i + 1 < argc)
            docs = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));
        else if (std::string(argv[i]) == "--dump-live" && i + 1 < argc)
            g_dumpLive = fs::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1])));

    const fs::path tmp = fs::temp_directory_path() / "bslstage_pixelmaptest";
    std::error_code ec;
    fs::remove_all(tmp, ec);

    testScale();
    testMapping();
    testCsv();
    testValidation();
    testPattern(tmp, docs);
    testOverview(tmp);

    if (glfwInit()) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* win = glfwCreateWindow(64, 64, "pixelmaptest", nullptr, nullptr);
        if (win) {
            glfwMakeContextCurrent(win);
            if (gladLoadGL(glfwGetProcAddress))
                testLiveSource();
            else
                std::printf("[fonte ao vivo] sem GL: pulado\n");
            glfwDestroyWindow(win);
        } else {
            std::printf("[fonte ao vivo] sem contexto GL 3.3: pulado\n");
        }
        glfwTerminate();
    }

    fs::remove_all(tmp, ec);
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
