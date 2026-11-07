#include "update/Service.h"

#include "update/Apply.h"
#include "update/Sha256.h"
#include "update/Zip.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <sstream>

namespace bsl::update {

namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& file, size_t maxBytes = 4u << 20)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    if (s.size() > maxBytes)
        s.clear();
    return s;
}

void writeText(const fs::path& file, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out)
        out << text;
}

fs::path exePath()
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n));
}

std::string lowerStr(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}

bool installedByMsi()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\BSL Light\\BSL Stage", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buf[32]{};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG r = RegQueryValueExW(key, L"InstalledBy", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(buf, L"msi") == 0;
}

Service::Service() = default;

Service::~Service()
{
    shutdown();
}

void Service::configure(std::string currentVersion, std::string repo, fs::path updatesDir)
{
    currentText_ = std::move(currentVersion);
    current_ = parseSemVer(currentText_);
    repo_ = std::move(repo);
    dir_ = std::move(updatesDir);
}

void Service::setHttp(std::unique_ptr<IHttp> http)
{
    http_ = std::move(http);
}

void Service::joinWorker()
{
    if (worker_.joinable())
        worker_.join();
}

void Service::shutdown()
{
    cancel_ = true;
    joinWorker();
}

std::string Service::pendingPath() const
{
    return (dir_ / L"pending.json").string();
}

void Service::adopt(const Release& release, const Prefs& prefs)
{
    (void)prefs;
    st_.release = release;
    st_.hasUpdate = true;
    const AssetChoice c = chooseAsset(release, installedByMsi_);
    st_.assetKind = c.kind;
    st_.assetName = c.asset ? c.asset->name : std::string();
    st_.assetSize = c.asset ? c.asset->size : 0;
}

void Service::loadPersistedState(const Prefs& prefs)
{
    std::error_code ec;
    fs::remove_all(dir_ / L"run", ec);

    Release cached;
    const std::string raw = readText(dir_ / L"last-release.json");
    bool haveCached = !raw.empty() && parseRelease(raw, cached) && isUpdateFor(prefs, cached, current_);
    if (!raw.empty() && !haveCached)
        fs::remove(dir_ / L"last-release.json", ec);

    std::lock_guard lk(m_);
    if (haveCached) {
        adopt(cached, prefs);
        st_.phase = Phase::Available;
    }

    const std::string pend = readText(dir_ / L"pending.json");
    if (pend.empty())
        return;
    try {
        const nlohmann::json j = nlohmann::json::parse(pend);
        const std::string tag = j.value("tag", "");
        const std::string fileUtf8 = j.value("file", "");
        const fs::path file = fs::path(std::u8string(fileUtf8.begin(), fileUtf8.end()));
        const SemVer v = parseSemVer(tag);
        const int64_t size = j.value("size", static_cast<int64_t>(-1));
        if (v.valid && isNewer(v, current_) && fs::is_regular_file(file, ec) && static_cast<int64_t>(fs::file_size(file, ec)) == size
            && haveCached && parseSemVer(cached.tag).valid && compare(parseSemVer(cached.tag), v) == 0) {
            st_.phase = Phase::Ready;
            st_.stagedFile = file;
            st_.checksumVerified = j.value("verified", false);
            st_.sha256 = j.value("sha256", "");
            const std::string kind = j.value("kind", "zip");
            st_.assetKind = kind == "msi" ? AssetKind::Msi : kind == "installer" ? AssetKind::Installer : AssetKind::Zip;
            if ((st_.assetKind == AssetKind::Msi) != installedByMsi_) {
                st_.phase = Phase::Available;
                st_.stagedFile.clear();
                fs::remove(dir_ / L"pending.json", ec);
                return;
            }
            return;
        }
    } catch (const nlohmann::json::exception&) {
    }
    fs::remove(dir_ / L"pending.json", ec);
    fs::remove_all(dir_ / L"downloads", ec);
}

