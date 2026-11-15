#include "media/DocThumb.h"

#include "media/DocInfo.h"
#include "media/MediaFormats.h"
#include "media/WorkerProcess.h"
#include "media/WorkerProtocol.h"
#include "platform/ExePath.h"
#include "platform/Utf.h"

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <thread>
#include <vector>

namespace bsl::media {

std::wstring workerExePath()
{
    return bslplat::utf8ToWide((bslplat::exeDir() / bslplat::wideToUtf8(wp::kWorkerExeName)).string());
}

void* workerJob()
{
    return nullptr;
}

namespace {

std::atomic<bool>& cancelFlag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

char** currentEnviron()
{
#ifdef __APPLE__
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

bool waitChild(pid_t pid, std::chrono::milliseconds limit, bool stopOnCancel, int* status)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    for (;;) {
        const pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid)
            return true;
        if (r < 0 && errno != EINTR) {
            *status = -1;
            return true;
        }
        if (stopOnCancel && cancelFlag().load())
            return false;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

}

void cancelDocThumbnails()
{
    cancelFlag() = true;
}

bool renderDocThumbnail(const std::filesystem::path& fileIn, int w, int h, Frame& out, int* pages, std::string* error)
{
    namespace fs = std::filesystem;
    std::error_code absEc;
    const fs::path file = fs::absolute(fileIn, absEc);
    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg;
        return false;
    };
    const std::string exe = bslplat::wideToUtf8(workerExePath());
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec))
        return fail("bslstage_worker não encontrado");

    static std::atomic<unsigned> counter{0};
    const fs::path outFile = fs::temp_directory_path(ec)
                             / ("bslstage-thumb-" + std::to_string(static_cast<long>(getpid())) + "-"
                                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
                                + std::to_string(counter.fetch_add(1)) + ".bin");

    const std::string fileText = file.string();
    const std::string outText = outFile.string();
    const std::string wText = std::to_string(w);
    const std::string hText = std::to_string(h);
    std::vector<char*> argv = {const_cast<char*>(exe.c_str()),     const_cast<char*>("--thumb"),
                               const_cast<char*>(fileText.c_str()), const_cast<char*>(outText.c_str()),
                               const_cast<char*>(wText.c_str()),    const_cast<char*>(hText.c_str()),
                               nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), currentEnviron()) != 0)
        return fail("não foi possível iniciar o processo auxiliar");

    const std::string ext = lowerExtension(file);
    const auto timeout = std::chrono::milliseconds(ext == "pdf" ? 40000 : 300000);
    int status = 0;
    bool done = waitChild(pid, timeout, true, &status);
    if (!done && cancelFlag().load())
        done = waitChild(pid, std::chrono::milliseconds(5000), false, &status);
    if (!done) {
        kill(pid, SIGKILL);
        waitChild(pid, std::chrono::milliseconds(2000), false, &status);
        fs::remove(outFile, ec);
        return fail("o processo auxiliar demorou demais");
    }
    const int code = status == -1 ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    if (code != 0) {
        fs::remove(outFile, ec);
        return fail("o processo auxiliar falhou (código " + std::to_string(code) + ")");
    }
    std::ifstream in(outFile, std::ios::binary);
    std::int32_t head[3] = {0, 0, 0};
    if (!in || !in.read(reinterpret_cast<char*>(head), sizeof(head)) || head[0] != w || head[1] != h) {
        in.close();
        fs::remove(outFile, ec);
        return fail("miniatura inválida");
    }
    out.allocate(w, h, PixelFormat::RGBA8);
    in.read(reinterpret_cast<char*>(out.planes[0].data()), static_cast<std::streamsize>(out.planes[0].size()));
    const bool ok = static_cast<size_t>(in.gcount()) == out.planes[0].size();
    in.close();
    fs::remove(outFile, ec);
    if (!ok)
        return fail("miniatura incompleta");
    out.colorSpace = ColorSpace::BT709;
    out.range = ColorRange::Full;
    if (pages)
        *pages = head[2];
    setDocPageCount(file, head[2]);
    return true;
}

}
