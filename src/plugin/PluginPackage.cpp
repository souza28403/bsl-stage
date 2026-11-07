#include "plugin/PluginPackage.h"

#include "app/Paths.h"
#include "fx/IsfDoc.h"
#include "plugin/PluginFiles.h"
#include "update/Zip.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace bsl::plugin {

namespace fs = std::filesystem;

namespace {

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path pathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string extensionOf(const std::string& name)
{
    const size_t slash = name.rfind('/');
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    return lowerAscii(name.substr(dot));
}

bool isDeviceName(const std::string& segment)
{
    static const std::set<std::string> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
                                                   "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
                                                   "lpt7", "lpt8", "lpt9"};
    return reserved.count(lowerAscii(segment.substr(0, segment.find('.')))) != 0;
}

bool isTextExtension(const std::string& e)
{
    return e == ".fs" || e == ".vs" || e == ".json" || e == ".txt" || e == ".md";
}

bool looksExecutable(const std::string& data)
{
    if (data.size() >= 2 && data[0] == 'M' && data[1] == 'Z')
        return true;
    if (data.size() >= 4 && data.compare(0, 4, "\x7f" "ELF") == 0)
        return true;
    if (data.size() >= 2 && data[0] == 'P' && data[1] == 'K')
        return true;
    if (data.size() >= 2 && data[0] == '#' && data[1] == '!')
        return true;
    return false;
}

std::string trimmed(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    return s.substr(a);
}

bool hasControlChars(const std::string& s)
{
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 0x20 && c != '\n' && c != '\t' && c != '\r'; });
}

std::string validateManifest(const Manifest& m)
{
    if (trimmed(m.name).empty())
        return "O plugin precisa de um nome.";
    if (trimmed(m.license).empty())
        return "Informe a licença do plugin (por exemplo MIT ou CC BY 4.0).";
    if (m.name.size() > 80 || m.author.size() > 120 || m.version.size() > 40 || m.license.size() > 120 || m.description.size() > 2000)
        return "Algum campo do manifest está comprido demais.";
    if (hasControlChars(m.name) || hasControlChars(m.author) || hasControlChars(m.version) || hasControlChars(m.license) ||
        hasControlChars(m.description))
        return "O manifest tem caracteres inválidos.";
    return {};
}

void put16(std::vector<std::uint8_t>& o, std::uint32_t v)
{
    o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t>& o, std::uint32_t v)
{
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

}

std::vector<std::uint8_t> zipStoreItems(const std::vector<ZipItem>& items)
{
    std::vector<std::uint8_t> out;
    struct Rec
    {
        std::uint32_t offset, crc, size;
        const ZipItem* item;
    };
    std::vector<Rec> recs;
    for (const ZipItem& it : items) {
        Rec r{static_cast<std::uint32_t>(out.size()), update::crc32Update(0, it.data.data(), it.data.size()),
              static_cast<std::uint32_t>(it.data.size()), &it};
        put32(out, 0x04034B50u);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0x21);
        put32(out, r.crc);
        put32(out, r.size);
        put32(out, r.size);
        put16(out, static_cast<std::uint32_t>(it.name.size()));
        put16(out, 0);
        out.insert(out.end(), it.name.begin(), it.name.end());
        out.insert(out.end(), it.data.begin(), it.data.end());
        recs.push_back(r);
    }
    const std::uint32_t centralStart = static_cast<std::uint32_t>(out.size());
    for (const Rec& r : recs) {
        put32(out, 0x02014B50u);
        put16(out, 20);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0x21);
        put32(out, r.crc);
        put32(out, r.size);
        put32(out, r.size);
        put16(out, static_cast<std::uint32_t>(r.item->name.size()));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, 0);
        put32(out, r.offset);
        out.insert(out.end(), r.item->name.begin(), r.item->name.end());
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;
    put32(out, 0x06054B50u);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint32_t>(recs.size()));
    put16(out, static_cast<std::uint32_t>(recs.size()));
    put32(out, centralSize);
    put32(out, centralStart);
    put16(out, 0);
    return out;
}

const std::vector<std::string>& suggestedLicenses()
{
    static const std::vector<std::string> v = {"MIT", "CC BY 4.0", "CC BY-SA 4.0", "CC BY-NC 4.0", "CC0 (domínio público)",
                                               "GPL-3.0", "Todos os direitos reservados"};
    return v;
}

