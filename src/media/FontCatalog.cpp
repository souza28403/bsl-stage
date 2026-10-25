#include "media/FontCatalog.h"

#include "app/Log.h"
#include "app/Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <thread>

namespace bsl::media {

namespace fs = std::filesystem;

namespace {

fs::path knownFolder(const KNOWNFOLDERID& id)
{
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw)
        result = raw;
    CoTaskMemFree(raw);
    return result;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::uint32_t be32(const unsigned char* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

std::uint16_t be16(const unsigned char* p)
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Texto de um registro da tabela 'name': UTF-16BE (plataforma 3/0) ou Mac Roman simplificado (plataforma 1)
std::string decodeName(const unsigned char* p, int len, int platform)
{
    std::string out;
    if (platform == 3 || platform == 0) {
        for (int i = 0; i + 1 < len; i += 2) {
            std::uint32_t cp = be16(p + i);
            if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < len) {
                const std::uint32_t lo = be16(p + i + 2);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            appendUtf8(out, cp);
        }
    } else {
        for (int i = 0; i < len; ++i)
            if (p[i] < 0x80)
                out += static_cast<char>(p[i]);
    }
    return out;
}

// Escolhe o melhor registro de um nameID: Windows/Unicode em ingles, depois qualquer Windows, depois Mac
std::string findName(const std::vector<unsigned char>& table, int wantedId)
{
    if (table.size() < 6)
        return {};
    const int count = be16(table.data() + 2);
    const int stringOffset = be16(table.data() + 4);
    int bestScore = -1;
    std::string best;
    for (int i = 0; i < count; ++i) {
        const size_t rec = 6 + static_cast<size_t>(i) * 12;
        if (rec + 12 > table.size())
            break;
        const unsigned char* r = table.data() + rec;
        const int platform = be16(r), lang = be16(r + 4), id = be16(r + 6), len = be16(r + 8), off = be16(r + 10);
        if (id != wantedId)
            continue;
        const size_t start = static_cast<size_t>(stringOffset) + static_cast<size_t>(off);
        if (start + static_cast<size_t>(len) > table.size())
            continue;
        int score = 0;
        if (platform == 3)
            score = lang == 0x0409 ? 4 : (lang == 0x0809 ? 3 : 2);
        else if (platform == 0)
            score = 1;
        else if (platform == 1 && lang == 0)
            score = 0;
        else
            continue;
        if (score > bestScore) {
            const std::string s = decodeName(table.data() + start, len, platform);
            if (!s.empty()) {
                bestScore = score;
                best = s;
            }
        }
    }
    return best;
}

bool isUnwanted(const std::string& label)
{
    const std::string l = lower(label);
    for (const char* bad : {"wingdings", "webdings", "marlett", "mdl2", "fluent icons", "symbol", "emoji", "dingbats",
                            "holomdl"})
        if (l.find(bad) != std::string::npos)
            return true;
    return false;
}

bool isFontFile(const fs::path& p)
{
    const std::string ext = lower(p.extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

} // namespace

bool readFontLabel(const fs::path& file, std::string& label)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    unsigned char head[16] = {};
    in.read(reinterpret_cast<char*>(head), 12);
    if (in.gcount() < 12)
        return false;

    std::uint32_t base = 0;
    if (head[0] == 't' && head[1] == 't' && head[2] == 'c' && head[3] == 'f') {
        in.seekg(12); // cabecalho TTC: numFonts em +8, offset da 1a fonte em +12
        unsigned char off[4] = {};
        in.read(reinterpret_cast<char*>(off), 4);
        if (in.gcount() < 4)
            return false;
        base = be32(off);
        in.seekg(base);
        in.read(reinterpret_cast<char*>(head), 12);
        if (in.gcount() < 12)
            return false;
    }
    const std::uint32_t tag = be32(head);
    if (tag != 0x00010000 && tag != 0x4F54544F /*OTTO*/ && tag != 0x74727565 /*true*/)
        return false;
    const int numTables = be16(head + 4);
    if (numTables <= 0 || numTables > 128)
        return false;

    std::vector<unsigned char> dir(static_cast<size_t>(numTables) * 16);
    in.read(reinterpret_cast<char*>(dir.data()), static_cast<std::streamsize>(dir.size()));
    if (static_cast<size_t>(in.gcount()) < dir.size())
        return false;

    std::uint32_t nameOff = 0, nameLen = 0;
    for (int i = 0; i < numTables; ++i) {
        const unsigned char* r = dir.data() + static_cast<size_t>(i) * 16;
        if (r[0] == 'n' && r[1] == 'a' && r[2] == 'm' && r[3] == 'e') {
            nameOff = be32(r + 8);
            nameLen = be32(r + 12);
        }
    }
    if (nameOff == 0 || nameLen < 6 || nameLen > (1u << 20))
        return false;

    std::vector<unsigned char> table(nameLen);
    in.clear();
    in.seekg(nameOff);
    in.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(nameLen));
    if (static_cast<std::uint32_t>(in.gcount()) < nameLen)
        return false;

    std::string family = findName(table, 16);
    if (family.empty())
        family = findName(table, 1);
    std::string style = findName(table, 17);
    if (style.empty())
        style = findName(table, 2);
    if (family.empty())
        return false;
    label = family;
    if (!style.empty() && lower(style) != "regular" && lower(style) != "normal")
        label += " " + style;
    return true;
}

struct FontCatalog::Impl
{
    mutable std::mutex m;
    std::vector<FontEntry> list;
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::thread worker;

