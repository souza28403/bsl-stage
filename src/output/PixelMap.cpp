#include "output/PixelMap.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace bsl::pixelmap {

double jsRound(double v)
{
    return std::floor(v + 0.5);
}

Scale scaleOf(const Config& c)
{
    Scale s;
    if (c.physW > 0)
        s.x = static_cast<double>(c.inW) / static_cast<double>(c.physW);
    if (c.physH > 0)
        s.y = static_cast<double>(c.inH) / static_cast<double>(c.physH);
    return s;
}

RectD scaledRect(const Config& c, const Panel& p)
{
    const Scale s = scaleOf(c);
    return {p.x * s.x, p.y * s.y, p.w * s.x, p.h * s.y};
}

RectI scaledRectRounded(const Config& c, const Panel& p)
{
    const Scale s = scaleOf(c);
    return {static_cast<int>(jsRound(p.x * s.x)), static_cast<int>(jsRound(p.y * s.y)),
            static_cast<int>(jsRound(p.w * s.x)), static_cast<int>(jsRound(p.h * s.y))};
}

std::uint32_t panelColor(const Panel& p, int index)
{
    if (p.color != 0)
        return p.color & 0xFFFFFFu;
    return kPaletteRgb[((index % 4) + 4) % 4];
}

float panelFill(const Panel& p, int index)
{
    if (p.color != 0) {
        const int k = static_cast<int>(std::find(std::begin(kPaletteRgb), std::end(kPaletteRgb), p.color & 0xFFFFFFu)
                                       - std::begin(kPaletteRgb));
        return k < 4 ? kPaletteFill[k] : 0.2f;
    }
    return kPaletteFill[((index % 4) + 4) % 4];
}

void applyCabinets(Panel& p)
{
    if (!p.byCabinets)
        return;
    p.cols = std::clamp(p.cols, 1, 256);
    p.rows = std::clamp(p.rows, 1, 256);
    p.cabW = std::clamp(p.cabW, 1, kMaxDim);
    p.cabH = std::clamp(p.cabH, 1, kMaxDim);
    p.w = static_cast<int>(std::min<long long>(static_cast<long long>(p.cols) * p.cabW, kMaxDim));
    p.h = static_cast<int>(std::min<long long>(static_cast<long long>(p.rows) * p.cabH, kMaxDim));
}

void normalize(Config& c)
{
    c.inW = std::clamp(c.inW, 1, kMaxDim);
    c.inH = std::clamp(c.inH, 1, kMaxDim);
    c.physW = std::clamp(c.physW, 1, kMaxDim);
    c.physH = std::clamp(c.physH, 1, kMaxDim);
    if (c.panels.size() > static_cast<size_t>(kMaxPanels))
        c.panels.resize(static_cast<size_t>(kMaxPanels));
    int i = 0;
    for (Panel& p : c.panels) {
        ++i;
        if (p.name.empty())
            p.name = "Painel " + std::to_string(i);
        p.x = std::clamp(p.x, -kMaxDim, kMaxDim);
        p.y = std::clamp(p.y, -kMaxDim, kMaxDim);
        p.w = std::clamp(p.w, 1, kMaxDim);
        p.h = std::clamp(p.h, 1, kMaxDim);
        p.cols = std::clamp(p.cols, 1, 256);
        p.rows = std::clamp(p.rows, 1, 256);
        p.cabW = std::clamp(p.cabW, 1, kMaxDim);
        p.cabH = std::clamp(p.cabH, 1, kMaxDim);
        p.color &= 0xFFFFFFu;
        applyCabinets(p);
    }
}

Panel makePanel(const std::string& name, int x, int y, int w, int h)
{
    Panel p;
    p.name = name;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    return p;
}

std::string nextPanelName(const Config& c)
{
    return "Painel " + std::to_string(c.panels.size() + 1);
}

void syncInput(Config& c, int canvasW, int canvasH)
{
    if (!c.linkCanvas)
        return;
    c.inW = std::clamp(canvasW, 1, kMaxDim);
    c.inH = std::clamp(canvasH, 1, kMaxDim);
}

bool isCleanScale(double s)
{
    if (!(s > 0.0))
        return false;
    constexpr double kTol = 1.0e-6;
    if (std::abs(s - 1.0) < kTol)
        return true;
    const double r = s >= 1.0 ? s : 1.0 / s;
    return std::abs(r - std::round(r)) < kTol;
}

Validation validate(const Config& c)
{
    Validation v;
    const size_t n = c.panels.size();
    v.overlap.assign(n, false);
    v.outside.assign(n, false);
    for (size_t i = 0; i < n; ++i) {
        const Panel& a = c.panels[i];
        if (a.w <= 0 || a.h <= 0 || a.x < 0 || a.y < 0 || a.x + a.w > c.physW || a.y + a.h > c.physH)
            v.outside[i] = true;
        for (size_t j = i + 1; j < n; ++j) {
            const Panel& b = c.panels[j];
            const bool inter = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
            if (inter) {
                v.overlap[i] = true;
                v.overlap[j] = true;
            }
        }
    }
    v.overlapCount = static_cast<int>(std::count(v.overlap.begin(), v.overlap.end(), true));
    v.outsideCount = static_cast<int>(std::count(v.outside.begin(), v.outside.end(), true));
    const Scale s = scaleOf(c);
    v.scaleWarn = !isCleanScale(s.x) || !isCleanScale(s.y);
    return v;
}

