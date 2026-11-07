#include "output/PatternRaster.h"

#include "app/Log.h"
#include "app/Paths.h"

#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>

namespace bsl::pixelmap {

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr long long kMaxPixels = 160'000'000;

inline std::uint8_t toByte(float v)
{
    return static_cast<std::uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline void blendPx(std::uint8_t* p, const Color& c, float cov)
{
    const float a = c.a * cov;
    if (a <= 0.0f)
        return;
    if (p[3] == 255) {
        const float inv = 1.0f - a;
        p[0] = toByte((static_cast<float>(p[0]) * inv) / 255.0f + c.r * a);
        p[1] = toByte((static_cast<float>(p[1]) * inv) / 255.0f + c.g * a);
        p[2] = toByte((static_cast<float>(p[2]) * inv) / 255.0f + c.b * a);
        return;
    }
    const float da = static_cast<float>(p[3]) / 255.0f;
    const float outA = a + da * (1.0f - a);
    if (outA <= 0.0f)
        return;
    const float k = da * (1.0f - a);
    p[0] = toByte((c.r * a + (static_cast<float>(p[0]) / 255.0f) * k) / outA);
    p[1] = toByte((c.g * a + (static_cast<float>(p[1]) / 255.0f) * k) / outA);
    p[2] = toByte((c.b * a + (static_cast<float>(p[2]) / 255.0f) * k) / outA);
    p[3] = toByte(outA);
}

struct FontData
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

FontData& fontOf(FontId id)
{
    static FontData fonts[2];
    static std::once_flag once[2];
    const int i = id == FontId::Bold ? 0 : 1;
    std::call_once(once[i], [i]() {
        const fs::path dir = paths::assetsDir();
        if (dir.empty())
            return;
        const fs::path file = dir / L"fonts" / (i == 0 ? L"Barlow-Bold.ttf" : L"JetBrainsMono.ttf");
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return;
        fonts[i].data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        const int offset = stbtt_GetFontOffsetForIndex(fonts[i].data.data(), 0);
        fonts[i].ok = offset >= 0 && stbtt_InitFont(&fonts[i].info, fonts[i].data.data(), offset) != 0;
        if (!fonts[i].ok)
            log::warn("Pixel Map: não foi possível ler a fonte " + paths::toUtf8(file.filename()));
    });
    return fonts[i];
}

std::vector<unsigned> decode(const std::string& s)
{
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6)
                 | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12)
                 | ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            len = 4;
        }
        if (cp != '\r' && cp != '\n')
            out.push_back(cp);
        i += len;
    }
    return out;
}

double advanceOf(const FontData& f, float scale, const std::vector<unsigned>& cps, size_t i)
{
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f.info, static_cast<int>(cps[i]), &adv, &lsb);
    double a = static_cast<double>(adv) * static_cast<double>(scale);
    if (i + 1 < cps.size())
        a += static_cast<double>(stbtt_GetCodepointKernAdvance(&f.info, static_cast<int>(cps[i]), static_cast<int>(cps[i + 1])))
             * static_cast<double>(scale);
    return a;
}

}

Image::Image(int width, int height, std::uint32_t rgbHex) : w(std::max(width, 0)), h(std::max(height, 0))
{
    rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = static_cast<std::uint8_t>((rgbHex >> 16) & 0xFF);
        rgba[i + 1] = static_cast<std::uint8_t>((rgbHex >> 8) & 0xFF);
        rgba[i + 2] = static_cast<std::uint8_t>(rgbHex & 0xFF);
        rgba[i + 3] = 255;
    }
}

Color rgb(std::uint32_t hex, float alpha)
{
    return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f, static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
            static_cast<float>(hex & 0xFF) / 255.0f, alpha};
}

