#include "media/WorkerHost.h"

#include "app/Log.h"
#include "media/WorkerProcess.h"
#include "platform/Utf.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace bsl::media {

using Clock = std::chrono::steady_clock;

namespace {

constexpr int kMaxRestarts = 3;
constexpr unsigned kRestartDelaysMs[kMaxRestarts] = {1000, 2000, 4000};
constexpr unsigned kHangTimeoutMs = 12000;
constexpr unsigned kStartTimeoutMs = 20000;
constexpr unsigned kStableMs = 20000;

std::atomic<int> g_hostCounter{0};
std::mutex g_liveMutex;
std::condition_variable g_liveCv;
int g_liveSupervisors = 0;

char** currentEnviron()
{
#ifdef __APPLE__
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

void setupFd(int fd, bool nonBlocking)
{
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
    if (nonBlocking)
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

}

struct WorkerHost::Core : std::enable_shared_from_this<WorkerHost::Core>
{
    struct Run
    {
        int sock = -1;
        pid_t pid = 0;
        bool reaped = false;
        int exitCode = 0;
        bool gotHello = false;
        bool gotFrame = false;
        std::string fatal;
        Clock::time_point started;
    };

    struct Receiver
    {
        void* view = nullptr;
        std::size_t viewBytes = 0;
        int shmW = 0, shmH = 0;
        bool haveFrame = false;
        int page = 0;
        std::uint64_t seq = 0;

        ~Receiver() { reset(); }
        void reset()
        {
            if (view) {
                munmap(view, viewBytes);
                view = nullptr;
                viewBytes = 0;
            }
            haveFrame = false;
        }
    };

    Core()
    {
        int fds[2];
        if (pipe(fds) == 0) {
            wakeRd = fds[0];
            wakeWr = fds[1];
            setupFd(wakeRd, true);
            setupFd(wakeWr, true);
        }
    }

    ~Core()
    {
        if (wakeRd >= 0)
            close(wakeRd);
        if (wakeWr >= 0)
            close(wakeWr);
    }

    int hostId = 0;
    std::atomic<bool> stop{false};
    int wakeRd = -1;
    int wakeWr = -1;

    mutable std::mutex mutex;
    HostSnapshot snap;
    WorkerJson open;
    std::deque<WorkerJson> events;
    std::deque<WorkerJson> outbox;
    std::string transportRequest = "auto";
    std::string workerStatusText;

    std::mutex frameMutex;
    Receiver rx;
    std::atomic<std::uint64_t> frameCounter{0};
    std::atomic<pid_t> processPid{0};

    std::thread thread;

    void wake()
    {
        const char b = 1;
        ssize_t r;
        do {
            r = write(wakeWr, &b, 1);
        } while (r < 0 && errno == EINTR);
    }

    void drainWake()
    {
        char buf[64];
        while (read(wakeRd, buf, sizeof(buf)) > 0) {
        }
    }

    void setState(HostState s, const std::string& text, bool warning = false)
    {
        std::lock_guard lk(mutex);
        snap.state = s;
        snap.text = text;
        snap.warning = warning;
    }

    void pushEvent(WorkerJson ev)
    {
        std::lock_guard lk(mutex);
        events.push_back(std::move(ev));
    }

    bool waitStop(unsigned ms)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        while (!stop.load()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0)
                break;
            pollfd p{wakeRd, POLLIN, 0};
            ::poll(&p, 1, static_cast<int>(left));
            drainWake();
        }
        return stop.load();
    }

    bool reap(Run& run)
    {
        if (run.reaped)
            return true;
        int st = 0;
        const pid_t r = waitpid(run.pid, &st, WNOHANG);
        if (r == run.pid) {
            run.reaped = true;
            run.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
        } else if (r < 0 && errno != EINTR) {
            run.reaped = true;
            run.exitCode = -1;
        }
        if (run.reaped)
            processPid = 0;
        return run.reaped;
    }

