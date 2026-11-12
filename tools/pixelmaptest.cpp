#include "core/Generators.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/PixelMapFeed.h"
#include "output/OutputSetup.h"
#include "output/PatternRaster.h"
#include "output/NovaScr.h"
#include "output/PixelMap.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
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
fs::path g_dumpLive;

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

void testScale()
{
    section("escala: igual a conta do HTML (Math.round)");
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

    CHECK(pm::jsRound(2.5) == 3.0 && pm::jsRound(-2.5) == -2.0 && pm::jsRound(0.5) == 1.0 && pm::jsRound(-0.4) == -0.0);
    CHECK(pm::jsRound(7.5) == 8.0 && pm::jsRound(93.75) == 94.0 && pm::jsRound(84.375) == 84.0);

    pm::Config lk;
    lk.linkCanvas = true;
    pm::syncInput(lk, 1280, 720);
    CHECK(lk.inW == 1280 && lk.inH == 720);
    lk.linkCanvas = false;
    pm::syncInput(lk, 3840, 2160);
    CHECK(lk.inW == 1280 && lk.inH == 720);

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

void testMapping()
{
    section("criar mapeamento e importar do mapeamento");
    const pm::Config c = sampleConfig();
    OutputSetup out;
    out.addScreen(1920, 1080);

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
        CHECK(sl.corners[kCornerTL] == (Point2{phys[i][0], phys[i][1]}));
        CHECK(sl.corners[kCornerTR] == (Point2{phys[i][0] + phys[i][2], phys[i][1]}));
        CHECK(sl.corners[kCornerBR] == (Point2{phys[i][0] + phys[i][2], phys[i][1] + phys[i][3]}));
        CHECK(sl.corners[kCornerBL] == (Point2{phys[i][0], phys[i][1] + phys[i][3]}));
    }

    const OutputSetup before = out;
    const int idx2 = pm::createMapping(c, out, pm::MapMode::Add, -1, 1920, 1080);
    CHECK(idx2 == 1 && out.screens.size() == 2);
    CHECK(out.screens[0] == before.screens[0]);
    CHECK(out.screens[1].name == "Pixel Map" && out.screens[1].slices.size() == 3);
    CHECK(out.screens[1].id != out.screens[0].id);
    const int idx3 = pm::createMapping(c, out, pm::MapMode::Add, -1, 1920, 1080);
    CHECK(out.screens[static_cast<size_t>(idx3)].name == "Pixel Map 2");

    OutputSetup empty;
    CHECK(pm::createMapping(c, empty, pm::MapMode::Replace, 5, 1920, 1080) == 0 && empty.screens.size() == 1);

    OutputSetup small;
    pm::createMapping(c, small, pm::MapMode::Add, -1, 960, 540);
    CHECK(small.screens[0].slices[0].input.w == 960 && small.screens[0].slices[0].input.h == 108);

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
    OutputSetup again;
    pm::createMapping(back, again, pm::MapMode::Add, -1, 1920, 1080);
    CHECK(again.screens[0].slices == out.screens[0].slices);

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

std::vector<std::uint8_t> readBytes(const char* name)
{
    std::ifstream in(fs::path(BSL_FIXTURES_DIR) / "novalct" / name, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const novascr::Cabinet* findCab(const novascr::Result& r, int port, int order)
{
    for (const novascr::Cabinet& c : r.cabinets)
        if (c.port == port && c.order == order)
            return &c;
    return nullptr;
}

void testNovaScr()
{
    section("NovaLCT .scr: gabaritos");
    {
        const novascr::Result r = novascr::parse(readBytes("3x2 p3.9 50x100 128x256.scr"));
        CHECK(r.ok && r.version == 1 && r.cabinets.size() == 12 && r.width == 768 && r.height == 512);
        CHECK(r.cols == 6 && r.rows == 2);
        for (int i = 0; i < 6 && r.ok; ++i) {
            const novascr::Cabinet* a = findCab(r, 0, i);
            const novascr::Cabinet* b = findCab(r, 0, 6 + i);
            CHECK(a && a->x == i * 128 && a->y == 256 && a->w == 128 && a->h == 256);
            CHECK(b && b->x == (5 - i) * 128 && b->y == 0 && b->w == 128 && b->h == 256);
        }
    }
    {
        const novascr::Result r = novascr::parse(readBytes("4x2 p3.9 50x100 128x256.scr"));
        CHECK(r.ok && r.version == 1 && r.cabinets.size() == 16 && r.width == 1024 && r.height == 512);
        for (int i = 0; i < 8 && r.ok; ++i) {
            const novascr::Cabinet* a = findCab(r, 0, i);
            const novascr::Cabinet* b = findCab(r, 0, 8 + i);
            CHECK(a && a->x == i * 128 && a->y == 0 && a->w == 128 && a->h == 256);
            CHECK(b && b->x == (7 - i) * 128 && b->y == 256 && b->w == 128 && b->h == 256);
        }
    }
    {
        const novascr::Result r = novascr::parse(readBytes("DISPLAY AUDITORIO.scr"));
        CHECK(r.ok && r.version == 2 && r.cabinets.size() == 12 && r.width == 1560 && r.height == 624);
        CHECK(novascr::portCount(r) == 2);
        const int xs[6] = {1248, 1092, 780, 468, 156, 0};
        for (int i = 0; i < 6 && r.ok; ++i) {
            const novascr::Cabinet* a = findCab(r, 0, i);
            const novascr::Cabinet* b = findCab(r, 1, i);
            CHECK(a && a->x == xs[i] && a->y == 312 && a->w == 312 && a->h == 312);
            CHECK(b && b->x == xs[i] && b->y == 0 && b->w == 312 && b->h == 312);
        }
    }

    section("NovaLCT .scr: arquivos ruins");
    {
        const std::vector<std::uint8_t> good = readBytes("DISPLAY AUDITORIO.scr");
        CHECK(!good.empty());
        for (const std::size_t n : {std::size_t(0), std::size_t(3), std::size_t(100), std::size_t(0x13A), std::size_t(0x145), std::size_t(0x150), std::size_t(0x145 + 12 * 16 - 1)}) {
            const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(std::min(n, good.size())));
            const novascr::Result r = novascr::parse(cut);
            CHECK(!r.ok && !r.error.empty() && r.cabinets.empty());
        }
        const std::vector<std::uint8_t> good1 = readBytes("3x2 p3.9 50x100 128x256.scr");
        for (std::size_t n = 0; n < std::size_t(0x14A + 12 * 17); n += 7) {
            const std::vector<std::uint8_t> cut(good1.begin(), good1.begin() + static_cast<std::ptrdiff_t>(n));
            CHECK(!novascr::parse(cut).ok);
        }
        std::vector<std::uint8_t> bad = good;
        bad[0] = 'X';
        CHECK(!novascr::parse(bad).ok && novascr::parse(bad).error.find("DSCI") != std::string::npos);
        bad = good;
        bad[0x13F] = 3;
        const novascr::Result v3 = novascr::parse(bad);
        CHECK(!v3.ok && v3.error.find("3 do arquivo") != std::string::npos);
        bad = good;
        bad[0x13A] = 9;
        CHECK(!novascr::parse(bad).ok);
        bad = good1;
        bad[0x144] = 0xFF;
        bad[0x145] = 0xFF;
        bad[0x146] = 0xFF;
        bad[0x147] = 0xFF;
        CHECK(!novascr::parse(bad).ok);
        bad = good;
        bad[0x140] = 0xFF;
        bad[0x141] = 0xFF;
        CHECK(!novascr::parse(bad).ok);
        CHECK(!novascr::parseFile("C:/nao/existe.scr").ok);
    }

    section("NovaLCT .scr: conversao para o Pixel Map");
    {
        const novascr::Result r = novascr::parse(readBytes("DISPLAY AUDITORIO.scr"));
        pm::Config cfg = sampleConfig();
        std::string err;
        CHECK(novascr::applyToPixelMap(r, cfg, novascr::ImportMode::Replace, &err));
        CHECK(cfg.panels.size() == 12 && cfg.physW == 1560 && cfg.physH == 624);
        for (size_t i = 0; i < r.cabinets.size() && i < cfg.panels.size(); ++i) {
            const novascr::Cabinet& c = r.cabinets[i];
            const pm::Panel& p = cfg.panels[i];
            CHECK(p.x == c.x && p.y == c.y && p.w == c.w && p.h == c.h && p.port == c.port && p.cableOrder == c.order && !p.byCabinets);
        }
        cfg.inW = 1560;
        cfg.inH = 624;
        const pm::Validation v = pm::validate(cfg);
        CHECK(v.outsideCount == 0 && !v.scaleWarn);

        pm::Config add = sampleConfig();
        CHECK(novascr::applyToPixelMap(r, add, novascr::ImportMode::AddBeside, &err));
        CHECK(add.panels.size() == 15 && add.panels[3].x == 2048 + r.cabinets[0].x && add.panels[3].y == r.cabinets[0].y);
        CHECK(add.physW == 2048 + 1560 && add.physH == 640);

        novascr::Result tooMany = r;
        tooMany.cabinets.assign(300, r.cabinets[0]);
        pm::Config big;
        CHECK(!novascr::applyToPixelMap(tooMany, big, novascr::ImportMode::Replace, &err) && !err.empty());
    }
}

void testCsv()
{
    section("CSV de ida e volta");
    pm::Config c = sampleConfig();
    c.panels.push_back(pm::makePanel("Backstage; \"Sul\", ç", -16, 40, 300, 70));
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
        CHECK(back[i].name == (i == 4 ? std::string("Com espaços") : c.panels[i].name));
        CHECK(back[i].x == c.panels[i].x && back[i].y == c.panels[i].y && back[i].w == c.panels[i].w && back[i].h == c.panels[i].h);
    }

    std::vector<pm::Panel> p2;
    CHECK(pm::fromCsv("name,x,y,w,h\r\nA,1,2,30,40\r\n\r\nB,5,6,70.4,80\r\n", p2, &err) && p2.size() == 2);
    CHECK(p2.size() == 2 && p2[0].name == "A" && p2[0].w == 30 && p2[1].w == 70 && p2[1].h == 80);
    CHECK(pm::fromCsv("A\t1\t2\t30\t40\n", p2, &err) && p2.size() == 1 && p2[0].x == 1);
    CHECK(pm::fromCsv("A;1;2;30,5;40\n", p2, &err) && p2.size() == 1 && p2[0].w == 31);
    CHECK(pm::fromCsv("\xEF\xBB\xBFnome;a;b;c;d\nA;1;2;3;4", p2, &err) && p2.size() == 1);

    const std::vector<pm::Panel> keep = p2;
    CHECK(!pm::fromCsv("", p2, &err) && !err.empty());
    CHECK(!pm::fromCsv("nome;x;y;w;h\n", p2, &err));
    CHECK(pm::fromCsv("A;1;2;abc;4\nB;1;2;3;4\n", p2, &err) && p2.size() == 1 && p2[0].name == "B");
    CHECK(pm::fromCsv("cab;a;b;c;d\nA;1;2;3;4\n", p2, &err) && p2.size() == 1);
    CHECK(!pm::fromCsv("A;1;2;3;4\nB;x;2;3;4\n", p2, &err));
    CHECK(!pm::fromCsv("A;1;2;0;4\n", p2, &err));
    CHECK(!pm::fromCsv("A;1;2\n", p2, &err));
    CHECK(!pm::fromCsv("A;1;2;99999;4\n", p2, &err));
    (void)keep;

    CHECK(pm::patternFileName(pm::makePanel("Testeira Palco", 0, 0, 2048, 128)) == "BSLLight_Pattern_Testeira Palco_2048x128.png");
    CHECK(pm::patternFileName(pm::makePanel("A/B:C*", 0, 0, 10, 20)) == "BSLLight_Pattern_A_B_C__10x20.png");
    CHECK(pm::upperUtf8("lateral Esquerda \xC3\xA7\xC3\xA3o") == "LATERAL ESQUERDA \xC3\x87\xC3\x83O");
}

void testValidation()
{
    section("validacao: sobreposicao, fora da area, escala nao inteira");
    pm::Config c = sampleConfig();
    pm::Validation v = pm::validate(c);
    CHECK(v.overlapCount == 0 && v.outsideCount == 0);
    CHECK(v.scaleWarn);

    c.panels.push_back(pm::makePanel("Em cima", 100, 100, 300, 300));
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
    one.inH = 1280;
    CHECK(!pm::validate(one).scaleWarn);
    one.inW = 3000;
    CHECK(pm::validate(one).scaleWarn);

    pm::Config bad;
    bad.inW = -5;
    bad.physH = 999999;
    bad.panels.push_back(pm::makePanel("", 0, 0, 0, -3));
    pm::normalize(bad);
    CHECK(bad.inW == 1 && bad.physH == pm::kMaxDim && bad.panels[0].w == 1 && bad.panels[0].h == 1 && !bad.panels[0].name.empty());
}

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

    CHECK(pixelIs(img, 0, 64, 255, 10, 51));
    CHECK(pixelIs(img, 2047, 64, 255, 10, 51));
    CHECK(pixelIs(img, 700, 0, 255, 10, 51));
    CHECK(pixelIs(img, 700, 127, 255, 10, 51));
    CHECK(!pixelIs(img, 1, 64, 255, 10, 51, 30));

    CHECK(pixelIs(img, 20, 54, 255, 255, 255));
    CHECK(pixelIs(img, 21, 54, 0, 0, 0));
    CHECK(pixelIs(img, 20, 55, 0, 0, 0));
    CHECK(pixelIs(img, 21, 55, 255, 255, 255));
    CHECK(pixelIs(img, 39, 73, 255, 255, 255));
    CHECK(pixelIs(img, 1014, 20, 255, 255, 255) && pixelIs(img, 1015, 20, 0, 0, 0));
    CHECK(pixelIs(img, 2008, 54, 255, 255, 255) && pixelIs(img, 2009, 54, 0, 0, 0));

    CHECK(pixelIs(img, 1024, 34, 255, 10, 51));
    {
        const std::uint8_t* p = img.px(305, 103);
        CHECK(p[0] < 40 && p[1] < 10 && p[2] < 10);
    }

    fs::create_directories(tmp);
    const fs::path file = tmp / pm::patternFileName(testeira);
    std::string err;
    CHECK(pm::writePng(file, img, &err));
    pm::Image rd;
    CHECK(pm::loadImageFile(file, rd, &err));
    CHECK(rd.w == 2048 && rd.h == 128 && rd.rgba == img.rgba);

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
    const pm::Image tiny = pm::renderPattern(pm::makePanel("P", 3, 4, 64, 32), nullptr);
    CHECK(tiny.w == 64 && tiny.h == 32);
    CHECK(pm::renderPattern(pm::makePanel("X", 0, 0, 0, 0), nullptr).empty());

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
    CHECK(pixelIs(ov, 1007, 1073, 5, 5, 5, 2));
    CHECK(pixelIs(ov, 7, 107, 55, 6, 14, 3));
    CHECK(pixelIs(ov, 7, 507, 4, 50, 55, 3));
    CHECK(pixelIs(ov, 1447, 507, 30, 30, 30, 3));
    CHECK(pixelIs(ov, 241, 216, 0, 229, 255, 3));
    const pm::Image nowm = pm::renderOverview(c, nullptr);
    CHECK(nowm.rgba != ov.rgba);
    fs::create_directories(tmp);
    std::string err;
    CHECK(pm::writePng(tmp / "overview.png", ov, &err));
}

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
    values.set("smooth", media::Json(false));
    src->setParams(values);
    src->setCanvasSize(1920, 1080);
    CHECK(src->open({}));
    src->update(1.0 / 60.0);
    CHECK(src->width() == 1920 && src->height() == 1080 && src->texture() != 0);

    auto px = readTexture(src->texture(), 1920, 1080);
    auto at = [&](int x, int y) { return px.data() + (static_cast<size_t>(y) * 1920u + static_cast<size_t>(x)) * 4; };
    CHECK(near(at(0, 100)[0], 255, 1) && near(at(0, 100)[1], 10, 1) && near(at(0, 100)[2], 51, 1));
    CHECK(at(960, 700)[0] == 0 && at(960, 700)[1] == 0 && at(960, 700)[2] == 0 && at(960, 700)[3] == 255);
    CHECK(near(at(0, 600)[0], 255, 1) && near(at(0, 600)[1], 10, 1));
    CHECK(near(at(1919, 600)[0], 255, 1) && near(at(1440, 600)[0], 255, 1));

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

    pm::Config c2 = c;
    c2.panels.push_back(pm::makePanel("Centro", 600, 200, 400, 200));
    bl::PixelMapFeed::instance().set(c2);
    values.set("bar", media::Json(false));
    values.set("counter", media::Json(false));
    src->setParams(values);
    src->update(0.016);
    const auto c2px = readTexture(src->texture(), 1920, 1080);
    CHECK(c2px != still2);

    src->setCanvasSize(960, 540);
    src->update(0.016);
    CHECK(src->width() == 960 && src->height() == 540);
    src->close();
    CHECK(!src->isOpen());

    {
        bl::PixelMapFeed::instance().set(pm::Config{});
        auto empty = info->create(1280, 720);
        empty->setCanvasSize(1280, 720);
        CHECK(empty->open({}));
        empty->update(1.0 / 60.0);
        const auto e = readTexture(empty->texture(), 1280, 720);
        size_t lit = 0;
        for (size_t i = 0; i + 3 < e.size(); i += 4)
            lit += (e[i] > 40 || e[i + 1] > 40 || e[i + 2] > 40) ? 1 : 0;
        std::printf("  Pixel Map vazio: %.1f %% dos pixels acesos\n", 100.0 * static_cast<double>(lit) / (1280.0 * 720.0));
        CHECK(lit > 1280u * 720u / 20u);
        CHECK(empty->status().text.find("Pixel Map vazio") != std::string::npos);
        pm::Config one;
        one.panels.push_back(pm::makePanel("Unico", 0, 0, 1024, 640));
        bl::PixelMapFeed::instance().set(one);
        empty->update(0.016);
        CHECK(empty->status().text.find("Pixel Map vazio") == std::string::npos);
        empty->close();
    }
}

}

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
    testNovaScr();
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
