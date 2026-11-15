#include "update/Apply.h"

#include "update/Zip.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <vector>

namespace bsl::update {

namespace fs = std::filesystem;

namespace {

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path pathFromUtf8(const std::string& utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

struct Logger
{
    const ApplyOptions& o;
    void operator()(const std::string& s) const
    {
        if (o.log)
            o.log(s);
    }
};

template <class Fn>
bool retrying(int tries, int delayMs, Fn&& fn)
{
    for (int i = 0; i < std::max(1, tries); ++i) {
        if (fn())
            return true;
        if (i + 1 < tries)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return false;
}

bool copyOver(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool moveOver(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
#else
    fs::rename(from, to, ec);
    if (!ec)
        return true;
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return false;
    fs::remove(from, ec);
    return true;
#endif
}

struct JournalEntry
{
    std::string rel;
    fs::path target;
    fs::path backup;
    bool hadOld = false;
};

}

bool isProtectedUserFile(const std::string& relativeName)
{
    const std::string n = lowerAscii(relativeName);
    const size_t slash = n.find_last_of('/');
    const std::string file = slash == std::string::npos ? n : n.substr(slash + 1);
    if (file == "settings.json" || file == "imgui.ini" || file == "bslstage.log" || file == "ffgl-cache.json")
        return true;
    const size_t dot = file.find_last_of('.');
    const std::string ext = dot == std::string::npos ? std::string() : file.substr(dot);
    if (ext == ".bslstage" || ext == ".bslkeys")
        return true;
    for (const char* dir : {"projects/", "projetos/", "user/", "usuario/"})
        if (n.rfind(dir, 0) == 0)
            return true;
    return false;
}

bool isDirectoryWritable(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return false;
#ifdef _WIN32
    const fs::path probe = dir / (L".bslstage-write-test-" + std::to_wstring(GetCurrentProcessId()));
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
#else
    return access(dir.c_str(), W_OK) == 0;
#endif
    return true;
}

ApplyResult applyZipUpdate(const ApplyOptions& o)
{
    ApplyResult result;
    const Logger log{o};
    auto progress = [&](double f, const char* phase) {
        if (o.progress)
            o.progress(std::clamp(f, 0.0, 1.0), phase);
    };
    std::error_code ec;

    ZipReader zip;
    std::string err;
    if (!zip.open(o.zipFile, &err)) {
        result.error = "pacote inválido: " + err;
        return result;
    }
    std::string prefix;
    bool foundExe = false;
    for (const ZipEntry& e : zip.entries()) {
        if (!isSafeZipPath(e.name)) {
            result.error = "pacote recusado: nome inseguro \"" + e.name + "\"";
            return result;
        }
        const std::string n = lowerAscii(e.name);
        if (!e.isDirectory && n == "bslstage.exe") {
            prefix.clear();
            foundExe = true;
        } else if (!e.isDirectory && !foundExe && n.size() > 13 && n.compare(n.size() - 13, 13, "/bslstage.exe") == 0
                   && std::count(n.begin(), n.end(), '/') == 1) {
            prefix = e.name.substr(0, e.name.size() - 12);
            foundExe = true;
        }
    }
    if (!foundExe) {
        result.error = "pacote recusado: não contém o BSLStage.exe";
        return result;
    }

    fs::remove_all(o.stagingDir, ec);
    fs::create_directories(o.stagingDir, ec);
    struct Staged
    {
        std::string rel;
        fs::path path;
        uint64_t size = 0;
    };
    std::vector<Staged> staged;
    uint64_t totalBytes = 0, doneBytes = 0;
    for (const ZipEntry& e : zip.entries())
        if (!e.isDirectory && e.name.rfind(prefix, 0) == 0)
            totalBytes += e.size;
    for (const ZipEntry& e : zip.entries()) {
        if (e.isDirectory || e.name.rfind(prefix, 0) != 0)
            continue;
        const std::string rel = e.name.substr(prefix.size());
        if (rel.empty())
            continue;
        const fs::path dest = o.stagingDir / pathFromUtf8(rel);
        if (!zip.extract(e, dest, &err, [&](uint64_t) { return true; })) {
            result.error = "pacote inválido: " + err;
            fs::remove_all(o.stagingDir, ec);
            return result;
        }
        doneBytes += e.size;
        progress(totalBytes ? 0.7 * static_cast<double>(doneBytes) / static_cast<double>(totalBytes) : 0.7, "Preparando");
        staged.push_back({rel, dest, e.size});
    }
    log("pacote extraído: " + std::to_string(staged.size()) + " arquivos");

    fs::remove_all(o.backupDir, ec);
    fs::create_directories(o.backupDir, ec);
    std::vector<JournalEntry> journal;
    std::string failure;
    int index = 0;
    for (const Staged& s : staged) {
        const int myIndex = index++;
        progress(0.7 + 0.3 * static_cast<double>(myIndex) / static_cast<double>(std::max<size_t>(1, staged.size())), "Instalando");
        if (isProtectedUserFile(s.rel)) {
            ++result.skipped;
            log("preservado (arquivo do usuário): " + s.rel);
            continue;
        }
        if (o.failHook && o.failHook(myIndex, s.rel)) {
            failure = "falha simulada em " + s.rel;
            break;
        }
        JournalEntry j;
        j.rel = s.rel;
        j.target = o.targetDir / pathFromUtf8(s.rel);
        j.backup = o.backupDir / pathFromUtf8(s.rel);
        j.hadOld = fs::is_regular_file(j.target, ec);
        if (j.hadOld) {
            if (!retrying(o.lockRetries, o.lockRetryDelayMs, [&] { return copyOver(j.target, j.backup); })) {
                failure = "não foi possível guardar o backup de " + s.rel + " (arquivo em uso?)";
                break;
            }
        }
        journal.push_back(j);
        if (!retrying(o.lockRetries, o.lockRetryDelayMs, [&] { return moveOver(s.path, j.target); })) {
            failure = "não foi possível substituir " + s.rel + " (arquivo em uso ou sem permissão)";
            break;
        }
        std::error_code sec;
        if (fs::file_size(j.target, sec) != s.size || sec) {
            failure = "o arquivo " + s.rel + " ficou com tamanho errado depois de copiado";
            break;
        }
        if (j.hadOld)
            ++result.replaced;
        else
            ++result.added;
    }

    if (!failure.empty()) {
        log("FALHA: " + failure + "; restaurando a versão anterior");
        std::string rollbackErrors;
        for (auto it = journal.rbegin(); it != journal.rend(); ++it) {
            bool ok;
            if (it->hadOld) {
                ok = retrying(o.lockRetries, o.lockRetryDelayMs, [&] { return copyOver(it->backup, it->target); });
            } else {
                ok = retrying(o.lockRetries, o.lockRetryDelayMs, [&] {
                    std::error_code rec;
                    fs::remove(it->target, rec);
                    return !fs::exists(it->target, rec);
                });
            }
            if (!ok)
                rollbackErrors += (rollbackErrors.empty() ? "" : "; ") + it->rel;
        }
        result.rolledBack = true;
        result.rollbackFailed = !rollbackErrors.empty();
        result.error = failure;
        if (result.rollbackFailed) {
            result.error += ". Não foi possível restaurar: " + rollbackErrors + ". A cópia de segurança está em "
                            + o.backupDir.string();
        }
        result.replaced = result.added = 0;
        fs::remove_all(o.stagingDir, ec);
        return result;
    }

    fs::remove_all(o.stagingDir, ec);
    progress(1.0, "Concluído");
    log("atualização aplicada: " + std::to_string(result.replaced) + " trocados, " + std::to_string(result.added) + " novos, "
        + std::to_string(result.skipped) + " preservados");
    result.ok = true;
    return result;
}

}