void Surface::fillRect(double x, double y, double w, double h, Color c)
{
    if (w <= 0.0 || h <= 0.0 || img_.empty())
        return;
    const int x0 = std::max(0, static_cast<int>(std::floor(x)));
    const int x1 = std::min(img_.w, static_cast<int>(std::ceil(x + w)));
    const int y0 = std::max(0, static_cast<int>(std::floor(y)));
    const int y1 = std::min(img_.h, static_cast<int>(std::ceil(y + h)));
    for (int py = y0; py < y1; ++py) {
        const double cy = std::min<double>(py + 1, y + h) - std::max<double>(py, y);
        if (cy <= 0.0)
            continue;
        for (int px = x0; px < x1; ++px) {
            const double cx = std::min<double>(px + 1, x + w) - std::max<double>(px, x);
            if (cx > 0.0)
                blendPx(img_.px(px, py), c, static_cast<float>(cx * cy));
        }
    }
}

void Surface::strokeRect(double x, double y, double w, double h, double lw, Color c)
{
    const double e = lw * 0.5;
    fillRect(x - e, y - e, w + lw, lw, c);
    fillRect(x - e, y + h - e, w + lw, lw, c);
    fillRect(x - e, y + e, lw, h - lw, c);
    fillRect(x + w - e, y + e, lw, h - lw, c);
}

void Surface::hline(double y, double x0, double x1, double lw, Color c)
{
    fillRect(std::min(x0, x1), y - lw * 0.5, std::abs(x1 - x0), lw, c);
}

void Surface::vline(double x, double y0, double y1, double lw, Color c)
{
    fillRect(x - lw * 0.5, std::min(y0, y1), lw, std::abs(y1 - y0), c);
}

void Surface::ring(double cx, double cy, double r, double lw, Color c, double dashOn, double dashOff)
{
    if (r <= 0.0 || lw <= 0.0)
        return;
    const double ext = r + lw * 0.5 + 1.0;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - ext)));
    const int x1 = std::min(img_.w, static_cast<int>(std::ceil(cx + ext)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - ext)));
    const int y1 = std::min(img_.h, static_cast<int>(std::ceil(cy + ext)));
    const bool dashed = dashOn > 0.0 && dashOff > 0.0;
    const double period = dashOn + dashOff;
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const double dx = px + 0.5 - cx, dy = py + 0.5 - cy;
            const double d = std::sqrt(dx * dx + dy * dy);
            double cov = std::clamp(lw * 0.5 - std::abs(d - r) + 0.5, 0.0, 1.0);
            if (cov <= 0.0)
                continue;
            if (dashed) {
                double ang = std::atan2(dy, dx);
                if (ang < 0.0)
                    ang += 2.0 * kPi;
                double m = std::fmod(ang * r, period);
                if (m > period - 0.5)
                    m -= period;
                cov *= std::clamp(std::min(m, dashOn - m) + 0.5, 0.0, 1.0);
                if (cov <= 0.0)
                    continue;
            }
            blendPx(img_.px(px, py), c, static_cast<float>(cov));
        }
    }
}

void Surface::fillRadial(double cx, double cy, double r1, Color c0, Color c1)
{
    if (r1 <= 0.0)
        r1 = 1.0;
    for (int py = 0; py < img_.h; ++py) {
        for (int px = 0; px < img_.w; ++px) {
            const double dx = px + 0.5 - cx, dy = py + 0.5 - cy;
            const float t = static_cast<float>(std::min(1.0, std::sqrt(dx * dx + dy * dy) / r1));
            std::uint8_t* p = img_.px(px, py);
            p[0] = toByte(c0.r + (c1.r - c0.r) * t);
            p[1] = toByte(c0.g + (c1.g - c0.g) * t);
            p[2] = toByte(c0.b + (c1.b - c0.b) * t);
            p[3] = 255;
        }
    }
}

