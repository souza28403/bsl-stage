#include "media/TextRaster.h"

#include "app/Log.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace bsl::media {

namespace fs = std::filesystem;

namespace {

constexpr float kInf = 1e20f;

// --- Fonte carregada (cache por arquivo; os dados precisam ficar vivos enquanto o stbtt_fontinfo existir) ---
struct Font
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
};

std::shared_ptr<Font> loadFont(const fs::path& path)
{
    static std::mutex m;
    static std::map<fs::path, std::shared_ptr<Font>> cache;
    std::lock_guard lk(m);
    if (const auto it = cache.find(path); it != cache.end())
        return it->second;

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return nullptr;
    auto font = std::make_shared<Font>();
    font->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (font->data.size() < 16)
        return nullptr;
    const int offset = stbtt_GetFontOffsetForIndex(font->data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&font->info, font->data.data(), offset))
        return nullptr;
    cache[path] = font;
    return font;
}

// Metricas e avancos na escala escolhida
struct Face
{
    const stbtt_fontinfo* info = nullptr;
    float scale = 1.0f;
    float ascent = 0.0f;  // px, acima da linha de base
    float descent = 0.0f; // px, negativo
    float lineGap = 0.0f;
    float spacing = 0.0f; // tracking: somado a cada avanco (definido antes do primeiro uso)
    std::unordered_map<char32_t, float> advCache;

    float advance(char32_t cp)
    {
        if (const auto it = advCache.find(cp); it != advCache.end())
            return it->second;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(info, static_cast<int>(cp), &adv, &lsb);
        const float v = static_cast<float>(adv) * scale + spacing;
        advCache[cp] = v;
        return v;
    }
    float kern(char32_t a, char32_t b) const
    {
        return static_cast<float>(stbtt_GetCodepointKernAdvance(info, static_cast<int>(a), static_cast<int>(b))) *
               scale;
    }
};

struct Line
{
    size_t begin = 0;
    size_t end = 0; // [begin, end)
    float width = 0.0f;
};

// Quebra em linhas: '\n' explicito e, se wrapW > 0, quebra automatica por palavra (palavra maior que a
// linha e partida no caractere).
std::vector<Line> layoutLines(const std::vector<char32_t>& cps, Face& face, float wrapW)
{
    std::vector<Line> lines;
    size_t ps = 0;
    while (ps <= cps.size()) {
        size_t pe = ps;
        while (pe < cps.size() && cps[pe] != U'\n')
            ++pe;

        if (pe == ps) {
            lines.push_back({ps, ps, 0.0f});
        } else {
            size_t start = ps;
            while (start < pe) {
                float w = 0.0f;
                float widthAtSpace = 0.0f;
                size_t lastSpace = static_cast<size_t>(-1);
                bool broke = false;
                for (size_t i = start; i < pe; ++i) {
                    const float adv = face.advance(cps[i]) + (i > start ? face.kern(cps[i - 1], cps[i]) : 0.0f);
                    if (cps[i] == U' ') {
                        lastSpace = i;
                        widthAtSpace = w;
                    }
                    if (wrapW > 0.0f && cps[i] != U' ' && i > start && w + adv > wrapW) {
                        if (lastSpace != static_cast<size_t>(-1) && lastSpace > start) {
                            lines.push_back({start, lastSpace, widthAtSpace});
                            start = lastSpace + 1;
                        } else {
                            lines.push_back({start, i, w});
                            start = i;
                        }
                        broke = true;
                        break;
                    }
                    w += adv;
                }
                if (!broke) {
                    size_t e = pe;
                    while (e > start && cps[e - 1] == U' ') {
                        w -= face.advance(U' ');
                        --e;
                    }
                    lines.push_back({start, e, std::max(0.0f, w)});
                    start = pe;
                }
            }
        }
        if (pe >= cps.size())
            break;
        ps = pe + 1;
    }
    return lines;
}