    std::vector<fs::path> searchDirs() const
    {
        std::vector<fs::path> dirs;
        const fs::path assets = paths::assetsDir();
        if (!assets.empty())
            dirs.push_back(assets / L"fonts");
        const fs::path sys = knownFolder(FOLDERID_Fonts);
        if (!sys.empty())
            dirs.push_back(sys);
        const fs::path local = knownFolder(FOLDERID_LocalAppData);
        if (!local.empty())
            dirs.push_back(local / L"Microsoft" / L"Windows" / L"Fonts");
        return dirs;
    }

    void scan()
    {
        std::vector<FontEntry> found;
        const fs::path assets = paths::assetsDir();
        const fs::path bundledDir = assets.empty() ? fs::path() : assets / L"fonts";
        std::error_code ec;
        for (const fs::path& dir : searchDirs()) {
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code ec2;
                if (!it->is_regular_file(ec2) || !isFontFile(it->path()))
                    continue;
                std::string label;
                if (!readFontLabel(it->path(), label) || isUnwanted(label))
                    continue;
                FontEntry e;
                e.file = paths::toUtf8(it->path().filename());
                e.label = label;
                e.path = it->path();
                e.bundled = !bundledDir.empty() && dir == bundledDir;
                found.push_back(std::move(e));
            }
            ec.clear();
        }
        // Nome repetido entre arquivos diferentes: acrescenta o arquivo para diferenciar
        std::sort(found.begin(), found.end(), [](const FontEntry& a, const FontEntry& b) {
            const std::string la = lower(a.label), lb = lower(b.label);
            return la != lb ? la < lb : a.file < b.file;
        });
        for (size_t i = 1; i < found.size(); ++i)
            if (lower(found[i].label) == lower(found[i - 1].label)) {
                found[i].label += " (" + found[i].file + ")";
            }
        const size_t count = found.size();
        {
            std::lock_guard lk(m);
            list = std::move(found);
        }
        done = true;
        log::info("Fontes de texto encontradas: " + std::to_string(count));
    }
};

FontCatalog::FontCatalog() : impl_(std::make_unique<Impl>()) {}

FontCatalog::~FontCatalog()
{
    if (impl_->worker.joinable())
        impl_->worker.join();
}

FontCatalog& FontCatalog::instance()
{
    static FontCatalog catalog;
    return catalog;
}

void FontCatalog::ensureScanned()
{
    if (impl_->started.exchange(true))
        return;
    impl_->worker = std::thread([this] { impl_->scan(); });
}

bool FontCatalog::ready() const
{
    return impl_->done;
}

std::vector<FontEntry> FontCatalog::entries() const
{
    std::lock_guard lk(impl_->m);
    return impl_->list;
}

fs::path FontCatalog::resolve(const std::string& file) const
{
    if (file.empty())
        return {};
    const fs::path name(std::u8string(file.begin(), file.end()));
    if (name.has_parent_path())
        return {}; // so nomes de arquivo: caminhos nao entram no projeto
    std::error_code ec;
    for (const fs::path& dir : impl_->searchDirs()) {
        const fs::path candidate = dir / name;
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return {};
}

std::string FontCatalog::defaultFile() const
{
    for (const char* name : {"Barlow-Bold.ttf", "Barlow-Medium.ttf", "segoeuib.ttf", "arialbd.ttf", "segoeui.ttf",
                             "arial.ttf"})
        if (!resolve(name).empty())
            return name;
    return "arial.ttf";
}

std::string FontCatalog::labelFor(const std::string& file) const
{
    std::lock_guard lk(impl_->m);
    for (const FontEntry& e : impl_->list)
        if (e.file == file)
            return e.label;
    return file;
}

} // namespace bsl::media