std::string Service::takeLastUpdateMessage(bool* ok)
{
    const fs::path file = dir_ / L"last-result.json";
    const std::string text = readText(file, 1 << 16);
    if (text.empty())
        return {};
    std::error_code ec;
    fs::remove(file, ec);
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        const bool good = j.value("ok", false);
        if (ok)
            *ok = good;
        const std::string to = j.value("to", "");
        if (good) {
            fs::remove_all(dir_ / L"downloads", ec);
            fs::remove(dir_ / L"pending.json", ec);
            fs::remove(dir_ / L"last-release.json", ec);
            return "BSL Stage atualizado para a versão " + to + ".";
        }
        fs::remove(dir_ / L"pending.json", ec);
        return "A atualização para a versão " + to + " não foi concluída e a versão anterior foi mantida: " + j.value("error", "erro desconhecido");
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

void Service::startCheck(const Prefs& prefs, bool manual)
{
    if (worker_busy_)
        return;
    joinWorker();
    {
        std::lock_guard lk(m_);
        if (st_.phase == Phase::Downloading || st_.phase == Phase::ConfirmNoChecksum || st_.phase == Phase::Ready)
            return;
        st_.checking = true;
        if (manual) {
            st_.checkError.clear();
            st_.upToDate = false;
        }
    }
    cancel_ = false;
    worker_busy_ = true;
    worker_ = std::thread([this, prefs, manual] {
        runCheck(prefs, manual);
        worker_busy_ = false;
    });
}

void Service::runCheck(Prefs prefs, bool manual)
{
    if (!http_)
        http_ = makeWinHttp("BSL-Stage/" + currentText_);
    CheckOutcome o;
    o.manual = manual;
    o.checkedUnix = static_cast<int64_t>(std::time(nullptr));

    const std::string base = "https://api.github.com/repos/" + repo_ + "/releases";
    const std::string url = prefs.beta ? base + "?per_page=10" : base + "/latest";
    const HttpResponse r = http_->get(url, 8000, 2u << 20);

    Release found;
    bool haveRelease = false;
    std::string note;
    if (r.status == 404) {
        note = "Nenhuma versão publicada em github.com/" + repo_ + " ainda.";
        o.ok = true;
    } else if (!r.ok()) {
        o.ok = false;
        o.offline = r.offline;
        o.error = r.error.empty() ? "falha desconhecida" : r.error;
    } else {
        std::string perr;
        if (prefs.beta) {
            std::vector<Release> list;
            if (parseReleaseList(r.body, list, &perr)) {
                const int idx = pickNewest(list, true);
                if (idx >= 0) {
                    found = list[static_cast<size_t>(idx)];
                    haveRelease = true;
                }
                o.ok = true;
            } else {
                o.error = "resposta inesperada do GitHub (" + perr + ")";
            }
        } else if (parseRelease(r.body, found, &perr)) {
            haveRelease = !found.draft && found.version.valid;
            o.ok = true;
        } else {
            o.error = "resposta inesperada do GitHub (" + perr + ")";
        }
    }

    {
        std::lock_guard lk(m_);
        st_.checking = false;
        if (!o.ok) {
            st_.checkError = o.error;
            st_.checkOffline = o.offline;
        } else {
            st_.checkError.clear();
            st_.checkOffline = false;
            st_.checkNote = note;
            std::error_code ec;
            if (haveRelease && isUpdateFor(prefs, found, current_)) {
                adopt(found, prefs);
                st_.phase = Phase::Available;
                st_.upToDate = false;
                st_.error.clear();
                writeText(dir_ / L"last-release.json", found.raw);
            } else {
                st_.hasUpdate = false;
                st_.release = Release{};
                st_.phase = Phase::Idle;
                st_.upToDate = note.empty();
                fs::remove(dir_ / L"last-release.json", ec);
            }
        }
        outcome_ = o;
        outcomePending_ = true;
    }
}

bool Service::takeCheckOutcome(CheckOutcome& out)
{
    std::lock_guard lk(m_);
    if (!outcomePending_)
        return false;
    out = outcome_;
    outcomePending_ = false;
    return true;
}

void Service::setError(const std::string& text)
{
    std::lock_guard lk(m_);
    st_.error = text;
    st_.phase = Phase::Available;
    st_.verifying = false;
}

void Service::startDownload()
{
    if (worker_busy_)
        return;
    Release rel;
    AssetChoice choice;
    {
        std::lock_guard lk(m_);
        if (st_.phase != Phase::Available || !st_.hasUpdate)
            return;
        rel = st_.release;
        choice = chooseAsset(rel, installedByMsi_);
        if (!choice.asset) {
            st_.error = installedByMsi_ ? "Esta versão não traz o instalador (.msi). Abra a página da versão no GitHub."
                                        : "Esta versão não traz um arquivo para Windows. Abra a página da versão no GitHub.";
            return;
        }
        st_.phase = Phase::Downloading;
        st_.error.clear();
        st_.done = 0;
        st_.total = choice.asset->size;
        st_.verifying = false;
    }
    joinWorker();
    cancel_ = false;
    worker_busy_ = true;
    worker_ = std::thread([this, rel = std::move(rel)]() mutable {
        const AssetChoice c = chooseAsset(rel, installedByMsi_);
        runDownload(rel, c);
        worker_busy_ = false;
    });
}

void Service::runDownload(Release release, AssetChoice choice)
{
    if (!http_)
        http_ = makeWinHttp("BSL-Stage/" + currentText_);
    std::error_code ec;
    const Asset& asset = *choice.asset;
    const std::string fileName = safeFileName(asset.name);
    if (fileName.empty() || !isTrustedDownloadUrl(asset.url)) {
        setError("O endereço do arquivo não é confiável; a atualização foi recusada.");
        return;
    }
    const fs::path folder = dir_ / L"downloads" / fs::path(std::u8string(release.tag.begin(), release.tag.end())).filename();
    const fs::path finalFile = folder / fs::path(std::u8string(fileName.begin(), fileName.end()));
    const fs::path part = fs::path(finalFile.wstring() + L".part");
    fs::create_directories(folder, ec);

    std::string expected;
    bool haveChecksums = false;
    if (const Asset* sums = findChecksumsAsset(release); sums && isTrustedDownloadUrl(sums->url)) {
        const HttpResponse sr = http_->get(sums->url, 10000, 1u << 20);
        if (sr.ok()) {
            const auto table = parseChecksums(sr.body);
            const auto it = table.find(lowerStr(fileName));
            if (it != table.end()) {
                expected = it->second;
                haveChecksums = true;
            }
        } else if (sr.offline) {
            setError("Sem conexão com o GitHub: " + sr.error);
            return;
        }
    }
    if (cancel_) {
        setError({});
        return;
    }

    const HttpResponse dr = http_->download(asset.url, part, [&](int64_t done, int64_t total) {
        {
            std::lock_guard lk(m_);
            st_.done = done;
            if (total > 0)
                st_.total = total;
        }
        return !cancel_.load();
    });
    if (!dr.ok()) {
        fs::remove(part, ec);
        setError(dr.cancelled ? std::string() : "Falha no download: " + dr.error);
        return;
    }

    {
        std::lock_guard lk(m_);
        st_.verifying = true;
    }
    std::string actual;
    if (!sha256File(part, actual, [&](uint64_t) { return !cancel_.load(); })) {
        fs::remove(part, ec);
        setError(cancel_ ? std::string() : "Não foi possível calcular o SHA-256 do arquivo baixado.");
        return;
    }
    if (haveChecksums && actual != expected) {
        fs::remove(part, ec);
        setError("A verificação de integridade falhou: o SHA-256 do arquivo baixado não confere com o SHA256SUMS.txt do release. "
                 "O arquivo foi descartado.");
        return;
    }
    fs::remove(finalFile, ec);
    fs::rename(part, finalFile, ec);
    if (ec) {
        setError("Não foi possível guardar o arquivo baixado: " + ec.message());
        return;
    }

    if (choice.kind == AssetKind::Zip) {
        ZipReader zr;
        std::string zerr;
        bool hasExe = false;
        if (zr.open(finalFile, &zerr)) {
            for (const ZipEntry& e : zr.entries()) {
                const std::string n = lowerStr(e.name);
                if (n == "bslstage.exe" || (n.size() > 13 && n.compare(n.size() - 13, 13, "/bslstage.exe") == 0))
                    hasExe = true;
            }
        }
        if (!hasExe) {
            fs::remove(finalFile, ec);
            setError("O arquivo baixado não parece um pacote do BSL Stage (sem BSLStage.exe).");
            return;
        }
    }

    if (choice.kind == AssetKind::Msi) {
        static const unsigned char kOle[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        unsigned char head[8]{};
        bool valid = false;
        {
            std::ifstream in(finalFile, std::ios::binary);
            in.read(reinterpret_cast<char*>(head), sizeof(head));
            valid = static_cast<bool>(in) && std::memcmp(head, kOle, sizeof(kOle)) == 0;
        }
        if (!valid) {
            fs::remove(finalFile, ec);
            setError("O arquivo baixado não parece um instalador do BSL Stage (.msi inválido).");
            return;
        }
    }

    std::lock_guard lk(m_);
    st_.verifying = false;
    st_.sha256 = actual;
    st_.stagedFile = finalFile;
    st_.checksumVerified = haveChecksums;
    st_.assetKind = choice.kind;
    if (haveChecksums) {
        st_.phase = Phase::Ready;
        savePending(st_);
    } else {
        st_.phase = Phase::ConfirmNoChecksum;
    }
}

void Service::savePending(const Status& st)
{
    std::error_code ec;
    nlohmann::json j;
    j["tag"] = st.release.tag;
    j["file"] = std::string(reinterpret_cast<const char*>(st.stagedFile.u8string().c_str()));
    j["size"] = static_cast<int64_t>(fs::file_size(st.stagedFile, ec));
    j["sha256"] = st.sha256;
    j["verified"] = st.checksumVerified;
    j["kind"] = st.assetKind == AssetKind::Msi ? "msi" : st.assetKind == AssetKind::Installer ? "installer" : "zip";
    writeText(dir_ / L"pending.json", j.dump(2));
}

void Service::clearPending(bool deleteFiles)
{
    std::error_code ec;
    fs::remove(dir_ / L"pending.json", ec);
    if (deleteFiles)
        fs::remove_all(dir_ / L"downloads", ec);
}

void Service::cancel()
{
    cancel_ = true;
}

void Service::acceptWithoutChecksum()
{
    std::lock_guard lk(m_);
    if (st_.phase != Phase::ConfirmNoChecksum)
        return;
    st_.phase = Phase::Ready;
    savePending(st_);
}

void Service::discard()
{
    std::lock_guard lk(m_);
    if (st_.phase != Phase::ConfirmNoChecksum && st_.phase != Phase::Ready)
        return;
    clearPending(true);
    st_.phase = st_.hasUpdate ? Phase::Available : Phase::Idle;
    st_.stagedFile.clear();
    st_.checksumVerified = false;
    st_.sha256.clear();
    restart_ = false;
}

Status Service::status() const
{
    std::lock_guard lk(m_);
    Status s = st_;
    s.restartRequested = restart_;
    return s;
}

Brief Service::brief() const
{
    std::lock_guard lk(m_);
    Brief b;
    b.phase = st_.phase;
    b.checking = st_.checking;
    b.hasUpdate = st_.hasUpdate;
    b.version = st_.hasUpdate ? st_.release.version.toString() : std::string();
    if (st_.phase == Phase::Downloading)
        b.progress = st_.verifying ? 1.0 : (st_.total > 0 ? static_cast<double>(st_.done) / static_cast<double>(st_.total) : 0.0);
    return b;
}

bool Service::readyToInstall() const
{
    std::lock_guard lk(m_);
    std::error_code ec;
    return st_.phase == Phase::Ready && fs::is_regular_file(st_.stagedFile, ec);
}

bool Service::launchInstallerOnExit(bool restartAfter, std::string* error)
{
    Status st;
    {
        std::lock_guard lk(m_);
        if (st_.phase != Phase::Ready)
            return false;
        st = st_;
    }
    auto fail = [&](const std::string& text) {
        if (error)
            *error = text;
        return false;
    };
    std::error_code ec;
    const fs::path exe = exePath();
    const fs::path appDir = exe.parent_path();
    if (!fs::is_regular_file(st.stagedFile, ec))
        return fail("o arquivo baixado não existe mais");

    const std::wstring pid = std::to_wstring(GetCurrentProcessId());
    std::wstring args = L"--pid " + pid + L" --work \"" + dir_.wstring() + L"\" --to " + fs::path(st.release.version.toString()).wstring();
    args += L" --from " + fs::path(currentText_).wstring();
    fs::path program;
    if (st.assetKind == AssetKind::Msi) {
        args += L" --msi \"" + st.stagedFile.wstring() + L"\"";
        if (restartAfter)
            args += L" --restart \"" + exe.wstring() + L"\"";
        program = appDir / L"bslstage_updater.exe";
    } else if (st.assetKind == AssetKind::Installer) {
        args += L" --installer \"" + st.stagedFile.wstring() + L"\"";
        program = appDir / L"bslstage_updater.exe";
    } else {
        args += L" --zip \"" + st.stagedFile.wstring() + L"\" --target \"" + appDir.wstring() + L"\"";
        if (restartAfter)
            args += L" --restart \"" + exe.wstring() + L"\"";
        program = appDir / L"bslstage_updater.exe";
    }
    if (!fs::is_regular_file(program, ec))
        return fail("bslstage_updater.exe não foi encontrado ao lado do programa");

    const fs::path runDir = dir_ / L"run";
    fs::create_directories(runDir, ec);
    const fs::path copy = runDir / (L"bslstage_updater_" + pid + L".exe");
    fs::copy_file(program, copy, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return fail("não foi possível preparar o atualizador: " + ec.message());

    const bool needsAdmin = st.assetKind == AssetKind::Zip && !isDirectoryWritable(appDir);
    if (needsAdmin) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = copy.c_str();
        sei.lpParameters = args.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei))
            return fail("o Windows não autorizou a instalação (permissão de administrador)");
        return true;
    }
    std::wstring cmd = L"\"" + copy.wstring() + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                        runDir.c_str(), &si, &pi))
        return fail("não foi possível iniciar o atualizador (erro " + std::to_string(GetLastError()) + ")");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}