// --- Mascaras de 8 bits ---
struct Mask
{
    int w = 0, h = 0;
    std::vector<unsigned char> v;
    void reset(int width, int height)
    {
        w = width;
        h = height;
        v.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    }
    unsigned char at(int x, int y) const { return v[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
    unsigned char& at(int x, int y) { return v[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
};

// Transformada de distancia euclidiana (Felzenszwalb): d[q] = min_p ((q-p)^2 + f[p])
void edt1d(const float* f, float* d, int n, int* v, float* z)
{
    int k = 0;
    v[0] = 0;
    z[0] = -kInf;
    z[1] = kInf;
    for (int q = 1; q < n; ++q) {
        float s = 0.0f;
        while (true) {
            const float fq = f[q] + static_cast<float>(q) * static_cast<float>(q);
            const float fv = f[v[k]] + static_cast<float>(v[k]) * static_cast<float>(v[k]);
            s = (fq - fv) / static_cast<float>(2 * q - 2 * v[k]);
            if (k > 0 && s <= z[k])
                --k;
            else
                break;
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = kInf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<float>(q))
            ++k;
        const float dx = static_cast<float>(q - v[k]);
        d[q] = dx * dx + f[v[k]];
    }
}

// Contorno: dilata a mascara 'fill' por 'radius' px com distancia euclidiana exata. Devolve cobertura 0..255
// (inclui o miolo; quem usa recorta o miolo).
Mask dilate(const Mask& fill, float radius)
{
    const int w = fill.w, h = fill.h;
    const int n = std::max(w, h);
    std::vector<float> grid(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < grid.size(); ++i)
        grid[i] = fill.v[i] >= 128 ? 0.0f : kInf;

    std::vector<float> f(static_cast<size_t>(n)), d(static_cast<size_t>(n)), z(static_cast<size_t>(n) + 1);
    std::vector<int> v(static_cast<size_t>(n));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y)
            f[static_cast<size_t>(y)] = grid[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        edt1d(f.data(), d.data(), h, v.data(), z.data());
        for (int y = 0; y < h; ++y)
            grid[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = d[static_cast<size_t>(y)];
    }
    for (int y = 0; y < h; ++y) {
        float* row = grid.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        edt1d(row, d.data(), w, v.data(), z.data());
        std::copy(d.begin(), d.begin() + w, row);
    }

    Mask out;
    out.reset(w, h);
    for (size_t i = 0; i < grid.size(); ++i) {
        const float dist = std::sqrt(grid[i]);
        const float a = std::clamp(radius + 0.5f - dist, 0.0f, 1.0f);
        const float c = std::max(a, static_cast<float>(fill.v[i]) / 255.0f);
        out.v[i] = static_cast<unsigned char>(std::lround(c * 255.0f));
    }
    return out;
}

void boxBlurLine(const unsigned char* src, unsigned char* dst, int n, int stride, int r, std::vector<int>& prefix)
{
    prefix.assign(static_cast<size_t>(n) + 1, 0);
    for (int i = 0; i < n; ++i)
        prefix[static_cast<size_t>(i) + 1] = prefix[static_cast<size_t>(i)] + src[static_cast<ptrdiff_t>(i) * stride];
    const int win = 2 * r + 1;
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - r);
        const int b = std::min(n, i + r + 1);
        dst[static_cast<ptrdiff_t>(i) * stride] =
            static_cast<unsigned char>((prefix[static_cast<size_t>(b)] - prefix[static_cast<size_t>(a)] + win / 2) / win);
    }
}

// Desfoque aproximado (gaussiano) por 2 passadas de media em caixa em cada eixo
void blur(Mask& m, int radius)
{
    if (radius <= 0)
        return;
    std::vector<int> prefix;
    std::vector<unsigned char> tmp(m.v.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < m.h; ++y)
            boxBlurLine(m.v.data() + static_cast<size_t>(y) * static_cast<size_t>(m.w),
                        tmp.data() + static_cast<size_t>(y) * static_cast<size_t>(m.w), m.w, 1, radius, prefix);
        for (int x = 0; x < m.w; ++x)
            boxBlurLine(tmp.data() + x, m.v.data() + x, m.h, m.w, radius, prefix);
    }
}

// Desenha um glifo na mascara (uniao das coberturas). Atualiza a caixa dos pixels tocados.
struct Bounds
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    bool empty() const { return x1 < x0; }
    void add(int x, int y)
    {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
};

void blitGlyph(const Face& face, char32_t cp, float penX, int baseline, Mask& dst, Bounds& bounds,
               std::vector<unsigned char>& tmp)
{
    const int px = static_cast<int>(std::floor(penX));
    const float fx = penX - static_cast<float>(px);
    int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    stbtt_GetCodepointBitmapBoxSubpixel(face.info, static_cast<int>(cp), face.scale, face.scale, fx, 0.0f, &ix0, &iy0,
                                        &ix1, &iy1);
    const int gw = ix1 - ix0, gh = iy1 - iy0;
    if (gw <= 0 || gh <= 0 || gw > 4096 || gh > 4096)
        return;
    tmp.assign(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0);
    stbtt_MakeCodepointBitmapSubpixel(face.info, tmp.data(), gw, gh, gw, face.scale, face.scale, fx, 0.0f,
                                      static_cast<int>(cp));
    for (int y = 0; y < gh; ++y) {
        const int dy = baseline + iy0 + y;
        if (dy < 0 || dy >= dst.h)
            continue;
        for (int x = 0; x < gw; ++x) {
            const int dx = px + ix0 + x;
            if (dx < 0 || dx >= dst.w)
                continue;
            const int g = tmp[static_cast<size_t>(y) * static_cast<size_t>(gw) + static_cast<size_t>(x)];
            if (g == 0)
                continue;
            unsigned char& d = dst.at(dx, dy);
            d = static_cast<unsigned char>(d + g - (d * g + 127) / 255);
            bounds.add(dx, dy);
        }
    }
}

} // namespace