namespace {

void csvField(std::string& out, const std::string& f)
{
    const bool quote = f.find_first_of(";,\"\r\n\t") != std::string::npos || (!f.empty() && (f.front() == ' ' || f.back() == ' '));
    if (!quote) {
        out += f;
        return;
    }
    out += '"';
    for (const char ch : f) {
        if (ch == '"')
            out += '"';
        out += ch;
    }
    out += '"';
}

std::vector<std::vector<std::string>> csvRows(std::string_view text, char delim)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool any = false;
    auto endField = [&]() {
        row.push_back(field);
        field.clear();
    };
    auto endRow = [&]() {
        endField();
        if (any)
            rows.push_back(std::move(row));
        row.clear();
        any = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
            continue;
        }
        if (ch == '"') {
            quoted = true;
            any = true;
        } else if (ch == delim) {
            endField();
            any = true;
        } else if (ch == '\r') {
            if (i + 1 >= text.size() || text[i + 1] != '\n')
                endRow();
        } else if (ch == '\n') {
            endRow();
        } else {
            field += ch;
            if (ch != ' ' && ch != '\t')
                any = true;
        }
    }
    endRow();
    return rows;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
        --b;
    return s.substr(a, b - a);
}

bool parseNumber(const std::string& raw, double& out)
{
    std::string s = trim(raw);
    if (s.empty())
        return false;
    std::replace(s.begin(), s.end(), ',', '.');
    const char* b = s.data();
    const char* e = s.data() + s.size();
    const auto r = std::from_chars(b, e, out);
    return r.ec == std::errc() && r.ptr == e && std::isfinite(out);
}

}

std::string toCsv(const Config& c)
{
    std::string out = "\xEF\xBB\xBF";
    out += "nome;fisico_x;fisico_y;fisico_w;fisico_h;comp_x;comp_y;comp_w;comp_h\r\n";
    for (const Panel& p : c.panels) {
        const RectI r = scaledRectRounded(c, p);
        csvField(out, p.name);
        for (const int v : {p.x, p.y, p.w, p.h, r.x, r.y, r.w, r.h}) {
            out += ';';
            out += std::to_string(v);
        }
        out += "\r\n";
    }
    return out;
}

bool fromCsv(const std::string& textIn, std::vector<Panel>& out, std::string* error)
{
    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg;
        return false;
    };
    std::string_view text(textIn);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    size_t lineEnd = text.find('\n');
    const std::string_view first = text.substr(0, lineEnd == std::string_view::npos ? text.size() : lineEnd);
    char delim = ';';
    size_t best = 0;
    for (const char d : {';', ',', '\t'}) {
        const size_t n = static_cast<size_t>(std::count(first.begin(), first.end(), d));
        if (n > best) {
            best = n;
            delim = d;
        }
    }

    const auto rows = csvRows(text, delim);
    std::vector<Panel> panels;
    int line = 0;
    for (const auto& row : rows) {
        ++line;
        if (row.size() < 5)
            return fail("linha " + std::to_string(line) + ": esperado nome e 4 números (x, y, largura, altura)");
        double v[4];
        bool numeric = true;
        for (int k = 0; k < 4; ++k)
            numeric = numeric && parseNumber(row[static_cast<size_t>(k) + 1], v[k]);
        if (!numeric) {
            if (line == 1)
                continue;
            return fail("linha " + std::to_string(line) + ": valores numéricos inválidos");
        }
        if (v[2] < 1.0 || v[3] < 1.0 || v[2] > kMaxDim || v[3] > kMaxDim || std::abs(v[0]) > kMaxDim || std::abs(v[1]) > kMaxDim)
            return fail("linha " + std::to_string(line) + ": tamanho ou posição fora dos limites");
        Panel p;
        p.name = trim(row[0]);
        if (p.name.empty())
            p.name = "Painel " + std::to_string(panels.size() + 1);
        p.x = static_cast<int>(jsRound(v[0]));
        p.y = static_cast<int>(jsRound(v[1]));
        p.w = static_cast<int>(jsRound(v[2]));
        p.h = static_cast<int>(jsRound(v[3]));
        panels.push_back(std::move(p));
        if (panels.size() > static_cast<size_t>(kMaxPanels))
            return fail("painéis demais (máximo " + std::to_string(kMaxPanels) + ")");
    }
    if (panels.empty())
        return fail("nenhum painel no arquivo");
    out = std::move(panels);
    return true;
}

std::string sanitizeFileName(const std::string& name)
{
    std::string s;
    for (const char ch : name) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u < 0x20 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos)
            s += '_';
        else
            s += ch;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
        s.pop_back();
    return s.empty() ? std::string("painel") : s;
}

std::string patternFileName(const Panel& p)
{
    return "BSLLight_Pattern_" + sanitizeFileName(p.name) + "_" + std::to_string(p.w) + "x" + std::to_string(p.h) + ".png";
}

std::string upperUtf8(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            unsigned up = cp;
            if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
                up = cp - 0x20;
            out += static_cast<char>(0xC0 | (up >> 6));
            out += static_cast<char>(0x80 | (up & 0x3F));
            i += 2;
        } else {
            out += static_cast<char>(c);
            ++i;
        }
    }
    return out;
}

}