bool checkEntryName(const std::string& name, std::string* why)
{
    auto fail = [&](const std::string& message) {
        if (why)
            *why = message;
        return false;
    };
    if (name.empty() || name.size() > 120)
        return fail("nome de arquivo vazio ou comprido demais");
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7F)
            return fail("caractere inválido no nome de \"" + name.substr(0, 40) + "\"");
    if (!update::isSafeZipPath(name))
        return fail("caminho inseguro no pacote: \"" + name + "\"");
    if (name.find_first_of("*?\"<>|") != std::string::npos)
        return fail("nome de arquivo inválido: \"" + name + "\"");
    int depth = 0;
    size_t start = 0;
    while (start < name.size()) {
        const size_t slash = name.find('/', start);
        const std::string seg = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!seg.empty()) {
            ++depth;
            if (seg.back() == '.' || seg.back() == ' ')
                return fail("nome de arquivo inválido: \"" + name + "\"");
            if (isDeviceName(seg))
                return fail("nome reservado do Windows: \"" + name + "\"");
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    if (depth > 5)
        return fail("pastas demais dentro do pacote: \"" + name + "\"");
    if (name.back() == '/')
        return true;

    const std::string ext = extensionOf(name);
    if (ext.empty())
        return fail("arquivo sem extensão: \"" + name + "\"");
    if (ext == ".json")
        return name == "manifest.json" ? true : fail("só o manifest.json é aceito, não \"" + name + "\"");
    if (isTextExtension(ext) || isPluginImageExtension(ext))
        return true;
    return fail("tipo de arquivo não permitido no pacote: \"" + name + "\" (um plugin tem só shader, imagens e texto; programas e scripts são recusados)");
}

PackageResult exportPlugin(const fs::path& shaderFile, const Manifest& manifest, const fs::path& outFile)
{
    PackageResult res;
    if (const std::string err = validateManifest(manifest); !err.empty()) {
        res.error = err;
        return res;
    }
    std::string text;
    if (!readTextFile(shaderFile, text, 1u << 20)) {
        res.error = "Não foi possível ler o shader (arquivo grande demais ou ausente).";
        return res;
    }
    const std::string stem = paths::toUtf8(shaderFile.stem());
    std::string vertex;
    fs::path vsFile = shaderFile;
    vsFile.replace_extension(".vs");
    std::error_code ec;
    const bool hasVs = fs::is_regular_file(vsFile, ec) && readTextFile(vsFile, vertex, 1u << 20);
    const fx::IsfParseResult parsed = fx::parseIsf(text, vertex, stem);
    if (!parsed.ok) {
        res.error = "O shader tem erro e não foi exportado: " + parsed.error;
        return res;
    }

    std::vector<ZipItem> files;
    std::vector<std::string> names;
    auto addFile = [&](const std::string& name, const std::string& bytes) {
        files.push_back({name, bytes});
        names.push_back(name);
    };
    const std::string shaderName = stem + ".fs";
    if (std::string why; !checkEntryName(shaderName, &why)) {
        res.error = "O nome do arquivo não pode ir no pacote: " + why;
        return res;
    }
    addFile(shaderName, text);
    if (hasVs)
        addFile(stem + ".vs", vertex);

    std::set<std::string> seen = {lowerAscii(shaderName), lowerAscii(stem + ".vs")};
    for (const fx::IsfImported& im : parsed.doc->imported) {
        std::string rel = im.path;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (std::string why; !checkEntryName(rel, &why) || rel.back() == '/') {
            res.error = "A imagem \"" + im.path + "\" não pode ir no pacote" + (why.empty() ? "." : ": " + why);
            return res;
        }
        if (!isPluginImageExtension(extensionOf(rel))) {
            res.error = "A imagem \"" + im.path + "\" precisa ser PNG, JPG, BMP ou TGA.";
            return res;
        }
        if (!seen.insert(lowerAscii(rel)).second)
            continue;
        const fs::path imgFile = shaderFile.parent_path() / pathFromUtf8(rel);
        std::string bytes;
        if (!readTextFile(imgFile, bytes, static_cast<size_t>(kMaxEntryBytes))) {
            res.error = "Não encontrei a imagem \"" + im.path + "\" ao lado do shader.";
            return res;
        }
        addFile(rel, bytes);
    }

    nlohmann::ordered_json m;
    m["format"] = "bslplugin";
    m["formatVersion"] = 1;
    m["name"] = trimmed(manifest.name);
    m["author"] = trimmed(manifest.author);
    m["version"] = trimmed(manifest.version);
    m["license"] = trimmed(manifest.license);
    m["description"] = trimmed(manifest.description);
    m["shaders"] = nlohmann::ordered_json::array({shaderName});
    m["files"] = names;
    const std::string mtext = m.dump(2) + "\n";
    files.insert(files.begin(), ZipItem{"manifest.json", mtext});

    std::uint64_t total = 0;
    for (const ZipItem& f : files)
        total += f.data.size();
    if (total > kMaxTotalBytes) {
        res.error = "O plugin passa de 48 MB com as imagens. Reduza o tamanho das imagens.";
        return res;
    }
    const std::vector<std::uint8_t> bytes = zipStoreItems(files);
    if (bytes.size() > kMaxPackageBytes) {
        res.error = "O pacote ficou grande demais (máximo 24 MB).";
        return res;
    }
    fs::create_directories(outFile.parent_path(), ec);
    fs::path tmp = outFile;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            res.error = "Não foi possível gravar o arquivo " + paths::toUtf8(outFile.filename());
            return res;
        }
    }
    fs::rename(tmp, outFile, ec);
    if (ec) {
        fs::remove(tmp, ec);
        res.error = "Não foi possível salvar o arquivo " + paths::toUtf8(outFile.filename());
        return res;
    }
    res.ok = true;
    return res;
}