    bool waitExit(Run& run, unsigned ms)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            if (reap(run))
                return true;
            if (Clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void supervise();
    std::string runOnce(int attempt, bool& fatal, Clock::duration& lasted, bool& healthy);
    void onMessage(const WorkerJson& m, Run& run);
    void onShm(const WorkerJson& m);
    void onFrame(const WorkerJson& m, Run& run);
    bool flushOutbox(Run& run);
    void consumeLines(std::string& pending, Run& run);
};

void WorkerHost::Core::onMessage(const WorkerJson& m, Run& run)
{
    const std::string t = m.value("t", std::string());
    if (t == "pong") {
        return;
    }
    if (t == "hello") {
        run.gotHello = true;
        setState(HostState::Running, workerStatusText);
    } else if (t == "log") {
        const std::string level = m.value("level", std::string("info"));
        const std::string text = "[worker] " + m.value("msg", std::string());
        if (level == "warn")
            log::warn(text);
        else
            log::info(text);
    } else if (t == "transport") {
        const std::string kind = m.value("kind", std::string());
        const std::string why = m.value("why", std::string());
        {
            std::lock_guard lk(mutex);
            snap.transport = kind;
            snap.transportWhy = why;
        }
        log::info("Worker: quadros por memória compartilhada" + (why.empty() ? std::string() : " (" + why + ")"));
    } else if (t == "shm") {
        onShm(m);
    } else if (t == "frame") {
        onFrame(m, run);
    } else if (t == "doc") {
        {
            std::lock_guard lk(mutex);
            snap.pages = m.value("pages", 0);
            snap.title = m.value("title", std::string());
            events.push_back(m);
        }
    } else if (t == "status") {
        const std::string text = m.value("text", std::string());
        const bool warn = m.value("level", std::string()) == "warn";
        std::lock_guard lk(mutex);
        workerStatusText = text;
        if (snap.state == HostState::Running || snap.state == HostState::Starting) {
            snap.text = text;
            snap.warning = warn;
        }
    } else if (t == "error") {
        const std::string msg = m.value("msg", std::string("erro desconhecido"));
        if (m.value("fatal", false)) {
            run.fatal = msg;
        } else {
            log::warn("Worker: " + msg);
            pushEvent(m);
        }
    }
}

void WorkerHost::Core::onShm(const WorkerJson& m)
{
    const int w = m.value("w", 0), h = m.value("h", 0);
    const std::string name = m.value("name", std::string());
    if (name.empty() || w <= 0 || h <= 0)
        return;
    const std::size_t total = wp::shmTotalBytes(w, h);
    const int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        log::warn("Worker: não foi possível abrir a memória compartilhada");
        return;
    }
    struct stat st{};
    void* view = MAP_FAILED;
    if (fstat(fd, &st) == 0 && static_cast<std::size_t>(st.st_size) >= total)
        view = mmap(nullptr, total, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    shm_unlink(name.c_str());
    if (view == MAP_FAILED) {
        log::warn("Worker: não foi possível mapear a memória compartilhada");
        return;
    }
    const auto* hdr = static_cast<const wp::ShmHeader*>(view);
    if (hdr->magic != wp::kShmMagic || static_cast<int>(hdr->width) != w || static_cast<int>(hdr->height) != h) {
        munmap(view, total);
        log::warn("Worker: memória compartilhada inválida");
        return;
    }
    std::lock_guard lk(frameMutex);
    rx.reset();
    rx.view = view;
    rx.viewBytes = total;
    rx.shmW = w;
    rx.shmH = h;
}

void WorkerHost::Core::onFrame(const WorkerJson& m, Run& run)
{
    run.gotFrame = true;
    const int page = m.value("page", 0);
    std::lock_guard lk(frameMutex);
    if (!rx.view)
        return;
    rx.haveFrame = true;
    rx.page = page;
    rx.seq = frameCounter.fetch_add(1) + 1;
}

bool WorkerHost::Core::flushOutbox(Run& run)
{
    std::deque<WorkerJson> pending;
    {
        std::lock_guard lk(mutex);
        pending.swap(outbox);
    }
    for (const WorkerJson& m : pending) {
        std::string line = m.dump(-1, ' ', false, WorkerJson::error_handler_t::replace);
        line.push_back('\n');
        std::size_t off = 0;
        while (off < line.size()) {
            const ssize_t n = ::send(run.sock, line.data() + off, line.size() - off, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n > 0) {
                off += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd p{run.sock, POLLOUT, 0};
                if (::poll(&p, 1, 5000) > 0)
                    continue;
            }
            return false;
        }
    }
    return true;
}

void WorkerHost::Core::consumeLines(std::string& pending, Run& run)
{
    std::size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
        const std::string line = pending.substr(0, nl);
        pending.erase(0, nl + 1);
        if (line.empty())
            continue;
        const WorkerJson m = WorkerJson::parse(line, nullptr, false);
        if (m.is_object())
            onMessage(m, run);
    }
}

