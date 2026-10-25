#include "media/WorkerHost.h"

#include "app/Log.h"
#include "media/WorkerProcess.h"
#include "render/D3D11Interop.h"

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace bsl::media {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kMaxRestarts = 3;
constexpr unsigned kRestartDelaysMs[kMaxRestarts] = {1000, 2000, 4000};
constexpr unsigned kHangTimeoutMs = 12000;     // sem nenhuma mensagem do worker
constexpr unsigned kStartTimeoutMs = 20000;    // do CreateProcess ate o "hello"
constexpr unsigned kStableMs = 20000;          // rodou isto sem cair: zera a contagem de reinicios

std::atomic<int> g_hostCounter{0};

std::string hrHex(HRESULT hr)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}
std::mutex g_liveMutex;
std::condition_variable g_liveCv;
int g_liveSupervisors = 0;

} // namespace

// ----------------------------------------------------------------------------- Core

struct WorkerHost::Core : std::enable_shared_from_this<WorkerHost::Core>
{
    struct Run
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        HANDLE process = nullptr;
        unsigned long pid = 0;
        bool gotHello = false;
        bool gotFrame = false;
        std::string fatal; // motivo de erro fatal enviado pelo worker
        Clock::time_point started;
    };

    // Recebimento de quadros (protegido por frameMutex)
    struct Receiver
    {
        FrameAccess::Kind kind = FrameAccess::Kind::Shm;
        // shm
        void* view = nullptr;
        int shmW = 0, shmH = 0;
        // textura
        ComPtr<ID3D11Texture2D> shared[2];
        ComPtr<IDXGIKeyedMutex> keyed[2];
        ComPtr<ID3D11Texture2D> mailbox;
        int texW = 0, texH = 0;
        bool haveFrame = false;
        int page = 0;
        std::uint64_t seq = 0;
        int mutexTimeouts = 0;

        ~Receiver() { reset(); }
        void reset()
        {
            if (view) {
                UnmapViewOfFile(view);
                view = nullptr;
            }
            for (int i = 0; i < 2; ++i) {
                keyed[i].Reset();
                shared[i].Reset();
            }
            mailbox.Reset();
            haveFrame = false;
        }
    };

    ~Core()
    {
        if (stopEvent)
            CloseHandle(stopEvent);
        if (wakeEvent)
            CloseHandle(wakeEvent);
    }

    // --- estado ---
    int hostId = 0;
    std::atomic<bool> stop{false};
    HANDLE stopEvent = nullptr;
    HANDLE wakeEvent = nullptr;

    mutable std::mutex mutex; // snap, open, events, outbox
    HostSnapshot snap;
    WorkerJson open;
    std::deque<WorkerJson> events;
    std::deque<WorkerJson> outbox;
    std::string transportRequest = "auto";
    std::string workerStatusText;

    // D3D do app (so ponteiros COM: este objeto pode sobreviver ao WorkerHost)
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> context;
    bool haveLuid = false;
    LUID luid{};
    bool forceShm = false; // a textura compartilhada nao abriu: nas proximas vezes so shm

    std::mutex frameMutex;
    Receiver rx;
    std::atomic<std::uint64_t> frameCounter{0};
    std::atomic<HANDLE> processHandle{nullptr};

    std::thread thread;

    // --- API usada pelo supervisor ---
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

    bool waitStop(unsigned ms) { return WaitForSingleObject(stopEvent, ms) == WAIT_OBJECT_0; }

    void supervise();
    std::string runOnce(int attempt, bool& fatal, Clock::duration& lasted, bool& healthy);
    void onMessage(const WorkerJson& m, Run& run);
    void onTex(const WorkerJson& m, Run& run);
    void onShm(const WorkerJson& m);
    void onFrame(const WorkerJson& m, Run& run);
    bool flushOutbox(Run& run);
};

// ----------------------------------------------------------------------------- mensagens

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
        log::info("Worker: quadros por " + std::string(kind == "texture" ? "textura D3D11 compartilhada" : "memória compartilhada")
                  + (why.empty() ? "" : " (" + why + ")"));
    } else if (t == "tex") {
        onTex(m, run);
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
    const auto handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(m.value("handle", std::uint64_t{0})));
    if (!handle || w <= 0 || h <= 0)
        return;
    void* view = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, wp::shmTotalBytes(w, h));
    CloseHandle(handle);
    if (!view) {
        log::warn("Worker: não foi possível mapear a memória compartilhada");
        return;
    }
    const auto* hdr = static_cast<const wp::ShmHeader*>(view);
    if (hdr->magic != wp::kShmMagic || static_cast<int>(hdr->width) != w || static_cast<int>(hdr->height) != h) {
        UnmapViewOfFile(view);
        log::warn("Worker: memória compartilhada inválida");
        return;
    }
    std::lock_guard lk(frameMutex);
    rx.reset();
    rx.kind = FrameAccess::Kind::Shm;
    rx.view = view;
    rx.shmW = w;
    rx.shmH = h;
}