void Surface::fillRampH(double x0, double x1, double y, double h, Color c0, Color c1)
{
    if (x1 <= x0 || h <= 0.0)
        return;
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0)));
    const int ix1 = std::min(img_.w, static_cast<int>(std::ceil(x1)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(y)));
    const int iy1 = std::min(img_.h, static_cast<int>(std::ceil(y + h)));
    for (int py = iy0; py < iy1; ++py) {
        const double cy = std::min<double>(py + 1, y + h) - std::max<double>(py, y);
        if (cy <= 0.0)
            continue;
        for (int px = ix0; px < ix1; ++px) {
            const double cx = std::min<double>(px + 1, x1) - std::max<double>(px, x0);
            if (cx <= 0.0)
                continue;
            const float t = static_cast<float>(std::clamp((px + 0.5 - x0) / (x1 - x0), 0.0, 1.0));
            const Color c{c0.r + (c1.r - c0.r) * t, c0.g + (c1.g - c0.g) * t, c0.b + (c1.b - c0.b) * t,
                          c0.a + (c1.a - c0.a) * t};
            blendPx(img_.px(px, py), c, static_cast<float>(cx * cy));
        }
    }
}

namespace {

struct Premult
{
    float r = 0, g = 0, b = 0, a = 0;
};

Premult fetch(const Image& s, int x, int y)
{
    x = std::clamp(x, 0, s.w - 1);
    y = std::clamp(y, 0, s.h - 1);
    const std::uint8_t* p = s.px(x, y);
    const float a = static_cast<float>(p[3]) / 255.0f;
    return {static_cast<float>(p[0]) / 255.0f * a, static_cast<float>(p[1]) / 255.0f * a,
            static_cast<float>(p[2]) / 255.0f * a, a};
}

Premult bilinear(const Image& s, double u, double v)
{
    const double fu = u - 0.5, fv = v - 0.5;
    const int x0 = static_cast<int>(std::floor(fu)), y0 = static_cast<int>(std::floor(fv));
    const float tx = static_cast<float>(fu - x0), ty = static_cast<float>(fv - y0);
    const Premult a = fetch(s, x0, y0), b = fetch(s, x0 + 1, y0), c = fetch(s, x0, y0 + 1), d = fetch(s, x0 + 1, y0 + 1);
    auto mix = [&](float Premult::*m) {
        const float top = a.*m + (b.*m - a.*m) * tx;
        const float bot = c.*m + (d.*m - c.*m) * tx;
        return top + (bot - top) * ty;
    };
    return {mix(&Premult::r), mix(&Premult::g), mix(&Premult::b), mix(&Premult::a)};
}

}

void Surface::drawImage(const Image& src, double x, double y, double w, double h, float alpha, bool smooth)
{
    if (src.empty() || w <= 0.0 || h <= 0.0 || img_.empty())
        return;
    const int x0 = std::max(0, static_cast<int>(std::floor(x)));
    const int x1 = std::min(img_.w, static_cast<int>(std::ceil(x + w)));
    const int y0 = std::max(0, static_cast<int>(std::floor(y)));
    const int y1 = std::min(img_.h, static_cast<int>(std::ceil(y + h)));
    const double sx = static_cast<double>(src.w) / w, sy = static_cast<double>(src.h) / h;
    const int tx = smooth ? std::clamp(static_cast<int>(std::ceil(sx)), 1, 8) : 1;
    const int ty = smooth ? std::clamp(static_cast<int>(std::ceil(sy)), 1, 8) : 1;
    const float inv = 1.0f / static_cast<float>(tx * ty);
    for (int py = y0; py < y1; ++py) {
        const double cy = std::min<double>(py + 1, y + h) - std::max<double>(py, y);
        if (cy <= 0.0)
            continue;
        for (int px = x0; px < x1; ++px) {
            const double cx = std::min<double>(px + 1, x + w) - std::max<double>(px, x);
            if (cx <= 0.0)
                continue;
            Premult acc;
            for (int j = 0; j < ty; ++j) {
                for (int i = 0; i < tx; ++i) {
                    const double u = (px + (i + 0.5) / tx - x) * sx;
                    const double v = (py + (j + 0.5) / ty - y) * sy;
                    Premult s;
                    if (smooth) {
                        s = bilinear(src, u, v);
                    } else {
                        s = fetch(src, static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)));
                    }
                    acc.r += s.r;
                    acc.g += s.g;
                    acc.b += s.b;
                    acc.a += s.a;
                }
            }
            acc.r *= inv;
            acc.g *= inv;
            acc.b *= inv;
            acc.a *= inv;
            if (acc.a <= 0.0f)
                continue;
            const Color c{acc.r / acc.a, acc.g / acc.a, acc.b / acc.a, acc.a * alpha};
            blendPx(img_.px(px, py), c, static_cast<float>(cx * cy));
        }
    }
}

