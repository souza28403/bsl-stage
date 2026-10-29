#include "ffgl/FfglCatalog.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "ffgl/FfglPe.h"
#include "ffgl/FfglPlugin.h"
#include "media/WorkerProcess.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <vector>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>

namespace bsl::ffgl {

namespace fs = std::filesystem;

namespace {

std::string lowerAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string utf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

bool statFile(const fs::path& p, std::uint64_t& size, std::int64_t& mtime)
{
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &attr))
        return false;
    size = (static_cast<std::uint64_t>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
    mtime = static_cast<std::int64_t>((static_cast<std::uint64_t>(attr.ftLastWriteTime.dwHighDateTime) << 32)
                                      | attr.ftLastWriteTime.dwLowDateTime);
    return true;
}

bool hasDllExtension(const fs::path& p)
{
    std::wstring ext = p.extension().wstring();
    for (wchar_t& c : ext)
        c = static_cast<wchar_t>(std::towlower(c));
    return ext == L".dll";
}

std::shared_ptr<const CatalogEntry> makeEntry(const PluginInfo& info, const std::string& origin)
{
    auto e = std::make_shared<CatalogEntry>();
    e->info = info;
    e->typeId = makeTypeId(info.pluginId, info.fileName);
    e->origin = origin;
    e->map = buildParamMap(info);
    return e;
}

} // namespace

std::string pluginDisplayName(const PluginInfo& info)
{
    return info.name.empty() ? info.fileName : info.name;
}

Catalog& Catalog::instance()
{
    static Catalog* c = new Catalog(); // nunca destruido: o fim do processo derruba a thread (ver shutdown())
    return *c;
}

Catalog::~Catalog()
{
    shutdown();
}

void Catalog::shutdown()
{
    stop_ = true;
    if (HANDLE h = static_cast<HANDLE>(currentProc_.load()))
        TerminateProcess(h, 1);
    if (thread_.joinable())
        thread_.join();
    scanning_ = false;
}

void Catalog::configure(std::vector<Folder> folders)
{
    // O processo de varredura roda com outro diretorio atual: caminhos relativos nao valem la
    for (Folder& f : folders) {
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(f.path, ec);
        if (!ec)
            f.path = abs;
    }
    std::lock_guard lk(mutex_);
    folders_ = std::move(folders);
}

std::vector<Folder> Catalog::folders() const
{
    std::lock_guard lk(mutex_);
    return folders_;
}

void Catalog::setCachePath(fs::path p)
{
    std::lock_guard lk(mutex_);
    cachePath_ = std::move(p);
    cacheLoaded_ = false;
    cache_.clear();
}

void Catalog::clearCache()
{
    std::lock_guard lk(mutex_);
    cache_.clear();
    cacheLoaded_ = true;
    if (!cachePath_.empty()) {
        std::error_code ec;
        fs::remove(cachePath_, ec);
    }
}

std::string Catalog::scanStatus() const
{
    std::lock_guard lk(mutex_);
    return status_;
}

