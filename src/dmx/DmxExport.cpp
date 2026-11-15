#include "dmx/DmxExport.h"
#include "platform/TimeUtil.h"

#include "core/Layer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

namespace bsl::dmx {

namespace {

constexpr const char* kManufacturer = "BSL Light";
constexpr const char* kLayerGuid = "5B6C4B8E-2D8A-4C1E-9A44-0B5A57A0E101";
constexpr const char* kMasterGuid = "5B6C4B8E-2D8A-4C1E-9A44-0B5A57A0E102";

constexpr const char* kBlendNamesAscii[kDmxBlendModeCount] = {"Normal", "Add",   "Subtract", "Multiply", "Screen",
                                                           "Overlay", "Lighten", "Darken", "Difference"};

std::string xmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string csvCell(const std::string& s)
{
    if (s.find_first_of(";\"\n\r") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + "\"";
}

std::string num(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string isoNow()
{
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    bslplat::localTime(t, local);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &local);
    return buf;
}

struct AttrInfo
{
    std::string pretty;
    std::string feature;
};
AttrInfo attributeInfo(const std::string& key)
{
    if (key == "Dimmer")
        return {"Dim", "Dimmer.Dimmer"};
    if (key == "BSL_Clip")
        return {"Clip", "BSL.Layer"};
    if (key == "BSL_Speed")
        return {"Speed", "BSL.Layer"};
    if (key == "BSL_Blend")
        return {"Blend", "BSL.Layer"};
    if (key == "BSL_PosX")
        return {"Pos X", "BSL.Transform"};
    if (key == "BSL_PosY")
        return {"Pos Y", "BSL.Transform"};
    if (key == "BSL_Scale")
        return {"Scale", "BSL.Transform"};
    if (key == "BSL_Rotation")
        return {"Rotation", "BSL.Transform"};
    if (key == "BSL_Blackout")
        return {"Blackout", "BSL.Master"};
    if (key == "BSL_Column")
        return {"Column", "BSL.Master"};
    if (key.rfind("BSL_FX", 0) == 0)
        return {"FX " + key.substr(6), "BSL.FX"};
    return {key, "BSL.Layer"};
}

struct Physical
{
    double from = 0.0, to = 1.0;
};
Physical physicalOf(Func f)
{
    switch (f) {
    case Func::Speed:
    case Func::Scale: return {0.0, 4.0};
    case Func::PosX:
    case Func::PosY: return {-1.0, 1.0};
    case Func::Rotation: return {-180.0, 180.0};
    default: return {0.0, 1.0};
    }
}

std::string dmxValue(int value, int bytes)
{
    return std::to_string(value) + "/" + std::to_string(bytes);
}

std::string channelSets(const ChannelRow& row, int bytes)
{
    std::string out;
    auto set = [&](const std::string& name, int from) {
        out += "              <ChannelSet Name=\"" + xmlEscape(name) + "\" DMXFrom=\"" + dmxValue(from, bytes) +
               "\" PhysicalFrom=\"0\" PhysicalTo=\"1\"/>\n";
    };
    switch (row.func) {
    case Func::Clip:
        for (const ClipRange& r : clipRanges())
            set(r.key, r.from);
        break;
    case Func::Column:
        for (const ClipRange& r : clipRanges()) {
            std::string name = r.key == "Clear" ? "Clear all" : r.key;
            set(name, r.from);
        }
        break;
    case Func::Blend:
        for (int i = 0; i < kDmxBlendModeCount; ++i)
            set(kBlendNamesAscii[i], i * 28);
        break;
    case Func::Blackout:
        set("Normal", 0);
        set("Blackout", 128);
        break;
    default: break;
    }
    return out;
}

}

std::vector<FixtureType> fixtureTypes(const Config& cfg)
{
    std::vector<FixtureType> types;

    FixtureType layer;
    layer.name = "BSL Stage Layer";
    layer.shortName = "BSLLayer";
    layer.id = kLayerGuid;
    layer.description = "Uma camada do BSL Stage: opacidade, clip por faixa de valor, velocidade, blend, transform e efeitos";
    std::set<std::pair<int, int>> wanted;
    for (int m = 0; m < kLayerModeCount; ++m)
        wanted.insert({m, 0});
    for (const Block& b : cfg.blocks)
        if (b.kind == BlockKind::Layer)
            wanted.insert({static_cast<int>(b.mode), static_cast<int>(std::min<size_t>(b.fx.size(), kMaxFxSlots))});
    for (const auto& [m, fx] : wanted) {
        FixtureMode mode;
        mode.mode = static_cast<LayerMode>(m);
        mode.fxCount = fx;
        mode.name = personalityName(mode.mode, fx);
        mode.rows = layerRows(mode.mode, fx);
        mode.channels = layerChannelCount(mode.mode, fx);
        layer.modes.push_back(std::move(mode));
    }
    types.push_back(std::move(layer));

    FixtureType master;
    master.name = "BSL Stage Master";
    master.shortName = "BSLMaster";
    master.id = kMasterGuid;
    master.master = true;
    master.description = "O master do BSL Stage: opacidade, blackout e disparo de coluna global";
    FixtureMode mm;
    mm.name = "Master";
    mm.master = true;
    mm.rows = masterRows();
    mm.channels = masterChannelCount();
    master.modes.push_back(std::move(mm));
    types.push_back(std::move(master));
    return types;
}

std::string gdtfFileName(const FixtureType& type)
{
    return std::string(kManufacturer) + "@" + type.name + "@v1.gdtf";
}

std::string gdtfDescription(const FixtureType& type, const std::string& isoDate)
{
    std::vector<std::string> attrs;
    for (const FixtureMode& m : type.modes)
        for (const ChannelRow& r : m.rows)
            if (std::find(attrs.begin(), attrs.end(), r.key) == attrs.end())
                attrs.push_back(r.key);

    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    x += "<GDTF DataVersion=\"1.2\">\n";
    x += "  <FixtureType Name=\"" + xmlEscape(type.name) + "\" ShortName=\"" + xmlEscape(type.shortName) + "\" LongName=\"" +
         xmlEscape(type.name) + "\" Manufacturer=\"" + kManufacturer + "\" Description=\"" + xmlEscape(type.description) +
         "\" FixtureTypeID=\"" + type.id + "\" RefFT=\"\" CanHaveChildren=\"No\">\n";

    x += "    <AttributeDefinitions>\n";
    x += "      <ActivationGroups/>\n";
    x += "      <FeatureGroups>\n";
    x += "        <FeatureGroup Name=\"Dimmer\" Pretty=\"Dimmer\">\n          <Feature Name=\"Dimmer\"/>\n        </FeatureGroup>\n";
    x += "        <FeatureGroup Name=\"BSL\" Pretty=\"BSL Stage\">\n";
    x += "          <Feature Name=\"Layer\"/>\n          <Feature Name=\"Transform\"/>\n          <Feature Name=\"FX\"/>\n"
         "          <Feature Name=\"Master\"/>\n        </FeatureGroup>\n";
    x += "      </FeatureGroups>\n";
    x += "      <Attributes>\n";
    for (const std::string& a : attrs) {
        const AttrInfo info = attributeInfo(a);
        x += "        <Attribute Name=\"" + xmlEscape(a) + "\" Pretty=\"" + xmlEscape(info.pretty) + "\" Feature=\"" +
             xmlEscape(info.feature) + "\"/>\n";
    }
    x += "      </Attributes>\n";
    x += "    </AttributeDefinitions>\n";
    x += "    <Wheels/>\n";
    x += "    <PhysicalDescriptions/>\n";
    x += "    <Models>\n      <Model Name=\"Body\" Length=\"0.1\" Width=\"0.1\" Height=\"0.1\" PrimitiveType=\"Cube\"/>\n    </Models>\n";
    x += "    <Geometries>\n      <Geometry Name=\"Body\" Model=\"Body\"/>\n    </Geometries>\n";

    x += "    <DMXModes>\n";
    for (const FixtureMode& m : type.modes) {
        x += "      <DMXMode Name=\"" + xmlEscape(m.name) + "\" Geometry=\"Body\">\n";
        x += "        <DMXChannels>\n";
        for (const ChannelRow& r : m.rows) {
            const int bytes = r.span;
            const int maxRaw = bytes == 2 ? 65535 : 255;
            std::string offset = std::to_string(r.offset);
            if (bytes == 2)
                offset += "," + std::to_string(r.offset + 1);
            const Physical ph = physicalOf(r.func);
            x += "          <DMXChannel DMXBreak=\"1\" Offset=\"" + offset + "\" Highlight=\"None\" Geometry=\"Body\">\n";
            x += "            <LogicalChannel Attribute=\"" + xmlEscape(r.key) + "\" Snap=\"No\" Master=\"None\" MibFade=\"0\" "
                 "DMXChangeTimeLimit=\"0\">\n";
            x += "              <ChannelFunction Name=\"" + xmlEscape(r.key) + "\" Attribute=\"" + xmlEscape(r.key) +
                 "\" OriginalAttribute=\"\" DMXFrom=\"" + dmxValue(0, bytes) + "\" Default=\"" +
                 dmxValue(std::clamp(r.defaultRaw, 0, maxRaw), bytes) + "\" PhysicalFrom=\"" + num(ph.from) +
                 "\" PhysicalTo=\"" + num(ph.to) + "\" RealFade=\"0\" RealAcceleration=\"0\">\n";
            x += channelSets(r, bytes);
            x += "              </ChannelFunction>\n";
            x += "            </LogicalChannel>\n";
            x += "          </DMXChannel>\n";
        }
        x += "        </DMXChannels>\n";
        x += "        <Relations/>\n";
        x += "        <FTMacros/>\n";
        x += "      </DMXMode>\n";
    }
    x += "    </DMXModes>\n";
    x += "    <Revisions>\n      <Revision Text=\"Gerado pelo BSL Stage\" Date=\"" + xmlEscape(isoDate) +
         "\" UserID=\"0\" ModifiedBy=\"BSL Stage\"/>\n    </Revisions>\n";
    x += "    <Protocols/>\n";
    x += "  </FixtureType>\n";
    x += "</GDTF>\n";
    return x;
}

std::vector<std::uint8_t> gdtfArchive(const FixtureType& type, const std::string& isoDate)
{
    const std::string xml = gdtfDescription(type, isoDate);
    ZipFile f;
    f.name = "description.xml";
    f.data.assign(xml.begin(), xml.end());
    return zipStore({f});
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

namespace {

void put16le(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
}
void put32le(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    put16le(v, x & 0xFFFF);
    put16le(v, x >> 16);
}
std::uint32_t get16le(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8));
}
std::uint32_t get32le(const std::uint8_t* p)
{
    return get16le(p) | (get16le(p + 2) << 16);
}

}

std::vector<std::uint8_t> zipStore(const std::vector<ZipFile>& files)
{
    std::vector<std::uint8_t> out;
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
    bslplat::localTime(t, lt);
    const std::uint32_t dosTime = static_cast<std::uint32_t>((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));
    const std::uint32_t dosDate = static_cast<std::uint32_t>(((std::max(lt.tm_year, 80) - 80) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);

    struct Entry
    {
        std::uint32_t offset, crc, size;
        const ZipFile* file;
    };
    std::vector<Entry> entries;
    for (const ZipFile& f : files) {
        Entry e{static_cast<std::uint32_t>(out.size()), crc32(f.data.data(), f.data.size()),
                static_cast<std::uint32_t>(f.data.size()), &f};
        put32le(out, 0x04034B50u);
        put16le(out, 20);
        put16le(out, 0x0800);
        put16le(out, 0);
        put16le(out, dosTime);
        put16le(out, dosDate);
        put32le(out, e.crc);
        put32le(out, e.size);
        put32le(out, e.size);
        put16le(out, static_cast<std::uint32_t>(f.name.size()));
        put16le(out, 0);
        out.insert(out.end(), f.name.begin(), f.name.end());
        out.insert(out.end(), f.data.begin(), f.data.end());
        entries.push_back(e);
    }
    const std::uint32_t centralStart = static_cast<std::uint32_t>(out.size());
    for (const Entry& e : entries) {
        put32le(out, 0x02014B50u);
        put16le(out, 20);
        put16le(out, 20);
        put16le(out, 0x0800);
        put16le(out, 0);
        put16le(out, dosTime);
        put16le(out, dosDate);
        put32le(out, e.crc);
        put32le(out, e.size);
        put32le(out, e.size);
        put16le(out, static_cast<std::uint32_t>(e.file->name.size()));
        put16le(out, 0);
        put16le(out, 0);
        put16le(out, 0);
        put16le(out, 0);
        put32le(out, 0);
        put32le(out, e.offset);
        out.insert(out.end(), e.file->name.begin(), e.file->name.end());
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;
    put32le(out, 0x06054B50u);
    put16le(out, 0);
    put16le(out, 0);
    put16le(out, static_cast<std::uint32_t>(entries.size()));
    put16le(out, static_cast<std::uint32_t>(entries.size()));
    put32le(out, centralSize);
    put32le(out, centralStart);
    put16le(out, 0);
    return out;
}

bool zipRead(const std::vector<std::uint8_t>& b, std::vector<ZipFile>& files, std::string* error)
{
    auto fail = [&](const char* msg) {
        if (error)
            *error = msg;
        return false;
    };
    files.clear();
    if (b.size() < 22)
        return fail("arquivo pequeno demais");
    const std::size_t eocd = b.size() - 22;
    if (get32le(&b[eocd]) != 0x06054B50u)
        return fail("sem o registro de fim do diretorio central");
    const std::uint32_t count = get16le(&b[eocd + 10]);
    const std::uint32_t centralSize = get32le(&b[eocd + 12]);
    const std::uint32_t centralOffset = get32le(&b[eocd + 16]);
    if (static_cast<std::size_t>(centralOffset) + centralSize > eocd)
        return fail("diretorio central fora do arquivo");
    std::size_t p = centralOffset;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (p + 46 > eocd || get32le(&b[p]) != 0x02014B50u)
            return fail("entrada do diretorio central invalida");
        const std::uint32_t method = get16le(&b[p + 10]);
        const std::uint32_t crc = get32le(&b[p + 16]);
        const std::uint32_t csize = get32le(&b[p + 20]);
        const std::uint32_t usize = get32le(&b[p + 24]);
        const std::uint32_t nameLen = get16le(&b[p + 28]);
        const std::uint32_t extraLen = get16le(&b[p + 30]);
        const std::uint32_t commentLen = get16le(&b[p + 32]);
        const std::uint32_t local = get32le(&b[p + 42]);
        if (p + 46 + nameLen > eocd)
            return fail("nome fora do arquivo");
        ZipFile f;
        f.name.assign(reinterpret_cast<const char*>(&b[p + 46]), nameLen);
        if (method != 0 || csize != usize)
            return fail("so entradas sem compressao");
        if (static_cast<std::size_t>(local) + 30 > b.size() || get32le(&b[local]) != 0x04034B50u)
            return fail("cabecalho local invalido");
        const std::size_t dataStart = static_cast<std::size_t>(local) + 30 + get16le(&b[local + 26]) + get16le(&b[local + 28]);
        if (dataStart + usize > b.size())
            return fail("dados fora do arquivo");
        f.data.assign(b.begin() + static_cast<std::ptrdiff_t>(dataStart), b.begin() + static_cast<std::ptrdiff_t>(dataStart + usize));
        if (crc32(f.data.data(), f.data.size()) != crc)
            return fail("CRC32 nao confere");
        files.push_back(std::move(f));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool xmlWellFormed(const std::string& xml, std::string* error)
{
    std::size_t i = 0;
    std::vector<std::string> stack;
    bool rootDone = false;
    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg + " (posição " + std::to_string(i) + ")";
        return false;
    };
    auto nameStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':'; };
    auto nameChar = [&](char c) { return nameStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.'; };
    auto readName = [&](std::string& out) {
        out.clear();
        if (i >= xml.size() || !nameStart(xml[i]))
            return false;
        while (i < xml.size() && nameChar(xml[i]))
            out += xml[i++];
        return true;
    };
    auto skipSpace = [&] {
        while (i < xml.size() && std::isspace(static_cast<unsigned char>(xml[i])))
            ++i;
    };
    auto checkEntities = [&](const std::string& text) {
        for (std::size_t k = 0; k < text.size(); ++k) {
            if (text[k] != '&')
                continue;
            const std::size_t semi = text.find(';', k);
            if (semi == std::string::npos)
                return false;
            const std::string ent = text.substr(k + 1, semi - k - 1);
            if (ent != "amp" && ent != "lt" && ent != "gt" && ent != "quot" && ent != "apos" &&
                !(ent.size() > 1 && ent[0] == '#'))
                return false;
            k = semi;
        }
        return true;
    };

    if (xml.rfind("\xEF\xBB\xBF", 0) == 0)
        i = 3;
    while (i < xml.size()) {
        if (xml[i] != '<') {
            const std::size_t end = xml.find('<', i);
            const std::string text = xml.substr(i, end == std::string::npos ? std::string::npos : end - i);
            if (stack.empty() && text.find_first_not_of(" \t\r\n") != std::string::npos)
                return fail("texto fora do elemento raiz");
            if (!checkEntities(text))
                return fail("entidade inválida");
            i = end == std::string::npos ? xml.size() : end;
            continue;
        }
        if (xml.compare(i, 4, "<!--") == 0) {
            const std::size_t end = xml.find("-->", i + 4);
            if (end == std::string::npos)
                return fail("comentário sem fim");
            i = end + 3;
            continue;
        }
        if (xml.compare(i, 2, "<?") == 0) {
            const std::size_t end = xml.find("?>", i + 2);
            if (end == std::string::npos)
                return fail("instrução sem fim");
            i = end + 2;
            continue;
        }
        if (xml.compare(i, 2, "</") == 0) {
            i += 2;
            std::string name;
            if (!readName(name))
                return fail("nome de fechamento inválido");
            skipSpace();
            if (i >= xml.size() || xml[i] != '>')
                return fail("fechamento sem '>'");
            ++i;
            if (stack.empty() || stack.back() != name)
                return fail("tag de fechamento </" + name + "> sem par");
            stack.pop_back();
            if (stack.empty())
                rootDone = true;
            continue;
        }
        ++i;
        std::string name;
        if (!readName(name))
            return fail("nome de elemento inválido");
        if (stack.empty() && rootDone)
            return fail("mais de um elemento raiz");
        std::set<std::string> seen;
        bool selfClose = false;
        while (true) {
            skipSpace();
            if (i >= xml.size())
                return fail("elemento sem fim");
            if (xml[i] == '>') {
                ++i;
                break;
            }
            if (xml[i] == '/' && i + 1 < xml.size() && xml[i + 1] == '>') {
                i += 2;
                selfClose = true;
                break;
            }
            std::string attr;
            if (!readName(attr))
                return fail("atributo inválido em <" + name + ">");
            if (!seen.insert(attr).second)
                return fail("atributo repetido: " + attr);
            skipSpace();
            if (i >= xml.size() || xml[i] != '=')
                return fail("atributo sem '=': " + attr);
            ++i;
            skipSpace();
            if (i >= xml.size() || (xml[i] != '"' && xml[i] != '\''))
                return fail("valor sem aspas: " + attr);
            const char quote = xml[i++];
            const std::size_t end = xml.find(quote, i);
            if (end == std::string::npos)
                return fail("valor sem fim: " + attr);
            const std::string value = xml.substr(i, end - i);
            if (value.find('<') != std::string::npos || !checkEntities(value))
                return fail("valor inválido em " + attr);
            i = end + 1;
        }
        if (!selfClose)
            stack.push_back(name);
        else if (stack.empty())
            rootDone = true;
    }
    if (!stack.empty())
        return fail("elemento <" + stack.back() + "> não foi fechado");
    if (!rootDone)
        return fail("sem elemento raiz");
    return true;
}

std::string csvChannels(const std::vector<FixtureType>& types)
{
    std::string out = "\xEF\xBB\xBF";
    out += "Fixture;Personalidade;Canal;Nome;Atributo;Bits;Faixa DMX;Padrao;Funcao;Observacao\r\n";
    for (const FixtureType& t : types) {
        for (const FixtureMode& m : t.modes) {
            for (const ChannelRow& r : m.rows) {
                for (int k = 0; k < r.span; ++k) {
                    std::string name = r.name, attr = r.key;
                    std::string bits = "8";
                    int def = r.defaultRaw;
                    if (r.span == 2) {
                        name += k == 0 ? " (grosso)" : " (fino)";
                        if (k == 1)
                            attr += "_Fine";
                        bits = k == 0 ? "16 (grosso)" : "16 (fino)";
                        def = k == 0 ? (r.defaultRaw >> 8) : (r.defaultRaw & 0xFF);
                    }
                    out += csvCell(t.name) + ";" + csvCell(m.name) + ";" + std::to_string(r.offset + k) + ";" + csvCell(name) +
                           ";" + csvCell(attr) + ";" + bits + ";0-255;" + std::to_string(def) + ";" + csvCell(r.range) + ";" +
                           csvCell(r.note) + "\r\n";
                }
            }
        }
    }
    return out;
}

std::string csvPatch(const Config& cfg, const std::function<std::string(int)>& layerName)
{
    std::string out = "\xEF\xBB\xBF";
    out += "Fixture;Personalidade;Camada;Universo;Endereco inicial;Endereco final;Canais\r\n";
    for (const Block& b : cfg.blocks) {
        if (!b.enabled)
            continue;
        const bool master = b.kind == BlockKind::Master;
        const std::string fixture = master ? "BSL Stage Master" : "BSL Stage Layer";
        const std::string mode = master ? "Master" : personalityName(b.mode, static_cast<int>(b.fx.size()));
        const std::string layer = master ? "Master" : (layerName ? layerName(b.layerId) : std::to_string(b.layerId));
        out += csvCell(fixture) + ";" + csvCell(mode) + ";" + csvCell(layer) + ";" + std::to_string(b.universe) + ";" +
               std::to_string(b.channel) + ";" + std::to_string(b.lastChannel()) + ";" + std::to_string(b.channelCount()) + "\r\n";
    }
    return out;
}

std::string htmlChannelTable(const std::vector<FixtureType>& types, const Config& cfg,
                             const std::function<std::string(int)>& layerName)
{
    auto esc = [](const std::string& s) { return xmlEscape(s); };
    std::string h;
    h += "<!DOCTYPE html>\n<html lang=\"pt-BR\">\n<head>\n<meta charset=\"utf-8\">\n<title>BSL Stage - tabela de canais DMX</title>\n";
    h += "<style>\n"
         "body{font-family:Segoe UI,Arial,sans-serif;color:#1c1c1c;margin:24px;}\n"
         "h1{font-size:22px;margin:0 0 4px;} h2{font-size:17px;margin:26px 0 4px;border-bottom:2px solid #ec3337;padding-bottom:3px;}\n"
         "h3{font-size:14px;margin:16px 0 4px;} p.sub{color:#555;margin:2px 0 8px;font-size:13px;}\n"
         "table{border-collapse:collapse;width:100%;font-size:12.5px;margin-bottom:8px;}\n"
         "th{background:#2c2c2c;color:#fff;text-align:left;padding:5px 8px;} td{border-bottom:1px solid #ddd;padding:4px 8px;vertical-align:top;}\n"
         "td.n{font-family:Consolas,monospace;text-align:right;width:46px;} td.r{color:#444;}\n"
         "@media print{body{margin:10mm;} h2{page-break-after:avoid;} table{page-break-inside:auto;} tr{page-break-inside:avoid;}}\n"
         "</style>\n</head>\n<body>\n";
    h += "<h1>BSL Stage - tabela de canais DMX</h1>\n";
    h += "<p class=\"sub\">Perfis para criar os fixtures na mesa ou no software de luz. Canais numerados a partir de 1 dentro de cada "
         "personalidade; valores DMX de 0 a 255. Para gerar o PDF: Imprimir, Salvar como PDF.</p>\n";

    if (!cfg.blocks.empty()) {
        h += "<h2>Patch deste projeto</h2>\n<table>\n<tr><th>Fixture</th><th>Personalidade</th><th>Camada</th><th>Universo</th>"
             "<th>Endereço inicial</th><th>Endereço final</th><th>Canais</th></tr>\n";
        for (const Block& b : cfg.blocks) {
            if (!b.enabled)
                continue;
            const bool master = b.kind == BlockKind::Master;
            h += "<tr><td>" + esc(master ? "BSL Stage Master" : "BSL Stage Layer") + "</td><td>" +
                 esc(master ? "Master" : personalityName(b.mode, static_cast<int>(b.fx.size()))) + "</td><td>" +
                 esc(master ? "Master" : (layerName ? layerName(b.layerId) : std::to_string(b.layerId))) + "</td><td>" +
                 std::to_string(b.universe) + "</td><td>" + std::to_string(b.channel) + "</td><td>" +
                 std::to_string(b.lastChannel()) + "</td><td>" + std::to_string(b.channelCount()) + "</td></tr>\n";
        }
        h += "</table>\n";
    }

    for (const FixtureType& t : types) {
        h += "<h2>" + esc(t.name) + "</h2>\n<p class=\"sub\">" + esc(t.description) + "</p>\n";
        for (const FixtureMode& m : t.modes) {
            h += "<h3>Personalidade: " + esc(m.name) + " (" + std::to_string(m.channels) + " canais)</h3>\n";
            h += "<table>\n<tr><th>Canal</th><th>Nome</th><th>Atributo</th><th>Bits</th><th>Padrão</th><th>Função / faixas</th></tr>\n";
            for (const ChannelRow& r : m.rows) {
                for (int k = 0; k < r.span; ++k) {
                    std::string name = r.name;
                    int def = r.defaultRaw;
                    std::string bits = "8";
                    if (r.span == 2) {
                        name += k == 0 ? " (grosso)" : " (fino)";
                        bits = k == 0 ? "16 grosso" : "16 fino";
                        def = k == 0 ? (r.defaultRaw >> 8) : (r.defaultRaw & 0xFF);
                    }
                    h += "<tr><td class=\"n\">" + std::to_string(r.offset + k) + "</td><td>" + esc(name) + "</td><td>" +
                         esc(k == 0 ? r.key : r.key + "_Fine") + "</td><td>" + bits + "</td><td class=\"n\">" + std::to_string(def) +
                         "</td><td class=\"r\">" + esc(r.range) + (r.note.empty() ? "" : "<br><small>" + esc(r.note) + "</small>") +
                         "</td></tr>\n";
                }
            }
            h += "</table>\n";
        }
    }
    h += "</body>\n</html>\n";
    return h;
}

namespace {

bool writeFile(const std::filesystem::path& file, const void* data, std::size_t size, std::string* error)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error)
            *error = "não foi possível criar " + file.filename().string();
        return false;
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    out.flush();
    if (!out) {
        if (error)
            *error = "erro ao gravar " + file.filename().string();
        return false;
    }
    return true;
}

bool writeText(const std::filesystem::path& file, const std::string& text, std::string* error)
{
    return writeFile(file, text.data(), text.size(), error);
}

const char* kReadmeTables =
    "BSL Stage - como criar os fixtures a partir das tabelas\r\n"
    "\r\n"
    "Duas opcoes:\r\n"
    "\r\n"
    "1) Pela tabela (sempre funciona): abra canais.csv no editor de tipos de fixture da sua mesa ou software de luz e crie\r\n"
    "   um tipo novo com um canal por linha, na ordem da coluna Canal. Em cada canal, crie as funcoes/faixas indicadas em\r\n"
    "   'Funcao' (por exemplo, no canal Clip: 0-9 nada, 10-19 limpar, 20-29 coluna 1...). Valores de 0 a 255.\r\n"
    "   Canais de 16 bits: marque 'grosso' + 'fino' no mesmo atributo.\r\n"
    "\r\n"
    "2) Pelo GDTF: a pasta ..\\GDTF tem os arquivos .gdtf, um padrao aberto (DIN SPEC 15800) que muitas mesas e softwares\r\n"
    "   importam. O GDTF Share (gdtf-share.com) tambem converte e publica arquivos GDTF.\r\n"
    "\r\n"
    "Depois: no patch da mesa, adicione o fixture e use o universo e o endereco de patch.csv.\r\n"
    "canais.html abre no navegador e pode ser impresso como PDF.\r\n"
    "No BSL Stage: Saida > DMX / Art-Net, ligue o receptor e escolha a placa de rede.\r\n";

const char* kReadmeRoot =
    "BSL Stage - perfis DMX exportados\r\n"
    "\r\n"
    "GDTF\\     arquivos .gdtf (importe na lista de tipos de fixture da mesa ou do software de luz)\r\n"
    "Tabelas\\  canais.csv, patch.csv, canais.html (abra no navegador e imprima como PDF) e um passo a passo\r\n"
    "\r\n"
    "Veja docs\\DMX.md no BSL Stage para o passo a passo completo.\r\n";

}

ExportResult exportProfiles(const std::filesystem::path& parentFolder, const Config& cfg,
                            const std::function<std::string(int)>& layerName)
{
    ExportResult res;
    namespace fs = std::filesystem;
    std::error_code ec;
    res.folder = parentFolder / L"BSL Stage DMX";
    const fs::path gdtfDir = res.folder / L"GDTF", tables = res.folder / L"Tabelas";
    for (const fs::path& dir : {res.folder, gdtfDir, tables}) {
        fs::create_directories(dir, ec);
        if (ec) {
            res.error = "não foi possível criar a pasta " + dir.string();
            return res;
        }
    }

    const std::vector<FixtureType> types = fixtureTypes(cfg);
    const std::string date = isoNow();
    auto add = [&](const fs::path& file) { res.files.push_back(file); };

    for (const FixtureType& t : types) {
        const std::vector<std::uint8_t> zip = gdtfArchive(t, date);
        const fs::path file = gdtfDir / fs::path(gdtfFileName(t));
        if (!writeFile(file, zip.data(), zip.size(), &res.error))
            return res;
        add(file);
    }
    const std::string channels = csvChannels(types);
    const struct
    {
        fs::path file;
        std::string text;
    } texts[] = {
        {res.folder / L"LEIA-ME.txt", kReadmeRoot},
        {tables / L"LEIA-ME.txt", kReadmeTables},
        {tables / L"canais.csv", channels},
        {tables / L"patch.csv", csvPatch(cfg, layerName)},
        {tables / L"canais.html", htmlChannelTable(types, cfg, layerName)},
    };
    for (const auto& t : texts) {
        if (!writeText(t.file, t.text, &res.error))
            return res;
        add(t.file);
    }
    res.ok = true;
    return res;
}

}