std::string WorkerHost::Core::runOnce(int attempt, bool& fatal, Clock::duration& lasted, bool& healthy)
{
    (void)attempt;
    fatal = false;
    healthy = false;
    Run run;
    run.started = Clock::now();
    setState(HostState::Starting, "Iniciando…");

    const std::string exe = bslplat::wideToUtf8(WorkerHost::workerExePath());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(exe, ec)) {
        fatal = true;
        return "o processo auxiliar (bslstage_worker) não foi encontrado";
    }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        fatal = true;
        return "não foi possível criar o canal de comunicação";
    }
    setupFd(sv[0], false);
    setupFd(sv[1], false);
    const int childFd = fcntl(sv[1], F_DUPFD, 16);
    close(sv[1]);
    if (childFd < 0) {
        close(sv[0]);
        fatal = true;
        return "não foi possível criar o canal de comunicação";
    }

    const std::string fdText = std::to_string(childFd);
    const std::string parentText = std::to_string(static_cast<long>(getpid()));
    std::vector<char*> argv = {const_cast<char*>(exe.c_str()), const_cast<char*>("--fd"), const_cast<char*>(fdText.c_str()),
                               const_cast<char*>("--parent"), const_cast<char*>(parentText.c_str()), nullptr};
    pid_t pid = 0;
    const int spawnErr = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), currentEnviron());
    close(childFd);
    if (spawnErr != 0) {
        close(sv[0]);
        fatal = true;
        return "não foi possível iniciar o processo auxiliar (erro " + std::to_string(spawnErr) + ")";
    }
    run.sock = sv[0];
    run.pid = pid;
    processPid = pid;
    {
        std::lock_guard lk(mutex);
        snap.pid = static_cast<unsigned long>(pid);
    }

    auto finish = [&](const std::string& why) -> std::string {
        if (!reap(run)) {
            if (run.gotHello) {
                {
                    std::lock_guard lk(mutex);
                    outbox.clear();
                    outbox.push_back(WorkerJson{{"t", "quit"}});
                }
                flushOutbox(run);
            }
            if (!waitExit(run, why.empty() ? 6000 : 1500)) {
                kill(run.pid, SIGKILL);
                waitExit(run, 2000);
            }
        }
        close(run.sock);
        run.sock = -1;
        processPid = 0;
        {
            std::lock_guard lk(mutex);
            snap.pid = 0;
        }
        lasted = Clock::now() - run.started;
        healthy = lasted > std::chrono::milliseconds(kStableMs) && run.gotFrame;
        {
            std::lock_guard lk(frameMutex);
            rx.reset();
        }
        return why;
    };

    {
        WorkerJson open_;
        std::string transport;
        {
            std::lock_guard lk(mutex);
            open_ = open;
            transport = transportRequest;
            outbox.clear();
        }
        open_["t"] = "open";
        open_["transport"] = transport;
        std::lock_guard lk(mutex);
        outbox.push_front(std::move(open_));
    }

    std::string pending;
    std::vector<char> buf(64 * 1024);
    auto lastRx = Clock::now();
    auto lastPing = Clock::now() - std::chrono::seconds(10);
    int pingNo = 0;
    for (;;) {
        pollfd fds[2] = {{run.sock, POLLIN, 0}, {wakeRd, POLLIN, 0}};
        const int pr = ::poll(fds, 2, 250);
        if (pr < 0 && errno != EINTR)
            return finish("falha ao esperar o processo auxiliar");
        if (stop.load())
            return finish(std::string());
        if (fds[1].revents & POLLIN)
            drainWake();

        bool eof = false;
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const ssize_t got = recv(run.sock, buf.data(), buf.size(), MSG_DONTWAIT);
            if (got > 0) {
                lastRx = Clock::now();
                pending.append(buf.data(), static_cast<std::size_t>(got));
                consumeLines(pending, run);
            } else if (got == 0 || (errno != EAGAIN && errno != EINTR)) {
                eof = true;
            }
        }
        if (!run.fatal.empty()) {
            fatal = true;
            return finish(run.fatal);
        }
        if (eof || reap(run)) {
            for (;;) {
                const ssize_t got = recv(run.sock, buf.data(), buf.size(), MSG_DONTWAIT);
                if (got <= 0)
                    break;
                pending.append(buf.data(), static_cast<std::size_t>(got));
                consumeLines(pending, run);
            }
            if (!run.fatal.empty()) {
                fatal = true;
                return finish(run.fatal);
            }
            if (waitExit(run, 2000))
                return finish("o processo auxiliar fechou inesperadamente (código " + std::to_string(run.exitCode) + ")");
            return finish("a conexão com o processo auxiliar caiu");
        }
        if (!flushOutbox(run))
            return finish("não foi possível falar com o processo auxiliar");

        const auto now = Clock::now();
        if (now - lastPing > std::chrono::seconds(1) && run.gotHello) {
            lastPing = now;
            {
                std::lock_guard lk(mutex);
                outbox.push_back(WorkerJson{{"t", "ping"}, {"n", ++pingNo}});
            }
            if (!flushOutbox(run))
                return finish("não foi possível falar com o processo auxiliar");
        }
        const unsigned limit = run.gotHello ? kHangTimeoutMs : kStartTimeoutMs;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRx).count() > limit)
            return finish("o processo auxiliar parou de responder");
    }
}

