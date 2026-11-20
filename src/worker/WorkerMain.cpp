#include "media/HapTranscode.h"
#include "media/WorkerProtocol.h"
#ifdef _WIN32
#include "worker/FfglScan.h"
#endif
#include "worker/FrameSink.h"
#include "worker/OfficeConverter.h"
#include "worker/PdfDocument.h"
#include "worker/PipeChannel.h"
#include "platform/Env.h"
#include "platform/Utf.h"
#include "worker/WakeEvent.h"
#include "worker/WorkerUtil.h"

#if BSL_WITH_CEF
#include "worker/WebSession.h"
#endif

#ifdef _WIN32
#include <objbase.h>
#include <shellapi.h>
#else
#include <signal.h>
#endif

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
    outBgra[0] = b(c[2]);
    outBgra[1] = b(c[1]);
    outBgra[2] = b(c[0]);
    outBgra[3] = b(c[3]);
}

int runThumbnail(const fs::path& in, const fs::path& out, int w, int h)
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
    fs::path tmp = out;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return 5;
        const std::int32_t head[3] = {w, h, pdf.pageCount()};
        f.write(reinterpret_cast<const char*>(head), sizeof(head));
        f.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }
    std::error_code ec;
    fs::rename(tmp, out, ec);
    if (ec) {
        fs::copy_file(tmp, out, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
    }
    return ec ? 6 : 0;
}

class Worker
{
public:
    Worker() = default;
    ~Worker() { joinConvert(); }

    int run(const std::string& endpoint, unsigned long parentPid)
    {
        parentPid_ = parentPid;
        parent_ = std::make_unique<ParentWatch>(parentPid);
        std::string error;
        if (!channel_.connect(endpoint, &wake_, &error)) {
            fileLog("pipe: " + error);
            return 10;
        }
        channel_.send(Json{{"t", "hello"}, {"pid", bslplat::currentProcessId()}, {"v", wp::kProtocolVersion}});
        lastPing_ = Clock::now();

        while (!quit_) {
            if (channel_.broken())
                break;
            if (parent_ && !parent_->alive())
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
#if BSL_WITH_CEF
            if (web_)
                web_->pump();
#endif
            unsigned timeout = 15;
            if (pendingPublish_)
                timeout = 5;
            else if (needsPrerender() || convertDone_.load())
                timeout = 0;
#if BSL_WITH_CEF
            if (web_)
                timeout = std::min<unsigned>(timeout, 4);
#endif
            if (!quit_) {
                prerenderStep();
                wake_.wait(timeout);
            }
        }
        shutdown();
        return 0;
    }

private:
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
#if BSL_WITH_CEF
        else if (web_) {
            web_->handle(m);
        }
#endif
    }

    void simulateHangOnce()
    {
        const std::string name = envUtf8("BSLSTAGE_WORKER_HANG_ONCE");
        if (name.empty())
            return;
        const fs::path marker = pathFromUtf8(name);
        std::error_code ec;
        if (fs::exists(marker, ec))
            return;
        std::ofstream(marker) << "hang";
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
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
            luidHigh_ = m["luid"][0].get<long>();
            luidLow_ = m["luid"][1].get<unsigned long>();
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
            const fs::path file = pathFromUtf8(m.value("path", std::string()));
            if (isOfficeExtension(lowerExt(file))) {
                channel_.send(Json{{"t", "status"}, {"text", "Convertendo…"}, {"level", "info"}});
                startConvert(file);
            } else {
                loadPdf(file);
            }
        } else if (kind_ == "web") {
#if BSL_WITH_CEF
            web_ = std::make_unique<WebSession>(channel_, sinkRequest(), wake_.native());
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
#if BSL_WITH_CEF
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
#if BSL_WITH_CEF
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
#if BSL_WITH_CEF
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

    void startConvert(const fs::path& file)
    {
        convertThread_ = std::thread([this, file] {
            ConvertResult r = convertToPdf(file);
            {
                std::lock_guard lk(convertMutex_);
                convertResult_ = std::move(r);
            }
            convertDone_ = true;
            wake_.set();
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
#if BSL_WITH_CEF
        web_.reset();
#endif
        joinConvert();
        pdf_.close();
        sink_.reset();
    }

    WakeEvent wake_;
    PipeChannel channel_;
    std::unique_ptr<ParentWatch> parent_;
    unsigned long parentPid_ = 0;
    bool quit_ = false;
    bool opened_ = false;
    Clock::time_point lastPing_;

    std::string kind_;
    std::string transport_ = "auto";
    bool haveLuid_ = false;
    long luidHigh_ = 0;
    unsigned long luidLow_ = 0;
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

#if BSL_WITH_CEF
    std::unique_ptr<WebSession> web_;
#endif
};

void writeJsonAtomic(const fs::path& file, const nlohmann::json& j)
{
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return;
        f << j.dump();
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec)
        fs::remove(tmp, ec);
}

int runHapConvert(const fs::path& jobFile, const fs::path& progressFile)
{
    auto failWith = [&](const std::string& msg) {
        writeJsonAtomic(progressFile, nlohmann::json{{"state", "failed"}, {"error", msg}});
        return 1;
    };

    bsl::media::HapJob job;
    try {
        std::ifstream f(jobFile, std::ios::binary);
        if (!f)
            return failWith("Nao foi possivel ler o arquivo do trabalho.");
        const nlohmann::json j = nlohmann::json::parse(f);
        job.source = pathFromUtf8(j.value("source", std::string()));
        job.target = pathFromUtf8(j.value("target", std::string()));
        job.maxWidth = j.value("maxWidth", 0);
        job.maxHeight = j.value("maxHeight", 0);
    } catch (const std::exception&) {
        return failWith("Arquivo do trabalho invalido.");
    }
    if (job.source.empty() || job.target.empty())
        return failWith("Origem ou destino nao informados.");

    std::atomic<bool> cancel{false};
    auto last = Clock::now() - std::chrono::seconds(1);
    const auto onProgress = [&](double p) {
        const auto now = Clock::now();
        if (now - last < std::chrono::milliseconds(250))
            return;
        last = now;
        writeJsonAtomic(progressFile, nlohmann::json{{"state", "running"}, {"progress", p}});
    };

    const bsl::media::HapResult res = bsl::media::transcodeToHap(job, onProgress, cancel);
    if (!res.ok)
        return failWith(res.error);
    writeJsonAtomic(progressFile, nlohmann::json{{"state", "done"},
                                                 {"width", res.width},
                                                 {"height", res.height},
                                                 {"alpha", res.alpha},
                                                 {"frames", res.frames},
                                                 {"fps", res.fps}});
    return 0;
}

}

}

