#include "fx/IsfLibrary.h"

#include "app/Log.h"
#include "app/Paths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

namespace bsl::fx {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;
constexpr size_t kMaxEntries = 5000;

bool readText(const fs::path& file, std::string& out)
{
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size > kMaxFileBytes)
        return false;
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Categorias comuns do ISF em portugues; as demais ficam como estao
std::string categoryPt(const std::vector<std::string>& cats, bool isEffect)
{
    if (cats.empty())
        return isEffect ? "Outros" : "Geradores";
    const std::string c = lowerAscii(cats.front());
    static const struct { const char* key; const char* pt; } table[] = {
        {"color effect", "Cor"},       {"color", "Cor"},           {"distortion effect", "Distorção"},
        {"geometry effect", "Geometria"}, {"blur", "Desfoque"},    {"sharpen", "Nitidez"},
        {"stylize", "Estilizar"},      {"tile effect", "Mosaico"}, {"halftone effect", "Meio-tom"},
        {"glitch", "Glitch"},          {"generator", "Geradores"}, {"transition", "Transição"},
        {"video", "Vídeo"},            {"utility", "Utilitários"}, {"feedback", "Feedback"},
    };
    for (const auto& t : table)
        if (c == t.key)
            return t.pt;
    return cats.front();
}

std::string idFor(const fs::path& root, const fs::path& file)
{
    std::error_code ec;
    const fs::path rel = fs::relative(file, root, ec);
    const std::u8string u = (ec ? file.filename() : rel).generic_u8string();
    return "isf:" + std::string(u.begin(), u.end());
}

} // namespace

IsfLibrary& IsfLibrary::instance()
{
    static IsfLibrary lib;
    return lib;
}

IsfLibrary::~IsfLibrary()
{
    if (worker_.joinable())
        worker_.join();
}

void IsfLibrary::configure(std::vector<fs::path> dirs, const fs::path& samplesDir)
{
    std::error_code ec;
    if (!dirs.empty() && !dirs.front().empty() && !fs::exists(dirs.front(), ec)) {
        fs::create_directories(dirs.front(), ec);
        if (!ec && !samplesDir.empty() && fs::is_directory(samplesDir, ec)) { // primeiro uso: copia os exemplos
            for (const auto& e : fs::directory_iterator(samplesDir, ec)) {
                const std::string ext = lowerAscii(paths::toUtf8(e.path().extension()));
                if (e.is_regular_file(ec) && (ext == ".fs" || ext == ".vs" || ext == ".png" || ext == ".jpg"))
                    fs::copy_file(e.path(), dirs.front() / e.path().filename(), fs::copy_options::skip_existing, ec);
            }
            log::info("ISF: pasta criada com os shaders de exemplo em " + paths::toUtf8(dirs.front()));
        }
    }
    std::lock_guard lock(mutex_);
    dirs_ = std::move(dirs);
}

std::vector<fs::path> IsfLibrary::directories() const
{
    std::lock_guard lock(mutex_);
    return dirs_;
}

IsfEntry IsfLibrary::loadFile(const fs::path& file, const std::string& id)
{
    IsfEntry e;
    e.id = id;
    e.file = file;
    e.name = paths::toUtf8(file.stem());
    std::string fragment;
    if (!readText(file, fragment)) {
        e.error = "Não foi possível ler o arquivo.";
        e.category = "Com erro";
        return e;
    }
    std::string vertex;
    fs::path vs = file;
    vs.replace_extension(".vs");
    std::error_code ec;
    if (fs::is_regular_file(vs, ec))
        readText(vs, vertex);
    IsfParseResult r = parseIsf(fragment, vertex, e.name);
    if (!r.ok) {
        e.error = r.error;
        e.category = "Com erro";
        return e;
    }
    e.doc = r.doc;
    e.isEffect = r.doc->isEffect();
    e.description = r.doc->description;
    e.category = categoryPt(r.doc->categories, e.isEffect);
    e.warnings = r.doc->warnings;
    return e;
}

void IsfLibrary::rescan()
{
    std::lock_guard lock(mutex_);
    if (scanning_) {
        rescanQueued_ = true;
        return;
    }
    if (worker_.joinable())
        worker_.join();
    scanning_ = true;
    worker_ = std::thread([this, dirs = dirs_]() mutable { scanWorker(std::move(dirs)); });
}

void IsfLibrary::waitScan()
{
    while (scanning_.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard lock(mutex_);
    if (worker_.joinable())
        worker_.join();
}

void IsfLibrary::scanWorker(std::vector<fs::path> dirs)
{
    for (;;) {
        std::vector<IsfEntry> found;
        std::vector<std::string> ids;
        for (const fs::path& dir : dirs) {
            std::error_code ec;
            if (dir.empty() || !fs::is_directory(dir, ec))
                continue;
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code ec2;
                if (!it->is_regular_file(ec2) || lowerAscii(paths::toUtf8(it->path().extension())) != ".fs")
                    continue;
                if (found.size() >= kMaxEntries)
                    break;
                const std::string id = idFor(dir, it->path());
                if (std::find(ids.begin(), ids.end(), id) != ids.end())
                    continue; // a pasta principal vence
                ids.push_back(id);
                found.push_back(loadFile(it->path(), id));
            }
        }
        std::sort(found.begin(), found.end(), [](const IsfEntry& a, const IsfEntry& b) {
            if (a.category != b.category)
                return lowerAscii(a.category) < lowerAscii(b.category);
            return lowerAscii(a.name) < lowerAscii(b.name);
        });

        std::lock_guard lock(mutex_);
        entries_ = std::move(found);
        version_.fetch_add(1);
        if (rescanQueued_) {
            rescanQueued_ = false;
            dirs = dirs_;
            continue;
        }
        scanning_ = false;
        return;
    }
}

std::vector<IsfEntry> IsfLibrary::entries() const
{
    std::lock_guard lock(mutex_);
    return entries_;
}

std::optional<IsfEntry> IsfLibrary::find(const std::string& id)
{
    std::vector<fs::path> dirs;
    {
        std::lock_guard lock(mutex_);
        for (const IsfEntry& e : entries_)
            if (e.id == id)
                return e;
        dirs = dirs_;
    }
    if (id.rfind("isf:", 0) != 0)
        return std::nullopt;
    // Ainda nao varrido (projeto aberto antes do fim da varredura): le o arquivo direto
    const std::string rel = id.substr(4);
    const fs::path relPath{std::u8string(rel.begin(), rel.end())};
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        const fs::path file = dir / relPath;
        if (fs::is_regular_file(file, ec)) {
            IsfEntry e = loadFile(file, id);
            std::lock_guard lock(mutex_); // guarda: as proximas consultas nao releem o disco
            if (std::none_of(entries_.begin(), entries_.end(), [&](const IsfEntry& x) { return x.id == id; }))
                entries_.push_back(e);
            return e;
        }
    }
    return std::nullopt;
}

std::shared_ptr<const IsfDoc> IsfLibrary::doc(const std::string& id)
{
    const auto e = find(id);
    return e ? e->doc : nullptr;
}

} // namespace bsl::fx