bool Catalog::waitIdle(unsigned timeoutMs)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (scanning_.load()) {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

void Catalog::rescan()
{
    std::lock_guard lk(mutex_);
    if (scanning_.load()) {
        restart_ = true;
        return;
    }
    if (thread_.joinable())
        thread_.join();
    stop_ = false;
    restart_ = false;
    scanning_ = true;
    status_ = "Procurando plugins…";
    thread_ = std::thread([this] { runScan(); });
}

void Catalog::retry(const std::string& pathUtf8)
{
    {
        std::lock_guard lk(mutex_);
        retryPaths_.push_back(lowerAscii(pathUtf8));
        std::erase_if(sessionBlocked_, [&](const BlockedPlugin& b) { return b.path == pathUtf8; });
    }
    faultEpoch_.fetch_add(1); // instancias que ainda esperam o desbloqueio olham de novo
    version_.fetch_add(1);
    rescan();
}

std::vector<EntryPtr> Catalog::entries() const
{
    std::lock_guard lk(mutex_);
    return entries_;
}

EntryPtr Catalog::find(const std::string& typeId) const
{
    std::string id, file;
    if (!splitTypeId(typeId, id, file))
        return nullptr;
    const std::string lowFile = lowerAscii(file);
    std::lock_guard lk(mutex_);
    for (const EntryPtr& e : entries_)
        if (e->typeId == typeId)
            return e;
    for (const EntryPtr& e : entries_) // mesmo id (o arquivo foi renomeado)
        if (makeTypeId(e->info.pluginId, e->info.fileName).compare(0, 9, typeId, 0, 9) == 0)
            return e;
    for (const EntryPtr& e : entries_) // mesmo nome de arquivo (o plugin mudou de id)
        if (lowerAscii(e->info.fileName) == lowFile)
            return e;
    return nullptr;
}

std::vector<BlockedPlugin> Catalog::blocked() const
{
    std::lock_guard lk(mutex_);
    std::vector<BlockedPlugin> out = blocked_;
    out.insert(out.end(), sessionBlocked_.begin(), sessionBlocked_.end());
    return out;
}

void Catalog::markSessionFault(const std::string& pathUtf8, const std::string& reason)
{
    {
        std::lock_guard lk(mutex_);
        for (BlockedPlugin& b : sessionBlocked_)
            if (b.path == pathUtf8) {
                b.reason = reason;
                faultEpoch_.fetch_add(1);
                version_.fetch_add(1);
                return;
            }
        BlockedPlugin b;
        b.path = pathUtf8;
        b.fileName = utf8(fromUtf8(pathUtf8).filename());
        for (const EntryPtr& e : entries_)
            if (e->info.path == pathUtf8)
                b.origin = e->origin;
        b.reason = reason;
        b.session = true;
        sessionBlocked_.push_back(std::move(b));
    }
    faultEpoch_.fetch_add(1);
    version_.fetch_add(1);
}

bool Catalog::sessionBlocked(const std::string& pathUtf8, std::string* reason) const
{
    std::lock_guard lk(mutex_);
    for (const BlockedPlugin& b : sessionBlocked_)
        if (b.path == pathUtf8) {
            if (reason)
                *reason = b.reason;
            return true;
        }
    return false;
}

// ----------------------------------------------------------------------------------------------- cache

void Catalog::loadCache()
{
    if (cacheLoaded_)
        return;
    cacheLoaded_ = true;
    if (!useCache_)
        return;
    fs::path path;
    {
        std::lock_guard lk(mutex_);
        if (cachePath_.empty())
            cachePath_ = paths::localDataDir() / L"ffgl-cache.json";
        path = cachePath_;
    }
    std::ifstream in(path);
    if (!in)
        return;
    try {
        const Json j = Json::parse(in);
        if (!j.is_object() || j.value("version", 0) != 1 || !j.contains("files") || !j["files"].is_array())
            return;
        std::map<std::string, CacheRecord> loaded;
        for (const Json& f : j["files"]) {
            if (!f.is_object() || !f.contains("path"))
                continue;
            CacheRecord r;
            r.size = f.value("size", std::uint64_t{0});
            r.mtime = f.value("mtime", std::int64_t{0});
            r.ok = f.value("ok", false);
            r.error = f.value("error", std::string());
            if (r.ok) {
                auto info = PluginInfo::fromJson(f.value("plugin", Json()));
                if (!info)
                    continue;
                r.info = std::move(*info);
            }
            loaded.emplace(lowerAscii(f["path"].get<std::string>()), std::move(r));
        }
        std::lock_guard lk(mutex_);
        cache_ = std::move(loaded);
    } catch (const Json::exception&) {
        // cache corrompido: reescaneia tudo
    }
}

void Catalog::saveCache() const
{
    if (!useCache_)
        return;
    Json files = Json::array();
    fs::path path;
    {
        std::lock_guard lk(mutex_);
        path = cachePath_;
        for (const auto& [key, r] : cache_) {
            Json f = {{"path", r.ok ? r.info.path : key}, {"size", r.size}, {"mtime", r.mtime}, {"ok", r.ok}};
            if (!r.error.empty())
                f["error"] = r.error;
            if (r.ok)
                f["plugin"] = r.info.toJson();
            files.push_back(std::move(f));
        }
    }
    if (path.empty())
        return;
    const Json j = {{"version", 1}, {"files", std::move(files)}};
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
            return;
        out << j.dump(1, ' ', false, Json::error_handler_t::replace);
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
    }
}