PackageResult inspectPackage(const fs::path& package, PackageInfo& info)
{
    PackageResult res;
    info = PackageInfo{};
    auto fail = [&](const std::string& message) {
        res.ok = false;
        res.error = message;
        return res;
    };

    std::error_code ec;
    const auto fileSize = fs::file_size(package, ec);
    if (ec)
        return fail("Não foi possível abrir o arquivo do plugin.");
    if (fileSize > kMaxPackageBytes)
        return fail("O arquivo é grande demais para ser um plugin (máximo 24 MB).");

    update::ZipReader zip;
    std::string zerr;
    if (!zip.open(package, &zerr))
        return fail("Este arquivo não é um pacote de plugin válido (" + zerr + ").");
    const auto& entries = zip.entries();
    if (entries.empty())
        return fail("O pacote está vazio.");
    if (entries.size() > kMaxEntries)
        return fail("O pacote tem arquivos demais (máximo 64).");

    std::set<std::string> lowerNames;
    std::uint64_t total = 0;
    for (const update::ZipEntry& e : entries) {
        std::string why;
        if (!checkEntryName(e.name, &why))
            return fail("Pacote recusado: " + why + ".");
        if (e.isDirectory)
            continue;
        if (!lowerNames.insert(lowerAscii(e.name)).second)
            return fail("Pacote recusado: arquivo repetido \"" + e.name + "\".");
        if (e.flags & 1)
            return fail("Pacote recusado: arquivo criptografado.");
        if (e.method != 0 && e.method != 8)
            return fail("Pacote recusado: método de compressão não suportado.");
        if (e.size > kMaxEntryBytes)
            return fail("Pacote recusado: \"" + e.name + "\" é grande demais (máximo 16 MB por arquivo).");
        if (e.size > (1u << 20) && e.compressedSize > 0 && e.size / e.compressedSize > 200)
            return fail("Pacote recusado: \"" + e.name + "\" tem uma taxa de compressão absurda.");
        total += e.size;
        if (total > kMaxTotalBytes)
            return fail("Pacote recusado: o conteúdo passa de 48 MB.");
        info.files.push_back({e.name, e.size});
    }
    info.totalSize = total;

    const update::ZipEntry* mEntry = zip.find("manifest.json");
    if (!mEntry)
        return fail("Pacote recusado: falta o manifest.json (nome, autor, versão e licença).");
    std::string mtext;
    if (!zip.readAll(*mEntry, mtext, &zerr))
        return fail("Pacote recusado: o manifest.json está corrompido (" + zerr + ").");
    nlohmann::json mj;
    try {
        mj = nlohmann::json::parse(mtext);
    } catch (const nlohmann::json::exception&) {
        return fail("Pacote recusado: o manifest.json não é um JSON válido.");
    }
    if (!mj.is_object())
        return fail("Pacote recusado: o manifest.json precisa ser um objeto { ... }.");
    auto str = [&](const char* key, std::string& out) {
        const auto it = mj.find(key);
        if (it == mj.end())
            return true;
        if (!it->is_string())
            return false;
        out = it->get<std::string>();
        return true;
    };
    if (!str("name", info.manifest.name) || !str("author", info.manifest.author) || !str("version", info.manifest.version) ||
        !str("license", info.manifest.license) || !str("description", info.manifest.description))
        return fail("Pacote recusado: o manifest.json tem campos que não são texto.");
    if (const std::string err = validateManifest(info.manifest); !err.empty())
        return fail("Pacote recusado: " + err);

    std::map<std::string, std::string> shaderText;
    for (const update::ZipEntry& e : entries) {
        if (e.isDirectory || e.name == "manifest.json")
            continue;
        std::string data;
        if (!zip.readAll(e, data, &zerr))
            return fail("Pacote recusado: \"" + e.name + "\" está corrompido (" + zerr + ").");
        const std::string ext = extensionOf(e.name);
        if (looksExecutable(data))
            return fail("Pacote recusado: \"" + e.name + "\" parece um programa ou outro pacote, não um arquivo de plugin.");
        if (isTextExtension(ext) && data.find('\0') != std::string::npos)
            return fail("Pacote recusado: \"" + e.name + "\" não é um arquivo de texto.");
        if (ext == ".fs") {
            if (data.size() > (1u << 20))
                return fail("Pacote recusado: o shader \"" + e.name + "\" é grande demais.");
            shaderText[e.name] = std::move(data);
            info.shaders.push_back(e.name);
        }
    }
    if (info.shaders.empty())
        return fail("Pacote recusado: não tem nenhum shader (.fs).");
    if (info.shaders.size() > kMaxShaders)
        return fail("Pacote recusado: shaders demais (máximo 32).");

    for (const std::string& name : info.shaders) {
        std::string vertex;
        std::string vsName = name.substr(0, name.size() - 3) + ".vs";
        for (const auto& f : info.files)
            if (lowerAscii(f.name) == lowerAscii(vsName)) {
                if (const update::ZipEntry* ve = zip.find(f.name))
                    zip.readAll(*ve, vertex, nullptr);
            }
        const fx::IsfParseResult parsed = fx::parseIsf(shaderText[name], vertex, name.substr(0, name.size() - 3));
        if (!parsed.ok)
            return fail("Pacote recusado: o shader \"" + name + "\" tem erro: " + parsed.error);
        for (const fx::IsfImported& im : parsed.doc->imported) {
            std::string rel = im.path;
            std::replace(rel.begin(), rel.end(), '\\', '/');
            const std::string base = name.rfind('/') == std::string::npos ? std::string() : name.substr(0, name.rfind('/') + 1);
            if (!lowerNames.count(lowerAscii(base + rel)))
                info.warnings.push_back("O shader \"" + name + "\" usa a imagem \"" + im.path + "\", que não está no pacote.");
        }
        for (const std::string& w : parsed.doc->warnings)
            info.warnings.push_back(name + ": " + w);
    }
    res.ok = true;
    return res;
}

