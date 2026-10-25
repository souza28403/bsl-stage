// bslstage_worker.exe: processo auxiliar do BSL Stage (PDF, Office e Web).
//
//   bslstage_worker.exe --pipe <nome> --parent <pid>         modo normal (um por clip)
//   bslstage_worker.exe --thumb <entrada> <saida> <w> <h>    miniatura da pagina 1 (arquivo bruto: w, h, paginas, RGBA)
//   bslstage_worker.exe --type=...                            subprocesso do CEF (quando a Web esta habilitada)
//
// Se o app morrer, o job object do app (KILL_ON_JOB_CLOSE) derruba o worker; alem disso o worker vigia o
// processo pai e sai sozinho.

#include "media/WorkerProtocol.h"
#include "worker/FrameSink.h"
#include "worker/OfficeConverter.h"
#include "worker/PdfDocument.h"
#include "worker/PipeChannel.h"
#include "worker/WorkerUtil.h"

#ifdef BSL_WITH_CEF
#include "worker/WebSession.h"
#endif

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bsl::worker {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

void colorFromJson(const Json& v, std::uint8_t outBgra[4])
{
    float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (v.is_array() && v.size() == 4)
        for (size_t i = 0; i < 4; ++i)
            if (v[i].is_number())
                c[i] = std::clamp(v[i].get<float>(), 0.0f, 1.0f);
    auto b = [](float f) { return static_cast<std::uint8_t>(std::lround(f * 255.0f)); };
    // BGRA com alpha reta
    outBgra[0] = b(c[2]);
    outBgra[1] = b(c[1]);
    outBgra[2] = b(c[0]);
    outBgra[3] = b(c[3]);
}

// --------------------------------------------------------------------------------- modo miniatura

int runThumbnail(const std::wstring& in, const std::wstring& out, int w, int h)
{
    fs::path source(in);
    std::string error;
    if (isOfficeExtension(lowerExt(source))) {
        const ConvertResult r = convertToPdf(source);
        if (!r.ok) {
            fileLog("miniatura: " + r.error);
            return 3;
        }
        source = r.pdf;
    }
    PdfDocument pdf;
    if (!pdf.open(source, &error)) {
        fileLog("miniatura: " + error);
        return 2;
    }
    const std::uint8_t clear[4] = {0, 0, 0, 0};
    std::vector<std::uint8_t> bgra;
    if (!pdf.render(1, w, h, clear, bgra))
        return 4;
    std::vector<std::uint8_t> rgba(bgra.size());
    for (size_t i = 0; i < bgra.size(); i += 4) {
        rgba[i] = bgra[i + 2];
        rgba[i + 1] = bgra[i + 1];
        rgba[i + 2] = bgra[i];
        rgba[i + 3] = bgra[i + 3];
    }
    const fs::path tmp = fs::path(out + L".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return 5;
        const std::int32_t head[3] = {w, h, pdf.pageCount()};
        f.write(reinterpret_cast<const char*>(head), sizeof(head));
        f.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }
    std::error_code ec;
    fs::rename(tmp, fs::path(out), ec);
    if (ec) {
        fs::copy_file(tmp, fs::path(out), fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
    }
    return ec ? 6 : 0;
}

// --------------------------------------------------------------------------------- modo normal

class Worker
{
public:
    Worker() { wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
    ~Worker()
    {
        joinConvert();
        if (parent_)
            CloseHandle(parent_);
        if (wake_)
            CloseHandle(wake_);
    }

    int run(const std::wstring& pipeName, DWORD parentPid)
    {
        parentPid_ = parentPid;
        parent_ = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        std::string error;
        if (!channel_.connect(pipeName, wake_, &error)) {
            fileLog("pipe: " + error);
            return 10;
        }
        channel_.send(Json{{"t", "hello"}, {"pid", GetCurrentProcessId()}, {"v", wp::kProtocolVersion}});
        lastPing_ = Clock::now();

        while (!quit_) {
            if (channel_.broken())
                break;
            if (parent_ && WaitForSingleObject(parent_, 0) == WAIT_OBJECT_0)
                break;
            if (Clock::now() - lastPing_ > std::chrono::seconds(30)) {
                fileLog("sem ping do app por 30 s; encerrando");
                break;
            }

            Json msg;
            while (!quit_ && channel_.poll(msg))
                handle(msg);

            pollConvert();
            retryPublish();
#ifdef BSL_WITH_CEF
            if (web_)
                web_->pump();
#endif
            DWORD timeout = 15;
            if (pendingPublish_)
                timeout = 5;
            else if (needsPrerender() || convertDone_.load())
                timeout = 0;
#ifdef BSL_WITH_CEF
            if (web_)
                timeout = std::min<DWORD>(timeout, 4);
#endif
            if (!quit_) {
                prerenderStep();
                WaitForSingleObject(wake_, timeout);
            }
        }
        shutdown();
        return 0;
    }

private:
    // ------------------------------------------------------------------ mensagens
    void handle(const Json& m)
    {
        const std::string t = m.value("t", std::string());
        if (t == "ping") {
            lastPing_ = Clock::now();
            channel_.send(Json{{"t", "pong"}, {"n", m.value("n", 0)}});
        } else if (t == "open") {
            onOpen(m);
        } else if (t == "params") {
            onParams(m);
        } else if (t == "page") {
            if (pdf_.isOpen())
                showPage(m.value("n", curPage_));
        } else if (t == "resize") {
            onResize(m.value("w", width_), m.value("h", height_));
        } else if (t == "transport") {
            if (m.value("use", std::string()) == "shm")
                switchToShm();
        } else if (t == "quit") {
            quit_ = true;
        }
#ifdef BSL_WITH_CEF
        else if (web_) {
            web_->handle(m);
        }
#endif
    }

    // So para teste: BSLSTAGE_WORKER_HANG_ONCE=<arquivo>. Na primeira execucao (arquivo ainda nao existe) cria o
    // arquivo e trava de verdade (sem responder a pings); as proximas execucoes seguem normalmente.
    void simulateHangOnce()
    {
        wchar_t marker[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"BSLSTAGE_WORKER_HANG_ONCE", marker, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return;
        std::error_code ec;
        if (fs::exists(marker, ec))
            return;
        std::ofstream(marker) << "hang";
        for (;;)
            Sleep(1000);
    }

    void fail(const std::string& msg, bool fatal = true)
    {
        channel_.send(Json{{"t", "error"}, {"msg", msg}, {"fatal", fatal}});
        fileLog("erro: " + msg);
    }

    SinkRequest sinkRequest() const
    {
        SinkRequest r;
        r.channel = const_cast<PipeChannel*>(&channel_);
        r.appPid = parentPid_;
        r.haveLuid = haveLuid_;
        r.luidHigh = luidHigh_;
        r.luidLow = luidLow_;
        r.transport = transport_;
        return r;
    }

    void onOpen(const Json& m)
    {
        if (opened_)
            return;
        opened_ = true;
        simulateHangOnce();
        kind_ = m.value("kind", std::string());
        width_ = std::clamp(m.value("w", 1920), 16, 8192);
        height_ = std::clamp(m.value("h", 1080), 16, 8192);
        transport_ = m.value("transport", std::string("auto"));
        if (m.contains("luid") && m["luid"].is_array() && m["luid"].size() == 2) {
            haveLuid_ = true;
            luidHigh_ = m["luid"][0].get<LONG>();
            luidLow_ = m["luid"][1].get<DWORD>();
        }
        const Json params = m.value("params", Json::object());

        if (kind_ == "pdf") {
            colorFromJson(params.value("bg", Json()), bg_);
            curPage_ = std::max(1, params.value("page", 1));
            sink_ = createFrameSink(sinkRequest());
            if (!sink_ || !sink_->resize(width_, height_)) {
                if (!sink_)
                    fail("não foi possível criar o canal de imagem");
                else
                    fail("não foi possível criar os buffers de imagem");
                return;
            }
            const fs::path file = fromUtf8(m.value("path", std::string()));
            if (isOfficeExtension(lowerExt(file))) {
                channel_.send(Json{{"t", "status"}, {"text", "Convertendo…"}, {"level", "info"}});
                startConvert(file);
            } else {
                loadPdf(file);
            }
        } else if (kind_ == "web") {
#ifdef BSL_WITH_CEF
            web_ = std::make_unique<WebSession>(channel_, sinkRequest(), wake_);
            web_->open(m);
#else
            fail("A Web não está habilitada nesta compilação (CEF não encontrado)");
#endif
        } else {
            fail("tipo de fonte desconhecido: " + kind_);
        }
    }

    void onParams(const Json& m)
    {
        const Json params = m.value("params", Json::object());
#ifdef BSL_WITH_CEF
        if (web_) {
            web_->setParams(params);
            return;
        }
#endif
        if (kind_ != "pdf")
            return;
        std::uint8_t bg[4];
        colorFromJson(params.value("bg", Json()), bg);
        if (std::equal(bg, bg + 4, bg_))
            return;
        std::copy(bg, bg + 4, bg_);
        cache_.clear();
        if (pdf_.isOpen())
            showPage(curPage_);
    }

    void onResize(int w, int h)
    {
        w = std::clamp(w, 16, 8192);
        h = std::clamp(h, 16, 8192);
#ifdef BSL_WITH_CEF
        if (web_) {
            web_->resize(w, h);
            return;
        }
#endif
        if (!sink_ || (w == width_ && h == height_))
            return;
        width_ = w;
        height_ = h;
        cache_.clear();
        if (!sink_->resize(w, h)) {
            fail("não foi possível redimensionar a imagem");
            return;
        }
        if (pdf_.isOpen())
            showPage(curPage_);
    }

    void switchToShm()
    {
        transport_ = "shm";
#ifdef BSL_WITH_CEF
        if (web_) {
            web_->useShm();
            return;
        }
#endif
        sink_ = createShmSink(sinkRequest());
        if (!sink_ || !sink_->resize(width_, height_)) {
            fail("não foi possível criar a memória compartilhada");
            return;
        }
        channel_.send(Json{{"t", "transport"}, {"kind", "shm"}, {"why", "o app não conseguiu abrir a textura compartilhada"}});
        if (pdf_.isOpen())
            showPage(curPage_);
    }

    // ------------------------------------------------------------------ PDF
    void loadPdf(const fs::path& file)
    {
        std::string error;
        if (!pdf_.open(file, &error)) {
            fail(error.empty() ? "não foi possível abrir o PDF" : error);
            return;
        }
        channel_.send(Json{{"t", "status"}, {"text", ""}, {"level", "info"}});
        channel_.send(Json{{"t", "doc"}, {"pages", pdf_.pageCount()}, {"title", pdf_.title()}});
        showPage(curPage_);
    }

    std::vector<std::uint8_t>* pageBuffer(int page)
    {
        auto it = cache_.find(page);
        if (it != cache_.end())
            return &it->second;
        std::vector<std::uint8_t> buf;
        if (!pdf_.render(page, width_, height_, bg_, buf))
            return nullptr;
        return &(cache_[page] = std::move(buf));
    }

    void trimCache()
    {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (std::abs(it->first - curPage_) > 1)
                it = cache_.erase(it);
            else
                ++it;
        }
    }

    void showPage(int page)
    {
        page = std::clamp(page, 1, std::max(1, pdf_.pageCount()));
        curPage_ = page;
        std::vector<std::uint8_t>* buf = pageBuffer(page);
        if (!buf) {
            fail("não foi possível desenhar a página " + std::to_string(page), false);
            return;
        }
        trimCache();
        publishedPage_ = 0;
        pendingPublish_ = true;
        retryPublish();
    }

    void retryPublish()
    {
        if (!pendingPublish_ || !sink_ || !pdf_.isOpen())
            return;
        auto it = cache_.find(curPage_);
        if (it == cache_.end())
            return;
        if (sink_->submitBgra(it->second.data(), width_ * 4, false, curPage_)) {
            pendingPublish_ = false;
            publishedPage_ = curPage_;
        }
    }

    bool needsPrerender() const
    {
        if (!pdf_.isOpen() || !sink_ || pendingPublish_)
            return false;
        for (int p : {curPage_ + 1, curPage_ - 1})
            if (p >= 1 && p <= pdf_.pageCount() && !cache_.count(p))
                return true;
        return false;
    }

    // Pre-renderiza a proxima e a anterior, uma por ciclo (nao segura os pings)
    void prerenderStep()
    {
        if (!needsPrerender())
            return;
        for (int p : {curPage_ + 1, curPage_ - 1}) {
            if (p >= 1 && p <= pdf_.pageCount() && !cache_.count(p)) {
                pageBuffer(p);
                return;
            }
        }
    }

    // ------------------------------------------------------------------ Office -> PDF
    void startConvert(const fs::path& file)
    {
        convertThread_ = std::thread([this, file] {
            ConvertResult r = convertToPdf(file);
            {
                std::lock_guard lk(convertMutex_);
                convertResult_ = std::move(r);
            }
            convertDone_ = true;
            SetEvent(wake_);
        });
    }

    void pollConvert()
    {
        if (!convertDone_.exchange(false))
            return;
        joinConvert();
        ConvertResult r;
        {
            std::lock_guard lk(convertMutex_);
            r = std::move(convertResult_);
        }
        if (!r.ok) {
            fail(r.error.empty() ? "não foi possível converter o documento" : r.error);
            return;
        }
        channel_.log("info", r.fromCache ? "documento convertido: usando o cache" : "documento convertido por " + r.via);
        loadPdf(r.pdf);
    }

    void joinConvert()
    {
        if (convertThread_.joinable())
            convertThread_.join();
    }

    void shutdown()
    {
#ifdef BSL_WITH_CEF
        web_.reset();
#endif
        joinConvert(); // deixa o Office fechar o que abriu
        pdf_.close();
        sink_.reset();
    }

    PipeChannel channel_;
    HANDLE wake_ = nullptr;
    HANDLE parent_ = nullptr;
    DWORD parentPid_ = 0;
    bool quit_ = false;
    bool opened_ = false;
    Clock::time_point lastPing_;

    std::string kind_;
    std::string transport_ = "auto";
    bool haveLuid_ = false;
    LONG luidHigh_ = 0;
    DWORD luidLow_ = 0;
    int width_ = 1920;
    int height_ = 1080;
    std::unique_ptr<FrameSink> sink_;

    PdfDocument pdf_;
    int curPage_ = 1;
    int publishedPage_ = 0;
    bool pendingPublish_ = false;
    std::uint8_t bg_[4] = {0, 0, 0, 255};
    std::map<int, std::vector<std::uint8_t>> cache_;

    std::thread convertThread_;
    std::atomic<bool> convertDone_{false};
    std::mutex convertMutex_;
    ConvertResult convertResult_;

#ifdef BSL_WITH_CEF
    std::unique_ptr<WebSession> web_;
#endif
};

} // namespace

} // namespace bsl::worker

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    using namespace bsl::worker;
    (void)instance;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring pipe, thumbIn, thumbOut;
    DWORD parent = 0;
    int thumbW = 160, thumbH = 90;
    bool thumb = false;
    bool cefSub = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--pipe" && i + 1 < argc)
            pipe = argv[++i];
        else if (a == L"--parent" && i + 1 < argc)
            parent = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 10));
        else if (a == L"--thumb" && i + 4 < argc) {
            thumb = true;
            thumbIn = argv[++i];
            thumbOut = argv[++i];
            thumbW = std::wcstol(argv[++i], nullptr, 10);
            thumbH = std::wcstol(argv[++i], nullptr, 10);
        } else if (a.rfind(L"--type=", 0) == 0) {
            cefSub = true;
        }
    }
    LocalFree(argv);

    if (cefSub) {
#ifdef BSL_WITH_CEF
        return cefSubprocessMain(instance);
#else
        return 1;
#endif
    }
    if (thumb)
        return runThumbnail(thumbIn, thumbOut, std::clamp(thumbW, 16, 1024), std::clamp(thumbH, 16, 1024));
    if (pipe.empty() || parent == 0)
        return 2;
    if (GetEnvironmentVariableW(L"BSLSTAGE_WORKER_CRASH", nullptr, 0) > 0)
        return 42; // so para teste: simula um worker que nunca sobe

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Worker worker;
    const int rc = worker.run(pipe, parent);
    if (SUCCEEDED(hr))
        CoUninitialize();
    return rc;
}