void Surface::text(const std::string& utf8, double cx, double cy, double sizePx, Color c, FontId font)
{
    if (sizePx < 1.0 || utf8.empty() || img_.empty())
        return;
    sizePx = std::min(sizePx, 2000.0);
    const FontData& f = fontOf(font);
    if (!f.ok)
        return;
    const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, static_cast<float>(sizePx));
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
    const std::vector<unsigned> cps = decode(utf8);
    double total = 0.0;
    for (size_t i = 0; i < cps.size(); ++i)
        total += advanceOf(f, scale, cps, i);
    double pen = cx - total * 0.5;
    const double baseline = cy + (static_cast<double>(asc) + static_cast<double>(desc)) * 0.5 * static_cast<double>(scale);
    const int oy = static_cast<int>(std::floor(baseline));
    const float shy = static_cast<float>(baseline - oy);
    std::vector<unsigned char> bmp;
    for (size_t i = 0; i < cps.size(); ++i) {
        const int ox = static_cast<int>(std::floor(pen));
        const float shx = static_cast<float>(pen - ox);
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&f.info, static_cast<int>(cps[i]), scale, scale, shx, shy, &ix0, &iy0, &ix1, &iy1);
        const int gw = ix1 - ix0, gh = iy1 - iy0;
        if (gw > 0 && gh > 0 && gw < 4096 && gh < 4096) {
            bmp.assign(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0);
            stbtt_MakeCodepointBitmapSubpixel(&f.info, bmp.data(), gw, gh, gw, scale, scale, shx, shy, static_cast<int>(cps[i]));
            for (int gy = 0; gy < gh; ++gy) {
                const int py = oy + iy0 + gy;
                if (py < 0 || py >= img_.h)
                    continue;
                for (int gx = 0; gx < gw; ++gx) {
                    const int px = ox + ix0 + gx;
                    if (px < 0 || px >= img_.w)
                        continue;
                    const unsigned char cov = bmp[static_cast<size_t>(gy) * static_cast<size_t>(gw) + static_cast<size_t>(gx)];
                    if (cov)
                        blendPx(img_.px(px, py), c, static_cast<float>(cov) / 255.0f);
                }
            }
        }
        pen += advanceOf(f, scale, cps, i);
    }
}

double textWidth(const std::string& utf8, double sizePx, FontId font)
{
    const FontData& f = fontOf(font);
    if (!f.ok || sizePx <= 0.0)
        return 0.0;
    const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, static_cast<float>(sizePx));
    const std::vector<unsigned> cps = decode(utf8);
    double total = 0.0;
    for (size_t i = 0; i < cps.size(); ++i)
        total += advanceOf(f, scale, cps, i);
    return total;
}

bool fontsAvailable()
{
    return fontOf(FontId::Bold).ok && fontOf(FontId::Mono).ok;
}