InstallResult installPackage(const fs::path& package, const fs::path& destinationParent)
{
    InstallResult res;
    const PackageResult check = inspectPackage(package, res.info);
    if (!check.ok) {
        res.error = check.error;
        return res;
    }
    update::ZipReader zip;
    std::string zerr;
    if (!zip.open(package, &zerr)) {
        res.error = "Não foi possível reabrir o pacote (" + zerr + ").";
        return res;
    }

    std::error_code ec;
    fs::create_directories(destinationParent, ec);
    const std::string folderName = safeFileName(res.info.manifest.name);
    fs::path folder = destinationParent / pathFromUtf8(folderName);
    for (int n = 2; fs::exists(folder, ec) && n < 1000; ++n)
        folder = destinationParent / pathFromUtf8(folderName + " " + std::to_string(n));
    if (!fs::create_directories(folder, ec) || ec) {
        res.error = "Não foi possível criar a pasta do plugin em " + paths::toUtf8(destinationParent);
        return res;
    }
    const fs::path root = folder.lexically_normal();
    auto cleanup = [&]() {
        std::error_code ec2;
        fs::remove_all(folder, ec2);
    };

    for (const update::ZipEntry& e : zip.entries()) {
        if (e.isDirectory)
            continue;
        const fs::path target = (folder / pathFromUtf8(e.name)).lexically_normal();

        const std::string rootStr = paths::toUtf8(root) + "\\";
        const std::string targetStr = paths::toUtf8(target);
        if (targetStr.size() <= rootStr.size() || lowerAscii(targetStr.substr(0, rootStr.size())) != lowerAscii(rootStr)) {
            cleanup();
            res.error = "Pacote recusado: caminho fora da pasta do plugin.";
            return res;
        }
        fs::create_directories(target.parent_path(), ec);
        if (!zip.extract(e, target, &zerr)) {
            cleanup();
            res.error = "Não foi possível extrair \"" + e.name + "\" (" + zerr + ").";
            return res;
        }
        if (extensionOf(e.name) == ".fs")
            res.shaders.push_back(target);
    }
    res.ok = true;
    res.folder = folder;
    return res;
}

}