void WorkerHost::Core::supervise()
{
    int failures = 0;
    int attempt = 0;
    for (;;) {
        if (stop.load())
            break;
        bool fatal = false;
        bool healthy = false;
        Clock::duration lasted{};
        std::string reason = runOnce(attempt++, fatal, lasted, healthy);
        if (stop.load() || reason.empty())
            break;
        if (healthy)
            failures = 0;
        ++failures;
        log::warn("Worker: " + reason);
        if (fatal || failures > kMaxRestarts) {
            {
                std::lock_guard lk(mutex);
                snap.failure = reason;
                snap.state = HostState::Failed;
                snap.text = "Falhou: " + reason;
                snap.warning = true;
            }
            break;
        }
        {
            std::lock_guard lk(mutex);
            ++snap.restarts;
        }
        setState(HostState::Restarting, "Reiniciando… (tentativa " + std::to_string(failures) + "/" + std::to_string(kMaxRestarts) + ")", true);
        if (waitStop(kRestartDelaysMs[failures - 1]))
            break;
    }
    {
        std::lock_guard lk(mutex);
        if (snap.state != HostState::Failed) {
            snap.state = HostState::Idle;
            snap.text.clear();
        }
    }
    processPid = 0;
    {
        std::lock_guard lk(g_liveMutex);
        --g_liveSupervisors;
    }
    g_liveCv.notify_all();
}

