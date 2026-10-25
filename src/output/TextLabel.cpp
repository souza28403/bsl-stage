#include "output/TextLabel.h"

#include "app/Paths.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>

namespace bsl {

namespace {

struct Font
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

const Font& font()
{
    static Font f;
    static std::once_flag once;
    std::call_once(once, [] {
        const auto dir = paths::assetsDir();
        if (dir.empty())
            return;
        std::ifstream file(dir / L"fonts" / L"Barlow-Bold.ttf", std::ios::binary);
        if (!file)
            return;
        f.data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        f.ok = !f.data.empty() && stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0)) != 0;
    });
    return f;
}

std::vector<int> decodeUtf8(const std::string& s)
{
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

// Linha de texto como máscara de alpha (8 bits)
struct Line
{
    int width = 0;
    int height = 0;
    int baseline = 0;
    std::vector<unsigned char> alpha;
};

Line renderLine(const Font& f, const std::string& text, float px)
{
    Line line;
    const float scale = stbtt_ScaleForPixelHeight(&f.info, px);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &gap);
    line.baseline = static_cast<int>(std::ceil(static_cast<float>(ascent) * scale));
    line.height = static_cast<int>(std::ceil(static_cast<float>(ascent - descent) * scale)) + 2;

    const std::vector<int> cps = decodeUtf8(text);
    float width = 0.0f;
    for (size_t i = 0; i < cps.size(); ++i) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, cps[i], &adv, &lsb);
        width += static_cast<float>(adv) * scale;
        if (i + 1 < cps.size())
            width += static_cast<float>(stbtt_GetCodepointKernAdvance(&f.info, cps[i], cps[i + 1])) * scale;
    }
    line.width = static_cast<int>(std::ceil(width)) + 2;
    line.alpha.assign(static_cast<size_t>(line.width) * static_cast<size_t>(line.height), 0);

    float x = 1.0f;
    for (size_t i = 0; i < cps.size(); ++i) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, cps[i], &adv, &lsb);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&f.info, cps[i], scale, scale, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> glyph(static_cast<size_t>(gw) * static_cast<size_t>(gh));
            stbtt_MakeCodepointBitmap(&f.info, glyph.data(), gw, gh, gw, scale, scale, cps[i]);
            for (int gy = 0; gy < gh; ++gy) {
                const int dy = line.baseline + y0 + gy;
                if (dy < 0 || dy >= line.height)
                    continue;
                for (int gx = 0; gx < gw; ++gx) {
                    const int dx = static_cast<int>(x) + x0 + gx;
                    if (dx < 0 || dx >= line.width)
                        continue;
                    unsigned char& dst = line.alpha[static_cast<size_t>(dy) * static_cast<size_t>(line.width) + static_cast<size_t>(dx)];
                    dst = std::max(dst, glyph[static_cast<size_t>(gy) * static_cast<size_t>(gw) + static_cast<size_t>(gx)]);
                }
            }
        }
        x += static_cast<float>(adv) * scale;
        if (i + 1 < cps.size())
            x += static_cast<float>(stbtt_GetCodepointKernAdvance(&f.info, cps[i], cps[i + 1])) * scale;
    }
    return line;
}

} // namespace

LabelBitmap renderLabel(const std::string& line1, float pixelHeight1, const std::string& line2, float pixelHeight2)
{
    const Font& f = font();
    LabelBitmap out;
    if (!f.ok)
        return out;

    const Line a = renderLine(f, line1, pixelHeight1);
    const Line b = line2.empty() ? Line{} : renderLine(f, line2, pixelHeight2);

    const int pad = std::max(6, static_cast<int>(pixelHeight1 * 0.4f));
    const int gap = b.height > 0 ? pad / 2 : 0;
    out.width = std::max(a.width, b.width) + pad * 2;
    out.height = a.height + b.height + gap + pad * 2;
    out.rgba.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4, 0);

    // Placa: preto a 70%
    for (size_t i = 0; i < out.rgba.size(); i += 4)
        out.rgba[i + 3] = 178;

    auto blit = [&](const Line& l, int top) {
        if (l.height <= 0)
            return;
        const int left = (out.width - l.width) / 2;
        for (int y = 0; y < l.height; ++y) {
            for (int x = 0; x < l.width; ++x) {
                const unsigned char cov = l.alpha[static_cast<size_t>(y) * static_cast<size_t>(l.width) + static_cast<size_t>(x)];
                if (!cov)
                    continue;
                unsigned char* px = &out.rgba[(static_cast<size_t>(top + y) * static_cast<size_t>(out.width) + static_cast<size_t>(left + x)) * 4];
                // mistura branco sobre a placa (cor) e sobe o alpha
                for (int c = 0; c < 3; ++c)
                    px[c] = static_cast<unsigned char>((255 * cov + px[c] * (255 - cov)) / 255);
                px[3] = static_cast<unsigned char>(std::max<int>(px[3], cov));
            }
        }
    };
    blit(a, pad);
    blit(b, pad + a.height + gap);
    return out;
}

} // namespace bsl