void WorkerHost::Core::onTex(const WorkerJson& m, Run& run)
{
    (void)run;
    const int w = m.value("w", 0), h = m.value("h", 0);
    std::uint64_t raw[2] = {0, 0};
    if (m.contains("handles") && m["handles"].is_array() && m["handles"].size() == 2) {
        raw[0] = m["handles"][0].get<std::uint64_t>();
        raw[1] = m["handles"][1].get<std::uint64_t>();
    }
    HANDLE handles[2] = {reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw[0])),
                         reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw[1]))};

    std::string why;
    ComPtr<ID3D11Texture2D> shared[2];
    ComPtr<IDXGIKeyedMutex> keyed[2];
    ComPtr<ID3D11Texture2D> mailbox;
    if (!device1) {
        why = "o app não tem device D3D11.1";
    } else {
        for (int i = 0; i < 2 && why.empty(); ++i) {
            const HRESULT hr = device1->OpenSharedResource1(handles[i], IID_PPV_ARGS(&shared[i]));
            if (FAILED(hr))
                why = "OpenSharedResource1 falhou (" + hrHex(hr) + ")";
            else if (FAILED(shared[i].As(&keyed[i])))
                why = "a textura compartilhada não tem keyed mutex";
        }
        if (why.empty()) {
            D3D11_TEXTURE2D_DESC d{};
            shared[0]->GetDesc(&d);
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags = 0;
            d.CPUAccessFlags = 0;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &mailbox)))
                why = "não foi possível criar a textura de recebimento";
        }
    }
    for (HANDLE hnd : handles)
        if (hnd)
            CloseHandle(hnd);

    if (!why.empty()) {
        log::warn("Worker: textura compartilhada indisponível (" + why + "); usando memória compartilhada");
        {
            std::lock_guard lk(mutex);
            forceShm = true;
            snap.transport = "shm";
            snap.transportWhy = why;
            outbox.push_back(WorkerJson{{"t", "transport"}, {"use", "shm"}});
        }
        SetEvent(wakeEvent);
        return;
    }
    std::lock_guard lk(frameMutex);
    rx.reset();
    rx.kind = FrameAccess::Kind::Texture;
    for (int i = 0; i < 2; ++i) {
        rx.shared[i] = shared[i];
        rx.keyed[i] = keyed[i];
    }
    rx.mailbox = mailbox;
    rx.texW = w;
    rx.texH = h;
}

void WorkerHost::Core::onFrame(const WorkerJson& m, Run& run)
{
    run.gotFrame = true;
    const int idx = std::clamp(m.value("i", 0), 0, 1);
    const int page = m.value("page", 0);
    std::lock_guard lk(frameMutex);
    if (rx.kind == FrameAccess::Kind::Texture) {
        if (!rx.mailbox || !rx.keyed[idx])
            return;
        if (rx.keyed[idx]->AcquireSync(1, 250) != S_OK) {
            if (++rx.mutexTimeouts == 1 || rx.mutexTimeouts % 100 == 0)
                log::warn("Worker: a textura compartilhada não ficou disponível a tempo (quadro descartado)");
            return;
        }
        context->CopyResource(rx.mailbox.Get(), rx.shared[idx].Get());
        rx.keyed[idx]->ReleaseSync(0);
    } else if (!rx.view) {
        return;
    }
    rx.haveFrame = true;
    rx.page = page;
    rx.seq = frameCounter.fetch_add(1) + 1;
}

// ----------------------------------------------------------------------------- supervisor

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
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD written = 0;
        bool ok = WriteFile(run.pipe, line.data(), static_cast<DWORD>(line.size()), &written, &ov) != FALSE;
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = {ov.hEvent, run.process};
            const DWORD r = WaitForMultipleObjects(2, waits, FALSE, 5000);
            if (r == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(run.pipe, &ov, &written, FALSE) != FALSE;
            } else {
                CancelIoEx(run.pipe, &ov);
                GetOverlappedResult(run.pipe, &ov, &written, TRUE);
                ok = false;
            }
        }
        CloseHandle(ov.hEvent);
        if (!ok)
            return false;
    }
    return true;
}