std::vector<char32_t> decodeUtf8(const std::string& text)
{
    std::vector<char32_t> out;
    out.reserve(text.size());
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        char32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n && (text[i + 1] & 0xC0) == 0x80) {
            cp = (static_cast<char32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
            if (cp < 0x80)
                cp = 0xFFFD;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n && (text[i + 1] & 0xC0) == 0x80 && (text[i + 2] & 0xC0) == 0x80) {
            cp = (static_cast<char32_t>(c & 0x0F) << 12) | (static_cast<char32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
            if (cp < 0x800)
                cp = 0xFFFD;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n && (text[i + 1] & 0xC0) == 0x80 && (text[i + 2] & 0xC0) == 0x80 &&
                   (text[i + 3] & 0xC0) == 0x80) {
            cp = (static_cast<char32_t>(c & 0x07) << 18) | (static_cast<char32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 (static_cast<char32_t>(static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            len = 4;
            if (cp < 0x10000 || cp > 0x10FFFF)
                cp = 0xFFFD;
        }
        i += len;
        if (cp == U'\r')
            continue;
        if (cp == U'\t') {
            out.insert(out.end(), 4, U' ');
            continue;
        }
        out.push_back(cp);
    }
    return out;
}

bool rasterizeText(const TextStyle& style, int canvasW, int canvasH, int maxTexture, TextBitmap& out,
                   std::string* error)
{
    out = TextBitmap{};
    canvasW = std::max(1, canvasW);
    canvasH = std::max(1, canvasH);
    maxTexture = std::clamp(maxTexture, 256, 16384);

    const std::shared_ptr<Font> font = loadFont(style.fontPath);
    if (!font) {
        if (error)
            *error = "não foi possível abrir a fonte";
        out.fontMissing = true;
        // Faixa transparente do tamanho do canvas: a fonte continua valida (so sem texto)
        out.width = canvasW;
        out.height = canvasH;
        out.rgba.assign(static_cast<size_t>(canvasW) * static_cast<size_t>(canvasH) * 4, 0);
        return false;
    }

    // --- Layout ---
    Face face;
    face.info = &font->info;
    const float sizePx = std::clamp(style.sizePx, 4.0f, 1500.0f);
    face.scale = stbtt_ScaleForPixelHeight(&font->info, sizePx);
    face.spacing = std::clamp(style.letterSpacing, -50.0f, 200.0f);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&font->info, &asc, &desc, &gap);
    face.ascent = static_cast<float>(asc) * face.scale;
    face.descent = static_cast<float>(desc) * face.scale;
    face.lineGap = static_cast<float>(gap) * face.scale;

    const int margin = std::clamp(style.margin, 0, 2000);
    const bool horizontal = style.marquee == TextMarquee::Horizontal;
    const bool vertical = style.marquee == TextMarquee::Vertical;

    // Folga para contorno/sombra nao serem cortados (e para o letreiro nao ter emenda visivel)
    int effectPad = 2;
    if (style.outline)
        effectPad += static_cast<int>(std::ceil(std::clamp(style.outlineWidth, 0.0f, 100.0f))) + 1;
    if (style.shadow)
        effectPad += std::max(std::abs(style.shadowX), std::abs(style.shadowY)) + 2 * std::clamp(style.shadowBlur, 0, 100);

    const std::vector<char32_t> cps = decodeUtf8(style.text);
    float wrapW = 0.0f; // 0 = sem quebra automatica
    if (style.wrap && !horizontal)
        wrapW = std::max(8.0f, static_cast<float>(canvasW - 2 * margin));
    std::vector<Line> lines = layoutLines(cps, face, wrapW);

    const float glyphH = face.ascent - face.descent;
    const float lineAdv = (glyphH + face.lineGap) * std::clamp(style.lineSpacing, 0.3f, 4.0f);
    float blockW = 0.0f;
    for (const Line& l : lines)
        blockW = std::max(blockW, l.width);
    const float blockH = glyphH + static_cast<float>(lines.size() - 1) * lineAdv;

    // --- Tamanho da faixa e posicao do bloco ---
    int sw = canvasW, sh = canvasH;
    float areaL = static_cast<float>(margin), areaR = static_cast<float>(canvasW - margin);
    float blockX = 0.0f, blockY = 0.0f;
    if (horizontal) {
        const int gapPx = style.marqueeGap > 0 ? style.marqueeGap : canvasW;
        const int wanted = static_cast<int>(std::ceil(blockW)) + 2 * effectPad + gapPx;
        sw = std::clamp(wanted, 1, maxTexture);
        out.truncated = wanted > maxTexture;
        out.period = sw;
        areaL = static_cast<float>(effectPad);
        areaR = areaL + blockW;
        blockX = areaL;
    } else if (vertical) {
        const int gapPx = style.marqueeGap > 0 ? style.marqueeGap : canvasH;
        const int wanted = static_cast<int>(std::ceil(blockH)) + 2 * effectPad + gapPx;
        sh = std::clamp(wanted, 1, maxTexture);
        out.truncated = wanted > maxTexture;
        out.period = sh;
        blockY = static_cast<float>(effectPad);
    }
    if (!vertical) {
        switch (style.vAlign) {
        case TextVAlign::Top: blockY = static_cast<float>(margin); break;
        case TextVAlign::Middle: blockY = (static_cast<float>(sh) - blockH) * 0.5f; break;
        case TextVAlign::Bottom: blockY = static_cast<float>(sh - margin) - blockH; break;
        }
    }
    if (!horizontal) {
        switch (style.hAlign) {
        case TextHAlign::Left: blockX = areaL; break;
        case TextHAlign::Center: blockX = (areaL + areaR - blockW) * 0.5f; break;
        case TextHAlign::Right: blockX = areaR - blockW; break;
        }
    }

    // --- Cobertura do texto ---
    Mask fill;
    fill.reset(sw, sh);
    Bounds bounds;
    std::vector<unsigned char> tmp;
    for (size_t li = 0; li < lines.size(); ++li) {
        const Line& l = lines[li];
        float x0 = areaL;
        switch (style.hAlign) {
        case TextHAlign::Left: x0 = areaL; break;
        case TextHAlign::Center: x0 = areaL + (areaR - areaL - l.width) * 0.5f; break;
        case TextHAlign::Right: x0 = areaR - l.width; break;
        }
        const int baseline = static_cast<int>(std::lround(blockY + face.ascent + static_cast<float>(li) * lineAdv));
        float pen = x0;
        for (size_t i = l.begin; i < l.end; ++i) {
            const char32_t cp = cps[i];
            if (i > l.begin)
                pen += face.kern(cps[i - 1], cp);
            if (cp >= 32 && cp != U' ')
                blitGlyph(face, cp, pen, baseline, fill, bounds, tmp);
            pen += face.advance(cp);
        }
    }

    // --- Efeitos na regiao do texto ---
    int rx0 = 0, ry0 = 0, rx1 = -1, ry1 = -1; // regiao inclusiva em coordenadas da faixa
    if (!bounds.empty()) {
        rx0 = std::max(0, bounds.x0 - effectPad);
        ry0 = std::max(0, bounds.y0 - effectPad);
        rx1 = std::min(sw - 1, bounds.x1 + effectPad);
        ry1 = std::min(sh - 1, bounds.y1 + effectPad);
    }
    const int rw = std::max(0, rx1 - rx0 + 1), rh = std::max(0, ry1 - ry0 + 1);

    Mask region; // cobertura do preenchimento recortada na regiao
    Mask ring;   // contorno (sem o miolo)
    Mask shade;  // sombra
    if (rw > 0 && rh > 0) {
        region.reset(rw, rh);
        for (int y = 0; y < rh; ++y)
            std::copy_n(&fill.v[static_cast<size_t>(ry0 + y) * static_cast<size_t>(sw) + static_cast<size_t>(rx0)],
                        static_cast<size_t>(rw), &region.v[static_cast<size_t>(y) * static_cast<size_t>(rw)]);

        Mask shape = region;
        if (style.outline && style.outlineWidth > 0.0f && style.outlineColor.a > 0.0f) {
            Mask dil = dilate(region, std::clamp(style.outlineWidth, 0.0f, 100.0f));
            ring.reset(rw, rh);
            for (size_t i = 0; i < ring.v.size(); ++i) {
                const int inner = region.v[i];
                ring.v[i] = static_cast<unsigned char>(std::max(0, dil.v[i] - inner));
            }
            shape = std::move(dil);
        }
        if (style.shadow && style.shadowColor.a > 0.0f) {
            shade.reset(rw, rh);
            for (int y = 0; y < rh; ++y)
                for (int x = 0; x < rw; ++x) {
                    const int sx = x - style.shadowX, sy = y - style.shadowY;
                    if (sx >= 0 && sx < rw && sy >= 0 && sy < rh)
                        shade.at(x, y) = shape.at(sx, sy);
                }
            blur(shade, std::clamp(style.shadowBlur, 0, 100));
        }
    }

    // --- Composicao: fundo -> sombra -> contorno -> texto ---
    out.width = sw;
    out.height = sh;
    out.rgba.assign(static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4, 0);

    const float bgA = std::clamp(style.bgColor.a * style.bgOpacity, 0.0f, 1.0f);
    const bool bgOn = style.background && bgA > 0.0f;
    int boxX0 = 0, boxY0 = 0, boxX1 = -1, boxY1 = -1; // caixa do fundo (inclusiva)
    if (bgOn && !style.bgFullScreen && !bounds.empty()) {
        const int pad = std::clamp(style.bgPadding, 0, 2000);
        boxX0 = static_cast<int>(std::floor(blockX)) - pad;
        boxX1 = static_cast<int>(std::ceil(blockX + blockW)) + pad;
        boxY0 = static_cast<int>(std::floor(blockY)) - pad;
        boxY1 = static_cast<int>(std::ceil(blockY + blockH)) + pad;
        if (horizontal) { // a caixa tem de caber no periodo para repetir sem emenda
            boxX0 = std::max(0, boxX0);
            boxX1 = std::min(sw - 1, boxX1);
        }
        if (vertical) {
            boxY0 = std::max(0, boxY0);
            boxY1 = std::min(sh - 1, boxY1);
        }
    }

    const Color fc = style.color;
    const Color oc = style.outlineColor;
    const Color sc = style.shadowColor;
    const bool hasRing = !ring.v.empty();
    const bool hasShade = !shade.v.empty();

    for (int y = 0; y < sh; ++y) {
        unsigned char* row = &out.rgba[static_cast<size_t>(y) * static_cast<size_t>(sw) * 4];
        const bool yInRegion = rh > 0 && y >= ry0 && y <= ry1;
        const bool yInBox = bgOn && !style.bgFullScreen && y >= boxY0 && y <= boxY1;
        for (int x = 0; x < sw; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f; // premultiplicado
            auto over = [&](float lr, float lg, float lb, float la) {
                if (la <= 0.0f)
                    return;
                r = lr * la + r * (1.0f - la);
                g = lg * la + g * (1.0f - la);
                b = lb * la + b * (1.0f - la);
                a = la + a * (1.0f - la);
            };
            if (bgOn && (style.bgFullScreen || (yInBox && x >= boxX0 && x <= boxX1)))
                over(style.bgColor.r, style.bgColor.g, style.bgColor.b, bgA);
            if (yInRegion && x >= rx0 && x <= rx1) {
                const int lx = x - rx0, ly = y - ry0;
                if (hasShade)
                    over(sc.r, sc.g, sc.b, sc.a * static_cast<float>(shade.at(lx, ly)) / 255.0f);
                if (hasRing)
                    over(oc.r, oc.g, oc.b, oc.a * static_cast<float>(ring.at(lx, ly)) / 255.0f);
                over(fc.r, fc.g, fc.b, fc.a * static_cast<float>(region.at(lx, ly)) / 255.0f);
            }
            if (a > 0.0f) {
                unsigned char* px = row + static_cast<size_t>(x) * 4;
                px[0] = static_cast<unsigned char>(std::lround(std::clamp(r / a, 0.0f, 1.0f) * 255.0f));
                px[1] = static_cast<unsigned char>(std::lround(std::clamp(g / a, 0.0f, 1.0f) * 255.0f));
                px[2] = static_cast<unsigned char>(std::lround(std::clamp(b / a, 0.0f, 1.0f) * 255.0f));
                px[3] = static_cast<unsigned char>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
            }
        }
    }
    return true;
}

} // namespace bsl::media
