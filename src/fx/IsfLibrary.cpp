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

std::int64_t toTicks(const fs::file_time_type& t)
{
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}

void stampOf(const fs::path& file, std::int64_t& time, std::uint64_t& size)
{
    time = 0;
    size = 0;
    std::error_code ec;
    const auto t = fs::last_write_time(file, ec);
    if (!ec)
        time = toTicks(t);
    const auto sz = fs::file_size(file, ec);
    if (!ec)
        size = sz;
    fs::path vs = file;
    vs.replace_extension(".vs");
    if (fs::is_regular_file(vs, ec)) {
        const auto t2 = fs::last_write_time(vs, ec);
        if (!ec)
            time ^= toTicks(t2) * 31;
        const auto sz2 = fs::file_size(vs, ec);
        if (!ec)
            size += sz2 * 7919;
    }
}

std::string idFor(const fs::path& root, const fs::path& file)
{
    std::error_code ec;
    const fs::path rel = fs::relative(file, root, ec);
    const std::u8string u = (ec ? file.filename() : rel).generic_u8string();
    return "isf:" + std::string(u.begin(), u.end());
}

}

IsfLibrary& IsfLibrary::instance()
{
    static IsfLibrary lib;
    return lib;
}

IsfLibrary::~IsfLibrary()
{
    stopWatcher();
    if (worker_.joinable())
        worker_.join();
}

void IsfLibrary::configure(std::vector<fs::path> dirs, const fs::path& samplesDir)
{
    std::error_code ec;
    if (!dirs.empty() && !dirs.front().empty() && !fs::exists(dirs.front(), ec)) {
        fs::create_directories(dirs.front(), ec);
        if (!ec && !samplesDir.empty() && fs::is_directory(samplesDir, ec)) {
            for (const auto& e : fs::directory_iterator(samplesDir, ec)) {
                const std::string ext = lowerAscii(paths::toUtf8(e.path().extension()));
                if (e.is_regular_file(ec) && (ext == ".fs" || ext == ".vs" || ext == ".png" || ext == ".jpg"))
                    fs::copy_file(e.path(), dirs.front() / e.path().filename(), fs::copy_options::skip_existing, ec);
            }
            log::info("ISF: pasta criada com os shaders de exemplo em " + paths::toUtf8(dirs.front()));
        }
    }
    {
        std::lock_guard lock(mutex_);
        dirs_ = std::move(dirs);
    }
    std::lock_guard watch(watchMutex_);
    seenValid_ = false;
    pendingStamps_.clear();
}

std::vector<fs::path> IsfLibrary::directories() const
{
    std::lock_guard lock(mutex_);
    return dirs_;
}

std::optional<std::string> IsfLibrary::idForFile(const fs::path& file) const
{
    std::vector<fs::path> dirs;
    {
        std::lock_guard lock(mutex_);
        dirs = dirs_;
    }
    std::error_code ec;
    const fs::path target = fs::weakly_canonical(file, ec);
    if (ec)
        return std::nullopt;
    for (const fs::path& dir : dirs) {
        const fs::path root = fs::weakly_canonical(dir, ec);
        if (ec || root.empty())
            continue;
        const fs::path rel = target.lexically_relative(root);
        if (rel.empty() || *rel.begin() == "..")
            continue;
        const std::u8string u = rel.generic_u8string();
        return "isf:" + std::string(u.begin(), u.end());
    }
    return std::nullopt;
}

IsfEntry IsfLibrary::loadFile(const fs::path& file, const std::string& id)
{
    IsfEntry e;
    e.id = id;
    e.file = file;
    e.name = paths::toUtf8(file.stem());
    stampOf(file, e.stampTime, e.stampSize);
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
        std::vector<FileStamp> stamps = listStamps(dirs);
        std::vector<IsfEntry> previous;
        {
            std::lock_guard lock(mutex_);
            previous = entries_;
        }
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
                    continue;
                ids.push_back(id);
                std::int64_t st = 0;
                std::uint64_t sz = 0;
                stampOf(it->path(), st, sz);
                const auto old = std::find_if(previous.begin(), previous.end(), [&](const IsfEntry& p) {
                    return p.id == id && p.file == it->path() && p.stampTime == st && p.stampSize == sz;
                });
                if (old != previous.end() && (old->doc || !old->error.empty()))
                    found.push_back(*old);
                else
                    found.push_back(loadFile(it->path(), id));
            }
        }
        std::sort(found.begin(), found.end(), [](const IsfEntry& a, const IsfEntry& b) {
            if (a.category != b.category)
                return lowerAscii(a.category) < lowerAscii(b.category);
            return lowerAscii(a.name) < lowerAscii(b.name);
        });

        {
            std::lock_guard watch(watchMutex_);
            seenStamps_ = std::move(stamps);
            pendingStamps_.clear();
            seenValid_ = true;
        }
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

std::vector<IsfLibrary::FileStamp> IsfLibrary::listStamps(const std::vector<fs::path>& dirs)
{
    std::vector<FileStamp> out;
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec))
            continue;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (!it->is_regular_file(ec2))
                continue;
            const std::string ext = lowerAscii(paths::toUtf8(it->path().extension()));
            if (ext != ".fs" && ext != ".vs")
                continue;
            if (out.size() >= kMaxEntries * 2)
                break;
            FileStamp f;
            f.path = paths::toUtf8(it->path());
            const auto t = it->last_write_time(ec2);
            f.time = ec2 ? 0 : toTicks(t);
            const auto sz = it->file_size(ec2);
            f.size = ec2 ? 0 : sz;
            out.push_back(std::move(f));
        }
    }
    std::sort(out.begin(), out.end(), [](const FileStamp& a, const FileStamp& b) { return a.path < b.path; });
    return out;
}

bool IsfLibrary::checkForChanges(bool immediate)
{
    std::vector<fs::path> dirs;
    {
        std::lock_guard lock(mutex_);
        if (scanning_.load())
            return false;
        dirs = dirs_;
    }
    std::vector<FileStamp> now = listStamps(dirs);
    {
        std::lock_guard watch(watchMutex_);
        if (!seenValid_) {
            seenStamps_ = std::move(now);
            seenValid_ = true;
            return false;
        }
        if (now == seenStamps_) {
            pendingStamps_.clear();
            return false;
        }
        if (!immediate && pendingStamps_ != now) {
            pendingStamps_ = std::move(now);
            return false;
        }
        pendingStamps_.clear();
        seenStamps_ = std::move(now);
    }
    rescan();
    return true;
}

void IsfLibrary::startWatcher(int intervalMs)
{
    std::lock_guard lock(stopMutex_);
    if (watcher_.joinable())
        return;
    stopWatcher_ = false;
    watcher_ = std::thread([this, intervalMs]() {
        for (;;) {
            {
                std::unique_lock lk(stopMutex_);
                if (stopCv_.wait_for(lk, std::chrono::milliseconds(intervalMs), [this] { return stopWatcher_; }))
                    return;
            }
            checkForChanges(false);
        }
    });
}

void IsfLibrary::stopWatcher()
{
    std::thread t;
    {
        std::lock_guard lock(stopMutex_);
        stopWatcher_ = true;
        t = std::move(watcher_);
    }
    stopCv_.notify_all();
    if (t.joinable())
        t.join();
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
    const std::string rel = id.substr(4);
    const fs::path relPath{std::u8string(rel.begin(), rel.end())};
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        const fs::path file = dir / relPath;
        if (fs::is_regular_file(file, ec)) {
            IsfEntry e = loadFile(file, id);
            std::lock_guard lock(mutex_);
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

}