Image renderPattern(const Panel& p, const Image* logo)
{
    const int w = p.w, h = p.h;
    if (w < 1 || h < 1 || static_cast<long long>(w) * h > kMaxPixels)
        return {};
    const double W = w, H = h, cx = W / 2.0, cy = H / 2.0;
    Image img(w, h);
    Surface s(img);

    s.fillRadial(cx, cy, std::max(W, H), rgb(0x140003), rgb(0x020000));

    const Color fine = rgb(0xFFFFFF, 0.03f);
    for (int i = 0; i < w; i += 10)
        s.vline(i, 0, H, 1.0, fine);
    for (int i = 0; i < h; i += 10)
        s.hline(i, 0, W, 1.0, fine);
    const Color mid = rgb(0xFF0A33, 0.1f);
    for (int i = 0; i < w; i += 50)
        s.vline(i, 0, H, 1.0, mid);
    for (int i = 0; i < h; i += 50)
        s.hline(i, 0, W, 1.0, mid);

    const double radius = std::min(W, H) * 0.4;
    s.ring(cx, cy, radius, 3.0, rgb(0xFF0A33));
    s.ring(cx, cy, radius * 0.8, 1.0, rgb(0x00E5FF), 5.0, 10.0);

    const Color cross = rgb(0x00E5FF, 0.3f);
    s.vline(cx, 0, H, 1.0, cross);
    s.hline(cy, 0, W, 1.0, cross);

    const double len = std::min(W, H) * 0.15;
    const Color red = rgb(0xFF0A33);
    const double e = 2.0;
    s.vline(15, 15 - e, 15 + len, 4.0, red);
    s.hline(15, 15, 15 + len, 4.0, red);
    s.vline(W - 15, 15 - e, 15 + len, 4.0, red);
    s.hline(15, W - 15 - len, W - 15, 4.0, red);
    s.vline(15, H - 15 - len, H - 15 + e, 4.0, red);
    s.hline(H - 15, 15, 15 + len, 4.0, red);
    s.vline(W - 15, H - 15 - len, H - 15 + e, 4.0, red);
    s.hline(H - 15, W - 15 - len, W - 15, 4.0, red);

    constexpr int kChk = 20;
    auto checker = [&](double x, double y) {
        for (int i = 0; i < kChk; ++i)
            for (int j = 0; j < kChk; ++j)
                s.fillRect(x + i, y + j, 1.0, 1.0, (i + j) % 2 == 0 ? rgb(0xFFFFFF) : rgb(0x000000));
    };
    checker(cx - kChk / 2.0, 20);
    checker(cx - kChk / 2.0, H - 20 - kChk);
    checker(20, cy - kChk / 2.0);
    checker(W - 20 - kChk, cy - kChk / 2.0);

    s.strokeRect(0, 0, W, H, 2.0, red);

    if (h > 120 && w > 300) {
        const double barW = std::min(W * 0.8, 1000.0);
        const double barH = std::min(H * 0.12, 60.0);
        const double startX = cx - barW / 2.0;
        const double startY = H - barH - 30.0;
        const std::uint32_t colors[5] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000};
        const double stepW = barW / 5.0;
        for (int i = 0; i < 5; ++i)
            s.fillRect(startX + i * stepW, startY, stepW, barH, rgb(colors[i]));
        s.fillRampH(startX, startX + barW, startY - 20.0, 15.0, rgb(0xFFFFFF), rgb(0x000000));
        s.strokeRect(startX, startY, barW, barH, 1.0, rgb(0x222222));
    }

    const double boxW = std::min(W * 0.6, 600.0);
    const double boxH = std::min(H * 0.5, 240.0);
    const double boxX = cx - boxW / 2.0;
    const double boxY = cy - boxH / 2.0;
    s.fillRect(boxX, boxY, boxW, boxH, rgb(0x050505, 0.85f));
    s.strokeRect(boxX, boxY, boxW, boxH, 2.0, red);
    s.fillRect(boxX, boxY, boxW, 5.0, red);

    double logoH = boxH * 0.35;
    if (logo && !logo->empty()) {
        const double aspect = static_cast<double>(logo->w) / static_cast<double>(logo->h);
        const double logoW = logoH * aspect;
        s.drawImage(*logo, cx - logoW / 2.0, boxY + 15.0, logoW, logoH);
    } else {
        logoH = 10.0;
    }

    const double base = std::min({boxW * 0.08, boxH * 0.15, 32.0});
    s.text("BSL LIGHT PIXEL MAPPING", cx, boxY + logoH + 30.0, base * 0.6, red, FontId::Bold);
    s.text(upperUtf8(p.name), cx, boxY + logoH + 30.0 + base, base * 1.2, rgb(0xFFFFFF), FontId::Bold);
    s.text("RES F\xC3\x8DSICA: " + std::to_string(w) + "x" + std::to_string(h) + " px", cx, boxY + boxH - 35.0, base * 0.65,
           rgb(0x00E5FF), FontId::Mono);
    s.text("COORDENADA: X:" + std::to_string(p.x) + " Y:" + std::to_string(p.y), cx, boxY + boxH - 15.0, base * 0.5,
           rgb(0x888888), FontId::Mono);
    return img;
}