// ----------------------------------------------------------------------------------------------- varredura

ScanResult Catalog::scanOne(const Candidate& c)
{
    ScanResult res;
    const std::wstring exe = media::workerExePath();
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) {
        res.error = "bslstage_worker.exe não encontrado";
        return res;
    }

    wchar_t tmpDir[MAX_PATH];
    const DWORD tn = GetTempPathW(MAX_PATH, tmpDir);
    static std::atomic<unsigned> counter{0};
    const fs::path outFile = fs::path(std::wstring(tmpDir, tn))
                             / (L"bslstage-ffgl-scan-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                                + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(counter.fetch_add(1)) + L".json");

    std::wstring cmd = L"\"" + exe + L"\" --scan-ffgl \"" + c.path.wstring() + L"\" \"" + outFile.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring dir = fs::path(exe).parent_path().wstring();
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        dir.c_str(), &si, &pi)) {
        res.error = "não foi possível iniciar o processo de varredura (erro " + std::to_string(GetLastError()) + ")";
        return res;
    }
    if (HANDLE job = media::workerJob())
        AssignProcessToJobObject(job, pi.hProcess);
    currentProc_ = pi.hProcess;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    const DWORD waited = WaitForSingleObject(pi.hProcess, timeoutMs_);
    DWORD code = 0;
    bool timedOut = false;
    if (waited != WAIT_OBJECT_0) {
        timedOut = true;
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    currentProc_ = nullptr;
    CloseHandle(pi.hProcess);

    std::string json;
    {
        std::ifstream in(outFile, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            json = ss.str();
        }
    }
    fs::remove(outFile, ec);

    if (!json.empty()) {
        try {
            const Json j = Json::parse(json);
            ScanResult r = ScanResult::fromJson(j);
            if (r.ok || !r.error.empty())
                return r; // o worker respondeu, mesmo que tenha demorado para descarregar
        } catch (const Json::exception&) {
        }
    }
    if (stop_.load()) {
        res.error = "varredura cancelada";
    } else if (timedOut) {
        res.error = "não respondeu em " + std::to_string(timeoutMs_ / 1000) + " s (travou ao carregar ou ao ler o plugin)";
    } else if (code >= 0xC0000000u) {
        res.error = "o plugin derrubou o processo de varredura: exceção " + exceptionText(code);
    } else {
        res.error = "o processo de varredura terminou sem resultado (código " + std::to_string(code) + ")";
    }
    return res;
}

void Catalog::publish(std::vector<EntryPtr> entries, std::vector<BlockedPlugin> blocked)
{
    {
        std::lock_guard lk(mutex_);
        entries_ = std::move(entries);
        blocked_ = std::move(blocked);
    }
    version_.fetch_add(1);
}