std::string WorkerHost::Core::runOnce(int attempt, bool& fatal, Clock::duration& lasted, bool& healthy)
{
    fatal = false;
    healthy = false;
    Run run;
    run.started = Clock::now();
    setState(HostState::Starting, "Iniciando…");

    const std::wstring exe = WorkerHost::workerExePath();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(exe, ec)) {
        fatal = true;
        return "o processo auxiliar (bslstage_worker.exe) não foi encontrado";
    }

    const std::wstring pipeName = L"\\\\.\\pipe\\bslstage-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                                  + std::to_wstring(hostId) + L"-" + std::to_wstring(attempt) + L"-"
                                  + std::to_wstring(GetTickCount64());
    run.pipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1 << 16,
                                1 << 16, 0, nullptr);
    if (run.pipe == INVALID_HANDLE_VALUE) {
        fatal = true;
        return "não foi possível criar o pipe de comunicação";
    }

    std::wstring cmd = L"\"" + exe + L"\" --pipe " + pipeName + L" --parent " + std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring dir = std::filesystem::path(exe).parent_path().wstring();
    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        dir.c_str(), &si, &pi)) {
        const DWORD err = GetLastError();
        CloseHandle(run.pipe);
        fatal = true;
        return "não foi possível iniciar o processo auxiliar (erro " + std::to_string(err) + ")";
    }
    if (HANDLE job = media::workerJob(); job && !AssignProcessToJobObject(job, pi.hProcess))
        log::warn("Worker: não foi possível colocar o processo no job object (erro " + std::to_string(GetLastError()) + ")");
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    run.process = pi.hProcess;
    run.pid = pi.dwProcessId;
    processHandle = run.process;
    {
        std::lock_guard lk(mutex);
        snap.pid = run.pid;
    }
    std::string reason;
    HANDLE rev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE cev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool quitSent = false;

    auto finish = [&](const std::string& why) -> std::string {
        // Encerra o worker: pede "quit" se ainda de pe, senao mata
        if (WaitForSingleObject(run.process, 0) == WAIT_TIMEOUT) {
            if (!quitSent && run.pipe != INVALID_HANDLE_VALUE && run.gotHello) {
                {
                    std::lock_guard lk(mutex);
                    outbox.clear();
                    outbox.push_back(WorkerJson{{"t", "quit"}});
                }
                flushOutbox(run);
            }
            // Conversao do Office em andamento pode demorar para fechar; o resto sai em ms
            if (WaitForSingleObject(run.process, why.empty() ? 6000 : 1500) != WAIT_OBJECT_0)
                TerminateProcess(run.process, 1);
            WaitForSingleObject(run.process, 2000);
        }
        DWORD code = 0;
        GetExitCodeProcess(run.process, &code);
        CancelIoEx(run.pipe, nullptr);
        DisconnectNamedPipe(run.pipe);
        CloseHandle(run.pipe);
        processHandle = nullptr;
        CloseHandle(run.process);
        CloseHandle(rev);
        CloseHandle(cev);
        {
            std::lock_guard lk(mutex);
            snap.pid = 0;
        }
        (void)code;
        lasted = Clock::now() - run.started;
        healthy = lasted > std::chrono::milliseconds(kStableMs) && run.gotFrame;
        {
            std::lock_guard lk(frameMutex);
            // Mantem o recebimento so enquanto o worker vive; o proximo reabre pelas mensagens tex/shm
            rx.reset();
        }
        return why;
    };

    // --- aguarda a conexao do worker ---
    {
        OVERLAPPED ov{};
        ov.hEvent = cev;
        bool connected = false;
        if (!ConnectNamedPipe(run.pipe, &ov)) {
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                HANDLE waits[3] = {cev, run.process, stopEvent};
                const DWORD r = WaitForMultipleObjects(3, waits, FALSE, kStartTimeoutMs);
                if (r == WAIT_OBJECT_0)
                    connected = true;
                else if (r == WAIT_OBJECT_0 + 2)
                    return finish(std::string());
                else
                    return finish(r == WAIT_OBJECT_0 + 1 ? "o processo auxiliar fechou ao iniciar" : "o processo auxiliar não conectou a tempo");
            }
        } else {
            connected = true;
        }
        if (!connected)
            return finish("falha ao conectar ao processo auxiliar");
    }

    // --- mensagem open ---
    {
        WorkerJson open_;
        std::string transport;
        {
            std::lock_guard lk(mutex);
            open_ = open;
            transport = (forceShm) ? "shm" : transportRequest;
            outbox.clear();
        }
        open_["t"] = "open";
        open_["transport"] = transport;
        if (haveLuid)
            open_["luid"] = WorkerJson::array({static_cast<long>(luid.HighPart), static_cast<unsigned long>(luid.LowPart)});
        std::lock_guard lk(mutex);
        outbox.push_front(std::move(open_));
    }

    // --- laco de servico ---
    std::string pending;
    std::vector<char> buf(64 * 1024);
    OVERLAPPED rov{};
    rov.hEvent = rev;
    bool readPending = false;
    auto startRead = [&]() -> bool {
        ResetEvent(rev);
        DWORD got = 0;
        const BOOL ok = ReadFile(run.pipe, buf.data(), static_cast<DWORD>(buf.size()), &got, &rov);
        if (!ok && GetLastError() != ERROR_IO_PENDING)
            return false;
        readPending = true;
        return true;
    };
    if (!startRead())
        return finish("falha ao ler do processo auxiliar");

    auto lastRx = Clock::now();
    auto lastPing = Clock::now() - std::chrono::seconds(10);
    int pingNo = 0;
    for (;;) {
        HANDLE waits[4] = {rev, run.process, stopEvent, wakeEvent};
        const DWORD r = WaitForMultipleObjects(4, waits, FALSE, 250);
        if (r == WAIT_OBJECT_0 + 2) { // pedido de parada do app
            quitSent = false;
            return finish(std::string());
        }
        if (r == WAIT_OBJECT_0 + 1) {
            DWORD code = 0;
            GetExitCodeProcess(run.process, &code);
            if (!run.fatal.empty()) {
                fatal = true;
                return finish(run.fatal);
            }
            return finish("o processo auxiliar fechou inesperadamente (código " + std::to_string(code) + ")");
        }
        if (r == WAIT_OBJECT_0) {
            DWORD got = 0;
            readPending = false;
            if (!GetOverlappedResult(run.pipe, &rov, &got, FALSE) || got == 0)
                return finish("a conexão com o processo auxiliar caiu");
            lastRx = Clock::now();
            pending.append(buf.data(), got);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (line.empty())
                    continue;
                const WorkerJson m = WorkerJson::parse(line, nullptr, false);
                if (m.is_object())
                    onMessage(m, run);
            }
            if (!run.fatal.empty()) {
                fatal = true;
                quitSent = false;
                return finish(run.fatal);
            }
            if (!startRead())
                return finish("a conexão com o processo auxiliar caiu");
        }
        // Escritas pendentes (open, comandos, ping)
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
    processHandle = nullptr;
    {
        std::lock_guard lk(g_liveMutex);
        --g_liveSupervisors;
    }
    g_liveCv.notify_all();
}