WorkerHost::WorkerHost() = default;

WorkerHost::~WorkerHost()
{
    stop();
}

std::wstring WorkerHost::workerExePath()
{
    return media::workerExePath();
}

void WorkerHost::start(WorkerJson open, const std::shared_ptr<render::D3D11Interop>&, const std::string& transport)
{
    if (core_)
        return;
    auto core = std::make_shared<Core>();
    core->hostId = ++g_hostCounter;
    core->open = std::move(open);
    core->transportRequest = transport == "texture" ? "auto" : transport;
    core->snap.state = HostState::Starting;
    core->snap.text = "Iniciando…";
    {
        std::lock_guard lk(g_liveMutex);
        ++g_liveSupervisors;
    }
    core->thread = std::thread([core] { core->supervise(); });
    core_ = std::move(core);
}

void WorkerHost::stop()
{
    if (!core_)
        return;
    core_->stop = true;
    core_->wake();
    if (core_->thread.joinable())
        core_->thread.detach();
    core_.reset();
}

void WorkerHost::setOpen(WorkerJson open)
{
    if (!core_)
        return;
    std::lock_guard lk(core_->mutex);
    core_->open = std::move(open);
}

void WorkerHost::send(WorkerJson msg)
{
    if (!core_)
        return;
    {
        std::lock_guard lk(core_->mutex);
        if (core_->snap.state != HostState::Running && core_->snap.state != HostState::Starting)
            return;
        core_->outbox.push_back(std::move(msg));
        if (core_->outbox.size() > 512)
            core_->outbox.pop_front();
    }
    core_->wake();
}

std::vector<WorkerJson> WorkerHost::drainEvents()
{
    std::vector<WorkerJson> out;
    if (!core_)
        return out;
    std::lock_guard lk(core_->mutex);
    out.assign(std::make_move_iterator(core_->events.begin()), std::make_move_iterator(core_->events.end()));
    core_->events.clear();
    return out;
}

HostSnapshot WorkerHost::snapshot() const
{
    if (!core_)
        return {};
    std::lock_guard lk(core_->mutex);
    return core_->snap;
}

bool WorkerHost::acquireNewFrame(std::uint64_t& lastSeq, const std::function<void(const FrameAccess&)>& fn)
{
    if (!core_)
        return false;
    std::lock_guard lk(core_->frameMutex);
    Core::Receiver& rx = core_->rx;
    if (!rx.haveFrame || rx.seq == lastSeq)
        return false;
    FrameAccess fa;
    fa.kind = FrameAccess::Kind::Shm;
    fa.page = rx.page;
    fa.seq = rx.seq;
    fa.width = rx.shmW;
    fa.height = rx.shmH;
    fa.shmBase = rx.view;
    fa.shmHeader = static_cast<const wp::ShmHeader*>(rx.view);
    lastSeq = rx.seq;
    fn(fa);
    return true;
}

std::uint64_t WorkerHost::frameCount() const
{
    return core_ ? core_->frameCounter.load() : 0;
}

void WorkerHost::debugKillWorker()
{
    if (!core_)
        return;
    const pid_t pid = core_->processPid.load();
    if (pid > 0)
        kill(pid, SIGKILL);
}

bool WorkerHost::waitAllStopped(unsigned timeoutMs)
{
    std::unique_lock lk(g_liveMutex);
    return g_liveCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [] { return g_liveSupervisors == 0; });
}

bool WorkerHost::processRunning(unsigned long pid)
{
    if (pid == 0)
        return false;
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM)
        return false;
#ifdef __linux__
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (f && std::getline(f, line)) {
        const std::size_t p = line.rfind(')');
        if (p != std::string::npos && p + 2 < line.size())
            return line[p + 2] != 'Z';
    }
#endif
    return true;
}

}