Image renderOverview(const Config& c, const Image* watermark)
{
    const int inW = c.inW, inH = c.inH;
    if (inW < 1 || inH < 1 || static_cast<long long>(inW) * inH > kMaxPixels)
        return {};
    Image img(inW, inH, 0x050505);
    Surface s(img);
    const double W = inW, H = inH;

    const Color g20 = rgb(0xFF0A33, 0.05f);
    for (int i = 0; i < inW; i += 20)
        s.vline(i, 0, H, 1.0, g20);
    for (int i = 0; i < inH; i += 20)
        s.hline(i, 0, W, 1.0, g20);
    const Color g100 = rgb(0xFF0A33, 0.15f);
    for (int i = 0; i < inW; i += 100)
        s.vline(i, 0, H, 1.0, g100);
    for (int i = 0; i < inH; i += 100)
        s.hline(i, 0, W, 1.0, g100);

    if (watermark && !watermark->empty()) {
        const double mw = std::min(W * 0.4, 400.0);
        const double mh = mw / (static_cast<double>(watermark->w) / static_cast<double>(watermark->h));
        s.drawImage(*watermark, (W - mw) / 2.0, (H - mh) / 2.0, mw, mh, 0.1f);
    }

    int index = 0;
    for (const Panel& p : c.panels) {
        const RectD r = scaledRect(c, p);
        const std::uint32_t col = panelColor(p, index);
        s.fillRect(r.x, r.y, r.w, r.h, rgb(col, panelFill(p, index)));
        s.strokeRect(r.x, r.y, r.w, r.h, 2.0, rgb(col));
        const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
        const double fs = std::min({r.w * 0.08, r.h * 0.25, 30.0});
        s.text(upperUtf8(p.name), cx, cy - fs * 0.6, fs, rgb(0xFFFFFF), FontId::Bold);
        const RectI rr = scaledRectRounded(c, p);
        s.text(std::to_string(rr.w) + "x" + std::to_string(rr.h), cx, cy + fs * 0.6, fs * 0.6, rgb(col), FontId::Mono);
        ++index;
    }
    return img;
}

bool loadImageFile(const fs::path& file, Image& out, std::string* error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error)
            *error = "não foi possível abrir " + paths::toUtf8(file);
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!data || w < 1 || h < 1) {
        if (data)
            stbi_image_free(data);
        if (error)
            *error = "formato de imagem não suportado (use PNG, JPG ou BMP)";
        return false;
    }
    out.w = w;
    out.h = h;
    out.rgba.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    return true;
}

bool loadBrandMonogram(Image& out)
{
    const fs::path dir = paths::assetsDir();
    if (dir.empty())
        return false;
    return loadImageFile(dir / L"brand" / L"bsl-monogram.png", out);
}

bool loadWatermark(const Config& c, Image& out)
{
    switch (c.watermark) {
    case Watermark::Monogram: return loadBrandMonogram(out);
    case Watermark::Image:
        if (c.watermarkPath.empty())
            return false;
        return loadImageFile(fs::path(std::u8string(c.watermarkPath.begin(), c.watermarkPath.end())), out);
    default: return false;
    }
}

bool writePng(const fs::path& file, const Image& img, std::string* error)
{
    if (img.empty()) {
        if (error)
            *error = "imagem vazia";
        return false;
    }
    std::error_code ec;
    if (file.has_parent_path())
        fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error)
            *error = "não foi possível criar " + paths::toUtf8(file);
        return false;
    }
    auto sink = [](void* ctx, void* data, int size) {
        static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size);
    };
    const int ok = stbi_write_png_to_func(sink, &out, img.w, img.h, 4, img.rgba.data(), img.w * 4);
    out.flush();
    if (!ok || !out) {
        if (error)
            *error = "falha ao gravar " + paths::toUtf8(file);
        return false;
    }
    return true;
}

}