void Catalog::runScan()
{
    for (;;) {
        restart_ = false;
        loadCache();

        // --- 1. DLLs candidatas nas pastas (so o cabecalho PE; nada e carregado) ---
        std::vector<Folder> folders;
        {
            std::lock_guard lk(mutex_);
            folders = folders_;
        }
        std::vector<Candidate> candidates;
        std::set<std::string> seen;
        int ignored = 0;
        bool webBrowser = false;
        for (const Folder& folder : folders) {
            std::error_code ec;
            if (!fs::is_directory(folder.path, ec))
                continue;
            fs::recursive_directory_iterator it(folder.path, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            for (; !ec && it != end && !stop_.load(); it.increment(ec)) {
                if (it.depth() >= 8)
                    it.disable_recursion_pending();
                std::error_code fec;
                if (it->is_directory(fec)) {
                    // A pasta do Chromium do WebBrowser (centenas de MB de DLLs que nao sao plugins) nunca e percorrida
                    if (lowerAscii(utf8(it->path().filename())) == "webbrowser.cef")
                        it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(fec) || !hasDllExtension(it->path()))
                    continue;
                if (lowerAscii(utf8(it->path().filename())) == "webbrowser.dll") {
                    webBrowser = true; // nao e carregado: o BSL Stage tem a fonte Web nativa
                    continue;
                }
                const std::string key = lowerAscii(utf8(it->path()));
                if (!seen.insert(key).second)
                    continue; // a mesma DLL por duas pastas
                if (inspectPe(it->path()) != PeVerdict::Candidate) {
                    ++ignored;
                    continue;
                }
                Candidate c;
                c.path = it->path();
                c.pathUtf8 = utf8(it->path());
                c.origin = folder.label;
                if (!statFile(c.path, c.size, c.mtime))
                    continue;
                candidates.push_back(std::move(c));
                if (candidates.size() >= 1000)
                    break;
            }
        }
        ignored_ = ignored;
        webBrowser_ = webBrowser;

        // --- 2. cache x varredura ---
        std::vector<char> done(candidates.size(), 0); // 1 = resolvido pelo cache
        std::vector<std::string> retries;
        {
            std::lock_guard lk(mutex_);
            retries = retryPaths_;
            retryPaths_.clear();
        }
        struct Outcome
        {
            bool known = false; // tem resultado (cache ou varredura)
            bool ok = false;
            std::string error;
            PluginInfo info;
        };
        std::vector<Outcome> outcomes(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) {
            const Candidate& c = candidates[i];
            const std::string key = lowerAscii(c.pathUtf8);
            if (std::find(retries.begin(), retries.end(), key) != retries.end())
                continue;
            std::lock_guard lk(mutex_);
            const auto it = cache_.find(key);
            if (it != cache_.end() && it->second.size == c.size && it->second.mtime == c.mtime) {
                outcomes[i] = {true, it->second.ok, it->second.error, it->second.info};
                done[i] = 1;
            }
        }

        auto snapshot = [&] {
            std::vector<EntryPtr> entries;
            std::vector<BlockedPlugin> blocked;
            std::set<std::string> typeIds;
            for (size_t i = 0; i < candidates.size(); ++i) {
                const Outcome& o = outcomes[i];
                if (!o.known)
                    continue;
                if (o.ok) {
                    EntryPtr e = makeEntry(o.info, candidates[i].origin);
                    if (typeIds.insert(e->typeId).second)
                        entries.push_back(std::move(e));
                } else {
                    BlockedPlugin b;
                    b.path = candidates[i].pathUtf8;
                    b.fileName = utf8(candidates[i].path.filename());
                    b.origin = candidates[i].origin;
                    b.reason = o.error;
                    blocked.push_back(std::move(b));
                }
            }
            publish(std::move(entries), std::move(blocked));
        };
        snapshot();

        // --- 3. varre o que mudou (um processo por DLL) ---
        size_t pending = 0;
        for (char d : done)
            pending += d ? 0 : 1;
        size_t index = 0;
        bool changed = false;
        for (size_t i = 0; i < candidates.size() && !stop_.load() && !restart_.load(); ++i) {
            if (done[i])
                continue;
            ++index;
            {
                std::lock_guard lk(mutex_);
                status_ = "Varrendo plugin " + std::to_string(index) + " de " + std::to_string(pending) + "…";
            }
            const Candidate& c = candidates[i];
            ScanResult r = scanOne(c);
            if (stop_.load())
                break;
            if (r.ok) {
                r.info.path = c.pathUtf8;
                r.info.size = c.size;
                r.info.mtime = c.mtime;
                if (r.info.fileName.empty())
                    r.info.fileName = utf8(c.path.filename());
            } else {
                log::warn("Plugin FFGL bloqueado (" + utf8(c.path.filename()) + "): " + r.error);
            }
            outcomes[i] = {true, r.ok, r.error, r.info};
            // Arquivo que nao deu para ler (preso por outro programa): bloqueia agora, mas nao grava no cache
            const bool transient = !r.ok && r.error.find("ilegível") != std::string::npos;
            if (!transient) {
                std::lock_guard lk(mutex_);
                CacheRecord rec;
                rec.size = c.size;
                rec.mtime = c.mtime;
                rec.ok = r.ok;
                rec.error = r.error;
                rec.info = r.info;
                cache_[lowerAscii(c.pathUtf8)] = std::move(rec);
            }
            changed = true;
            snapshot();
        }
        if (changed)
            saveCache();

        std::lock_guard lk(mutex_);
        if (stop_.load() || !restart_.load()) {
            scanning_ = false;
            status_.clear();
            return;
        }
    }
}

} // namespace bsl::ffgl
