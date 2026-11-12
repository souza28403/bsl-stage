#include "output/NovaScr.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace bsl::novascr {

namespace {

constexpr std::size_t kTypeOffset = 0x13A;
constexpr std::size_t kVersionOffset = 0x13F;
constexpr std::size_t kMinSize = 0x145;
constexpr std::size_t kV1Cols = 0x144;
constexpr std::size_t kV1Rows = 0x146;
constexpr std::size_t kV1Data = 0x14A;
constexpr std::size_t kV1Unit = 17;
constexpr std::size_t kV2Count = 0x140;
constexpr std::size_t kV2Data = 0x145;
constexpr std::size_t kV2Unit = 16;

int be16(const std::uint8_t* p)
{
    return (p[0] << 8) | p[1];
}

int le16(const std::uint8_t* p)
{
    return p[0] | (p[1] << 8);
}

Result fail(Result r, const std::string& msg)
{
    r.ok = false;
    r.error = msg;
    r.cabinets.clear();
    return r;
}

}

Result parse(const std::uint8_t* data, std::size_t size)
{
    Result r;
    if (data == nullptr || size < 4 || data[0] != 'D' || data[1] != 'S' || data[2] != 'C' || data[3] != 'I')
        return fail(r, "Arquivo inválido: não é um mapa de tela .scr do NovaLCT (assinatura DSCI ausente).");
    if (size < kMinSize)
        return fail(r, "Arquivo .scr truncado: faltam os dados dos gabinetes.");
    if (data[kTypeOffset] != 1)
        return fail(r, "Arquivo .scr sem bloco de gabinetes (tipo " + std::to_string(data[kTypeOffset]) + ").");

    r.version = data[kVersionOffset];
    if (r.version == 1) {
        if (size < kV1Rows + 2)
            return fail(r, "Arquivo .scr truncado: faltam os dados dos gabinetes.");
        r.cols = be16(data + kV1Cols);
        r.rows = be16(data + kV1Rows);
        if (r.cols <= 0 || r.rows <= 0)
            return fail(r, "Arquivo .scr sem gabinetes (colunas ou linhas zeradas).");
        const unsigned long long n = static_cast<unsigned long long>(r.cols) * static_cast<unsigned long long>(r.rows);
        if (n > static_cast<unsigned long long>(kMaxCabinets))
            return fail(r, "Quantidade de gabinetes absurda no arquivo .scr (" + std::to_string(n) + ").");
        if (kV1Data + n * kV1Unit > size)
            return fail(r, "Arquivo .scr truncado: esperados " + std::to_string(n) + " gabinetes, o arquivo termina antes.");
        r.cabinets.reserve(static_cast<std::size_t>(n));
        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
            const std::uint8_t* b = data + kV1Data + i * kV1Unit;
            Cabinet c;
            c.order = b[1];
            c.x = le16(b + 3);
            c.y = le16(b + 5);
            c.col = b[7];
            c.row = b[9];
            c.w = be16(b + 10);
            c.h = be16(b + 15);
            c.port = 0;
            r.cabinets.push_back(c);
        }
        r.warnings.push_back("Este formato (v1) não traz a porta da controladora: todos os gabinetes ficam na porta 0.");
    } else if (r.version == 2) {
        const std::size_t n = static_cast<std::size_t>(be16(data + kV2Count));
        if (n == 0)
            return fail(r, "Arquivo .scr sem gabinetes.");
        if (n > static_cast<std::size_t>(kMaxCabinets))
            return fail(r, "Quantidade de gabinetes absurda no arquivo .scr (" + std::to_string(n) + ").");
        if (kV2Data + n * kV2Unit > size)
            return fail(r, "Arquivo .scr truncado: esperados " + std::to_string(n) + " gabinetes, o arquivo termina antes.");
        r.hasPorts = true;
        r.cabinets.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t* b = data + kV2Data + i * kV2Unit;
            Cabinet c;
            c.port = b[1];
            c.order = b[2];
            c.x = le16(b + 4);
            c.y = le16(b + 6);
            c.w = le16(b + 12);
            c.h = le16(b + 14);
            r.cabinets.push_back(c);
        }
    } else {
        return fail(r, "Versão " + std::to_string(r.version) + " do arquivo .scr não é suportada (só 1 e 2).");
    }

    for (std::size_t i = 0; i < r.cabinets.size(); ++i) {
        const Cabinet& c = r.cabinets[i];
        if (c.w <= 0 || c.h <= 0)
            return fail(r, "Gabinete " + std::to_string(i + 1) + " com tamanho zero no arquivo .scr.");
        r.width = std::max(r.width, c.x + c.w);
        r.height = std::max(r.height, c.y + c.h);
    }

    int overlaps = 0;
    for (std::size_t i = 0; i < r.cabinets.size(); ++i)
        for (std::size_t j = i + 1; j < r.cabinets.size(); ++j) {
            const Cabinet& a = r.cabinets[i];
            const Cabinet& b = r.cabinets[j];
            if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h)
                ++overlaps;
        }
    if (overlaps > 0)
        r.warnings.push_back(std::to_string(overlaps) + " par(es) de gabinetes se sobrepõem, como no arquivo original.");

    r.ok = true;
    return r;
}