// ----------------------------------------------------------------------------- WorkerHost

WorkerHost::WorkerHost() = default;

WorkerHost::~WorkerHost()
{
    stop();
}

std::wstring WorkerHost::workerExePath()
{
    return media::workerExePath();
}

void WorkerHost::start(WorkerJson open, const std::shared_ptr<render::D3D11Interop>& interop, const std::string& transport)
{
    if (core_)
        return;
    auto core = std::make_shared<Core>();
    core->hostId = ++g_hostCounter;
    core->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    core->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    core->open = std::move(open);
    core->transportRequest = transport;
    if (interop && interop->device() && interop->adapter()) {
        core->device = interop->device();
        core->context = interop->context();
        core->device.As(&core->device1);
        DXGI_ADAPTER_DESC1 desc{};
        interop->adapter()->GetDesc1(&desc);
        core->luid = desc.AdapterLuid;
        core->haveLuid = true;
    }
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
    SetEvent(core_->stopEvent);
    if (core_->thread.joinable())
        core_->thread.detach(); // o Core vive ate a thread terminar
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
            return; // sem worker no momento: o estado atual vai no proximo "open"
        core_->outbox.push_back(std::move(msg));
        if (core_->outbox.size() > 512)
            core_->outbox.pop_front();
    }
    SetEvent(core_->wakeEvent);
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
    fa.kind = rx.kind;
    fa.page = rx.page;
    fa.seq = rx.seq;
    if (rx.kind == FrameAccess::Kind::Shm) {
        fa.width = rx.shmW;
        fa.height = rx.shmH;
        fa.shmBase = rx.view;
        fa.shmHeader = static_cast<const wp::ShmHeader*>(rx.view);
    } else {
        fa.width = rx.texW;
        fa.height = rx.texH;
        fa.mailbox = rx.mailbox.Get();
        fa.context = core_->context.Get();
    }
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
    if (HANDLE h = core_->processHandle.load())
        TerminateProcess(h, 0xDEAD);
}

bool WorkerHost::waitAllStopped(unsigned timeoutMs)
{
    std::unique_lock lk(g_liveMutex);
    return g_liveCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [] { return g_liveSupervisors == 0; });
}

bool WorkerHost::processRunning(unsigned long pid)
{
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

} // namespace bsl::media
