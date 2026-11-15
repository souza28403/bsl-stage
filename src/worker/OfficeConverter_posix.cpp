#include "worker/OfficeConverter.h"

#include "platform/Env.h"
#include "platform/Paths.h"
#include "worker/WorkerUtil.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif

#include <chrono>
#include <thread>
#include <vector>

namespace bsl::worker {

namespace fs = std::filesystem;

namespace {

constexpr const char* kNoOfficeMessage = "LibreOffice não encontrado: instale o LibreOffice (gratuito) para abrir este arquivo";

char** currentEnviron()
{
#ifdef __APPLE__
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

std::string cacheKey(const fs::path& source)
{
    std::error_code ec;
    const fs::path abs = fs::absolute(source, ec).lexically_normal();
    const auto size = fs::file_size(abs, ec);
    const auto mtime = fs::last_write_time(abs, ec).time_since_epoch().count();
    return pathToUtf8(abs) + "|" + std::to_string(static_cast<unsigned long long>(size)) + "|"
           + std::to_string(static_cast<long long>(mtime));
}

fs::path cacheRoot()
{
    return bslplat::cacheDir() / "docs";
}

class FileLock
{
public:
    FileLock(const fs::path& file, unsigned timeoutMs)
    {
        fd_ = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0)
            return;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            if (flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                held_ = true;
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ~FileLock()
    {
        if (fd_ >= 0) {
            if (held_)
                flock(fd_, LOCK_UN);
            ::close(fd_);
        }
    }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    int fd_ = -1;
    bool held_ = false;
};

fs::path findInPath(const char* name)
{
    const std::string path = envUtf8("PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(':', start);
        if (end == std::string::npos)
            end = path.size();
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            const fs::path candidate = fs::path(dir) / name;
            if (::access(candidate.c_str(), X_OK) == 0) {
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec))
                    return candidate;
            }
        }
        start = end + 1;
    }
    return {};
}

fs::path findSoffice()
{
    for (const char* name : {"soffice", "libreoffice"}) {
        const fs::path found = findInPath(name);
        if (!found.empty())
            return found;
    }
    for (const char* fixed : {"/usr/lib/libreoffice/program/soffice", "/opt/libreoffice/program/soffice",
                              "/Applications/LibreOffice.app/Contents/MacOS/soffice"}) {
        std::error_code ec;
        if (fs::is_regular_file(fixed, ec))
            return fixed;
    }
    return {};
}

std::string convertWithLibreOffice(const fs::path& soffice, const fs::path& source, const fs::path& outPdf)
{
    std::error_code ec;
    const fs::path tmpDir = outPdf.parent_path() / (outPdf.stem().string() + ".lo");
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    const fs::path profile = cacheRoot().parent_path() / "lo-profile";
    fs::create_directories(profile, ec);

    const std::string exe = soffice.string();
    const std::string profileArg = "-env:UserInstallation=file://" + profile.string();
    const std::string outDir = tmpDir.string();
    const std::string in = source.string();
    std::vector<char*> argv = {const_cast<char*>(exe.c_str()),
                               const_cast<char*>("--headless"),
                               const_cast<char*>("--norestore"),
                               const_cast<char*>("--nolockcheck"),
                               const_cast<char*>("--nologo"),
                               const_cast<char*>(profileArg.c_str()),
                               const_cast<char*>("--convert-to"),
                               const_cast<char*>("pdf"),
                               const_cast<char*>("--outdir"),
                               const_cast<char*>(outDir.c_str()),
                               const_cast<char*>(in.c_str()),
                               nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    pid_t pid = 0;
    const int spawnErr = posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), currentEnviron());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (spawnErr != 0) {
        fs::remove_all(tmpDir, ec);
        return "não foi possível iniciar o LibreOffice (erro " + std::to_string(spawnErr) + ")";
    }

    std::string error;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    int status = 0;
    for (;;) {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (r < 0 && errno != EINTR)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            error = "o LibreOffice demorou demais para converter";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const fs::path produced = tmpDir / (source.stem().string() + ".pdf");
    if (error.empty()) {
        if (fs::is_regular_file(produced, ec))
            fs::rename(produced, outPdf, ec);
        if (ec || !fs::is_regular_file(outPdf, ec))
            error = "o LibreOffice não gerou o PDF";
    }
    fs::remove_all(tmpDir, ec);
    return error;
}

}

fs::path docCachePath(const fs::path& source)
{
    return cacheRoot() / (fnv1aHex(cacheKey(source)) + ".pdf");
}

ConvertResult convertToPdf(const fs::path& source)
{
    ConvertResult r;
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        r.error = "arquivo não encontrado";
        return r;
    }
    const fs::path target = docCachePath(source);
    fs::create_directories(target.parent_path(), ec);

    auto cached = [&] { return fs::is_regular_file(target, ec) && fs::file_size(target, ec) > 0; };
    auto fromCache = [&] {
        r.ok = true;
        r.pdf = target;
        r.fromCache = true;
        r.via = "cache";
        return r;
    };
    if (cached())
        return fromCache();

    const fs::path soffice = findSoffice();
    if (soffice.empty()) {
        r.error = kNoOfficeMessage;
        return r;
    }

    FileLock lock(target.parent_path() / ".convert.lock", 240000);
    if (cached())
        return fromCache();

    fs::path tmp = target;
    tmp.replace_extension(".tmp.pdf");
    fs::remove(tmp, ec);

    const std::string error = convertWithLibreOffice(soffice, source, tmp);
    if (error.empty()) {
        fs::rename(tmp, target, ec);
        if (!ec) {
            r.ok = true;
            r.pdf = target;
            r.via = "LibreOffice";
            return r;
        }
        r.error = "Não foi possível converter o arquivo (LibreOffice: não foi possível gravar o PDF no cache)";
    } else {
        r.error = "Não foi possível converter o arquivo (LibreOffice: " + error + ")";
    }
    fs::remove(tmp, ec);
    return r;
}

}