#ifdef _WIN32

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    using namespace bsl::worker;
    (void)instance;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring pipe, thumbIn, thumbOut, scanDll, scanOut, panelUrl, panelTitle = L"BSL Stage";
    std::wstring hapJob, hapProgress;
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
        } else if (a == L"--scan-ffgl" && i + 1 < argc) {
            scanDll = argv[++i];
            if (i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0)
                scanOut = argv[++i];
        } else if (a == L"--hap-convert" && i + 2 < argc) {
            hapJob = argv[++i];
            hapProgress = argv[++i];
        } else if (a == L"--panel" && i + 1 < argc) {
            panelUrl = argv[++i];
        } else if (a == L"--title" && i + 1 < argc) {
            panelTitle = argv[++i];
        } else if (a.rfind(L"--type=", 0) == 0) {
            cefSub = true;
        }
    }
    LocalFree(argv);

    if (cefSub) {
#if BSL_WITH_CEF
        return cefSubprocessMain(instance);
#else
        return 1;
#endif
    }
    if (!hapJob.empty())
        return runHapConvert(fs::path(hapJob), fs::path(hapProgress));
    if (!scanDll.empty())
        runFfglScan(scanDll, scanOut);
    if (thumb)
        return runThumbnail(fs::path(thumbIn), fs::path(thumbOut), std::clamp(thumbW, 16, 1024), std::clamp(thumbH, 16, 1024));
    if (!panelUrl.empty()) {
#if BSL_WITH_CEF
        return runPanelWindow(instance, panelUrl, panelTitle, parent);
#else
        return 1;
#endif
    }
    if (pipe.empty() || parent == 0)
        return 2;
    if (GetEnvironmentVariableW(L"BSLSTAGE_WORKER_CRASH", nullptr, 0) > 0)
        return 42;

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Worker worker;
    const int rc = worker.run(bslplat::wideToUtf8(pipe), parent);
    if (SUCCEEDED(hr))
        CoUninitialize();
    return rc;
}

#else

int main(int argc, char** argv)
{
    using namespace bsl::worker;
    signal(SIGPIPE, SIG_IGN);

    std::string endpoint, thumbIn, thumbOut;
    unsigned long parent = 0;
    int thumbW = 160, thumbH = 90;
    bool thumb = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fd" && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (a == "--parent" && i + 1 < argc) {
            parent = std::strtoul(argv[++i], nullptr, 10);
        } else if (a == "--thumb" && i + 4 < argc) {
            thumb = true;
            thumbIn = argv[++i];
            thumbOut = argv[++i];
            thumbW = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            thumbH = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
    }

    if (thumb)
        return runThumbnail(pathFromUtf8(thumbIn), pathFromUtf8(thumbOut), std::clamp(thumbW, 16, 1024),
                            std::clamp(thumbH, 16, 1024));
    if (endpoint.empty() || parent == 0)
        return 2;
    if (!envUtf8("BSLSTAGE_WORKER_CRASH").empty())
        return 42;

    Worker worker;
    return worker.run(endpoint, parent);
}

#endif