Result parse(const std::vector<std::uint8_t>& bytes)
{
    return parse(bytes.data(), bytes.size());
}

Result parseFile(const std::string& utf8Path)
{
    const std::filesystem::path p(std::u8string(utf8Path.begin(), utf8Path.end()));
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        Result r;
        r.error = "Não foi possível abrir o arquivo .scr.";
        return r;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(bytes);
}

int portCount(const Result& r)
{
    std::set<int> ports;
    for (const Cabinet& c : r.cabinets)
        ports.insert(c.port);
    return static_cast<int>(ports.size());
}

bool applyToPixelMap(const Result& r, pixelmap::Config& cfg, ImportMode mode, std::string* error)
{
    auto failMsg = [&](const std::string& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (!r.ok || r.cabinets.empty())
        return failMsg("Não há mapa válido para importar.");

    int offsetX = 0;
    std::size_t existing = 0;
    if (mode == ImportMode::AddBeside) {
        existing = cfg.panels.size();
        for (const pixelmap::Panel& p : cfg.panels)
            offsetX = std::max(offsetX, p.x + p.w);
    }
    if (existing + r.cabinets.size() > static_cast<std::size_t>(pixelmap::kMaxPanels))
        return failMsg("Painéis demais: o Pixel Map aceita no máximo " + std::to_string(pixelmap::kMaxPanels) + ".");
    if (static_cast<long long>(offsetX) + r.width > pixelmap::kMaxDim || r.height > pixelmap::kMaxDim)
        return failMsg("O mapa passa do limite de " + std::to_string(pixelmap::kMaxDim) + " px do Pixel Map.");

    std::vector<pixelmap::Panel> added;
    added.reserve(r.cabinets.size());
    for (const Cabinet& c : r.cabinets) {
        std::string name = "Gab " + std::to_string(c.order + 1);
        if (r.hasPorts)
            name = "P" + std::to_string(c.port + 1) + " " + name;
        pixelmap::Panel p = pixelmap::makePanel(name, c.x + offsetX, c.y, c.w, c.h);
        p.port = c.port;
        p.cableOrder = c.order;
        added.push_back(std::move(p));
    }

    if (mode == ImportMode::Replace) {
        cfg.panels = std::move(added);
        cfg.physW = r.width;
        cfg.physH = r.height;
    } else {
        for (pixelmap::Panel& p : added)
            cfg.panels.push_back(std::move(p));
        cfg.physW = std::max(cfg.physW, offsetX + r.width);
        cfg.physH = std::max(cfg.physH, r.height);
    }
    pixelmap::normalize(cfg);
    return true;
}

}
