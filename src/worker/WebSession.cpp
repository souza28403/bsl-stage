#include "worker/WebSession.h"

#include "worker/WorkerUtil.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace bsl::worker {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

fs::path moduleDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
}

fs::path cefRuntimeDir()
{
    return moduleDir() / L"cef";
}

// libcef.dll fica em <exe>/cef: tem de ser achada antes do primeiro uso (carga atrasada)
void addCefDllDirectory()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    SetDllDirectoryW(cefRuntimeDir().c_str());
}

int modifiersFrom(int mods)
{
    int m = 0;
    if (mods & 1)
        m |= EVENTFLAG_SHIFT_DOWN;
    if (mods & 2)
        m |= EVENTFLAG_CONTROL_DOWN;
    if (mods & 4)
        m |= EVENTFLAG_ALT_DOWN;
    return m;
}

} // namespace

int cefSubprocessMain(HINSTANCE instance)
{
    addCefDllDirectory();
    CefMainArgs args(instance);
    const int code = CefExecuteProcess(args, nullptr, nullptr);
    return code >= 0 ? code : 0;
}

// ----------------------------------------------------------------------------- Impl

struct WebSession::Impl
{
    class App;
    class Client;

    Impl(PipeChannel& c, const SinkRequest& r, HANDLE w) : ch(c), request(r), wake(w) {}

    PipeChannel& ch;
    SinkRequest request;
    HANDLE wake;

    std::unique_ptr<FrameSink> sink;
    CefRefPtr<App> app;
    CefRefPtr<Client> client;
    CefRefPtr<CefBrowser> browser;
    bool cefStarted = false;
    HANDLE slotMutex = nullptr;

    // Estado pedido pelo app
    std::string url = "about:blank";
    int width = 1920, height = 1080;
    double zoom = 1.0;
    int fps = 30;
    bool transparent = false;
    int reloadMinutes = 0;
    bool muted = true;
    std::string css;

    // Estado interno
    bool useGpu = false;       // shared_texture_enabled
    bool gpuEverWorked = false;
    int gpuFailures = 0;
    bool closing = false;      // fechando o navegador para recriar (transparencia / troca de caminho)
    bool closedFlag = false;
    bool browserGpu = false;          // com que opcoes o navegador atual foi criado
    bool browserTransparent = false;
    bool invalidatePending = false;
    Clock::time_point lastInvalidate{};
    Clock::time_point lastReload = Clock::now();
    bool gotFrame = false;
    Clock::time_point createdAt = Clock::now();
    bool fatalReported = false;
    unsigned long long frames = 0;
    std::vector<std::uint8_t> pending; // quadro de CPU que o sink recusou (app ainda segurava os buffers)
    int pendingW = 0, pendingH = 0;

    void send(const Json& j) { ch.send(j); }
    void status(const std::string& text, const char* level = "info")
    {
        send(Json{{"t", "status"}, {"text", text}, {"level", level}});
    }
    void fatal(const std::string& msg)
    {
        if (fatalReported)
            return;
        fatalReported = true;
        send(Json{{"t", "error"}, {"msg", msg}, {"fatal", true}});
        fileLog("web: " + msg);
    }

    bool startCef();
    void createBrowser();
    void applyBrowserSettings();
    void injectCss();
    void fallbackToCpu(const std::string& why);
    void onPaint(const void* buffer, int w, int h);
    void onAccelerated(HANDLE handle);
    void submitPending();
    void shutdown();
};

class WebSession::Impl::App final : public CefApp
{
public:
    explicit App(Impl* owner) : owner_(owner) {}

    void OnBeforeCommandLineProcessing(const CefString& /*process_type*/, CefRefPtr<CefCommandLine> command_line) override
    {
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        command_line->AppendSwitch("disable-background-networking");
        if (owner_ && owner_->request.haveLuid) {
            // Mantem a GPU do navegador no adaptador do app (Optimus): textura compartilhada abre no mesmo device
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%ld,%lu", static_cast<long>(owner_->request.luidHigh),
                          static_cast<unsigned long>(owner_->request.luidLow));
            wchar_t over[64];
            if (GetEnvironmentVariableW(L"BSLSTAGE_CEF_LUID", over, 64) > 0) // diagnostico: LUID alto,baixo forcado
                command_line->AppendSwitchWithValue("use-adapter-luid", toUtf8(over));
            else if (GetEnvironmentVariableW(L"BSLSTAGE_CEF_NO_LUID", over, 64) == 0)
                command_line->AppendSwitchWithValue("use-adapter-luid", buf);
        }
    }

private:
    Impl* owner_;
    IMPLEMENT_REFCOUNTING(App);
};

class WebSession::Impl::Client final : public CefClient,
                                       public CefRenderHandler,
                                       public CefLifeSpanHandler,
                                       public CefLoadHandler,
                                       public CefDisplayHandler
{
public:
    explicit Client(Impl* owner) : owner_(owner) {}

    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

    // --- CefRenderHandler ---
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override
    {
        rect = CefRect(0, 0, owner_->width, owner_->height);
    }

    bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override
    {
        info.device_scale_factor = 1.0f;
        info.depth = 32;
        info.depth_per_component = 8;
        info.is_monochrome = false;
        info.rect = CefRect(0, 0, owner_->width, owner_->height);
        info.available_rect = info.rect;
        return true;
    }

    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width,
                 int height) override
    {
        if (type == PET_VIEW)
            owner_->onPaint(buffer, width, height);
    }

    void OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
                            const CefAcceleratedPaintInfo& info) override
    {
        if (type == PET_VIEW)
            owner_->onAccelerated(info.shared_texture_handle);
    }

    // --- CefLifeSpanHandler ---
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        owner_->browser = browser;
        owner_->closedFlag = false;
        owner_->applyBrowserSettings();
    }

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int, const CefString& target_url,
                       const CefString&, WindowOpenDisposition, bool, const CefPopupFeatures&, CefWindowInfo&,
                       CefRefPtr<CefClient>&, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override
    {
        // Sem janelas extras: o link abre na propria pagina
        if (browser && !target_url.empty())
            browser->GetMainFrame()->LoadURL(target_url);
        return true;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser>) override
    {
        owner_->browser = nullptr;
        owner_->closedFlag = true;
    }

    // --- CefLoadHandler ---
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) override
    {
        if (frame && frame->IsMain())
            owner_->injectCss();
    }

    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override
    {
        if (!frame || !frame->IsMain() || errorCode == ERR_ABORTED)
            return;
        std::string text = "Falha ao carregar a página";
        const std::string detail = errorText.ToString();
        if (!detail.empty())
            text += ": " + detail;
        text += " (" + std::to_string(static_cast<int>(errorCode)) + ")";
        owner_->status(text, "warn");
        fileLog("web: " + text + " - " + failedUrl.ToString());
    }

    // --- CefDisplayHandler ---
    void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override
    {
        owner_->send(Json{{"t", "doc"}, {"pages", 0}, {"title", title.ToString()}});
    }

private:
    Impl* owner_;
    IMPLEMENT_REFCOUNTING(Client);
};

bool WebSession::Impl::startCef()
{
    addCefDllDirectory();
    const fs::path runtime = cefRuntimeDir();
    if (!fs::is_regular_file(runtime / L"libcef.dll")) {
        fatal("A Web está desabilitada: cef\\libcef.dll não foi encontrado ao lado do programa");
        return false;
    }

    // Perfil persistente por "vaga": instancias simultaneas nao podem dividir o mesmo diretorio do Chromium
    const fs::path root = localDataDir() / L"cef";
    fs::path slotDir;
    for (int i = 0; i < 8 && slotDir.empty(); ++i) {
        HANDLE m = CreateMutexW(nullptr, TRUE, (L"Local\\bslstage-cef-slot-" + std::to_wstring(i)).c_str());
        if (m && GetLastError() != ERROR_ALREADY_EXISTS) {
            slotMutex = m;
            slotDir = root / (L"slot-" + std::to_wstring(i));
        } else if (m) {
            CloseHandle(m);
        }
    }
    if (slotDir.empty())
        slotDir = root / (L"extra-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(slotDir / L"profile", ec);

    CefMainArgs args(GetModuleHandleW(nullptr));
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = false;
    settings.log_severity = LOGSEVERITY_WARNING;
    settings.persist_session_cookies = true;
    CefString(&settings.root_cache_path) = slotDir.wstring();
    CefString(&settings.cache_path) = (slotDir / L"profile").wstring();
    CefString(&settings.log_file) = (slotDir / L"cef.log").wstring();
    CefString(&settings.browser_subprocess_path) = (moduleDir() / L"bslstage_worker.exe").wstring();
    CefString(&settings.resources_dir_path) = runtime.wstring();
    CefString(&settings.locales_dir_path) = (runtime / L"locales").wstring();
    CefString(&settings.locale) = L"pt-BR";
    CefString(&settings.accept_language_list) = L"pt-BR,pt,en-US,en";

    app = new App(this);
    cefStarted = CefInitialize(args, settings, app.get(), nullptr);
    if (!cefStarted) {
        fatal("Não foi possível iniciar o navegador (CEF, código " + std::to_string(CefGetExitCode()) + ")");
        return false;
    }
    return true;
}

void WebSession::Impl::createBrowser()
{
    if (!client)
        client = new Client(this);
    CefWindowInfo info;
    info.SetAsWindowless(nullptr);
    info.shared_texture_enabled = useGpu;
    CefBrowserSettings bs;
    bs.windowless_frame_rate = fps;
    bs.background_color = transparent ? 0x00000000u : 0xFFFFFFFFu;
    CefBrowserHost::CreateBrowser(info, client, url, bs, nullptr, nullptr);
    closedFlag = false;
    browserGpu = useGpu;
    browserTransparent = transparent;
    createdAt = Clock::now();
}

void WebSession::Impl::applyBrowserSettings()
{
    if (!browser)
        return;
    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    host->SetAudioMuted(muted);
    host->SetZoomLevel(std::log(std::max(0.05, zoom)) / std::log(1.2));
    host->SetWindowlessFrameRate(fps);
    host->SetFocus(true);
}

void WebSession::Impl::injectCss()
{
    if (!browser)
        return;
    const std::string literal = Json(css).dump(-1, ' ', false, Json::error_handler_t::replace);
    const std::string js =
        "(function(){var s=document.getElementById('__bsl_css');"
        "if(!s){s=document.createElement('style');s.id='__bsl_css';(document.head||document.documentElement).appendChild(s);}"
        "s.textContent=" + literal + ";})();";
    browser->GetMainFrame()->ExecuteJavaScript(js, "bslstage://css", 0);
}

void WebSession::Impl::onPaint(const void* buffer, int w, int h)
{
    if (!sink || !buffer)
        return;
    if (w != sink->width() || h != sink->height()) {
        if (!sink->resize(w, h))
            return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    if (sink->submitBgra(bytes, w * 4, true, 0)) {
        pending.clear();
        if (!gotFrame) {
            gotFrame = true;
            status("");
            ch.log("info", "Web: quadros do navegador por cópia de memória (OnPaint)");
        }
        ++frames;
    } else {
        pending.assign(bytes, bytes + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
        pendingW = w;
        pendingH = h;
    }
}

void WebSession::Impl::submitPending()
{
    if (pending.empty() || !sink)
        return;
    if (pendingW == sink->width() && pendingH == sink->height()
        && sink->submitBgra(pending.data(), pendingW * 4, true, 0)) {
        pending.clear();
        ++frames;
        if (!gotFrame) {
            gotFrame = true;
            status("");
        }
    }
}

void WebSession::Impl::onAccelerated(HANDLE handle)
{
    if (!sink || !handle)
        return;
    std::string error;
    if (sink->submitSharedHandle(handle, 0, &error)) {
        gpuEverWorked = true;
        gpuFailures = 0;
        ++frames;
        if (!gotFrame) {
            gotFrame = true;
            status("");
            ch.log("info", "Web: quadros do navegador por textura compartilhada (OnAcceleratedPaint)");
        }
        return;
    }
    if (error.empty()) { // buffers do app ocupados: pede outro quadro para nao ficar com imagem velha
        invalidatePending = true;
        return;
    }
    ++gpuFailures;
    if (gpuFailures == 1)
        ch.log("warn", "textura do navegador: " + error);
    if (!gpuEverWorked) // a primeira textura nao abriu: outra GPU/adaptador; nao adianta insistir
        fallbackToCpu("textura compartilhada do navegador indisponivel neste adaptador");
}

// O CEF volta a entregar quadros por OnPaint (CPU); o app continua recebendo pelo mesmo canal
void WebSession::Impl::fallbackToCpu(const std::string& why)
{
    if (!useGpu)
        return;
    ch.log("warn", "Web: " + why + "; usando copia por CPU (OnPaint)");
    useGpu = false; // pump() recria o navegador sem shared_texture_enabled
}

void WebSession::Impl::shutdown()
{
    if (browser) {
        browser->GetHost()->CloseBrowser(true);
        const auto t0 = Clock::now();
        while (!closedFlag && Clock::now() - t0 < std::chrono::seconds(3)) {
            CefDoMessageLoopWork();
            Sleep(5);
        }
    }
    browser = nullptr;
    client = nullptr;
    if (cefStarted) {
        app = nullptr;
        CefShutdown();
        cefStarted = false;
    }
    sink.reset();
    if (slotMutex) {
        ReleaseMutex(slotMutex);
        CloseHandle(slotMutex);
        slotMutex = nullptr;
    }
}

// ----------------------------------------------------------------------------- WebSession

WebSession::WebSession(PipeChannel& channel, const SinkRequest& request, HANDLE wake)
    : impl_(std::make_unique<Impl>(channel, request, wake))
{
}

WebSession::~WebSession()
{
    impl_->shutdown();
}

void WebSession::setParams(const Json& p)
{
    Impl& d = *impl_;
    const double zoom = p.value("zoom", d.zoom);
    const int fps = p.value("fps", d.fps) >= 60 ? 60 : 30;
    const bool transparent = p.value("transparent", d.transparent);
    const bool muted = p.value("muted", d.muted);
    const std::string css = p.value("css", d.css);
    d.reloadMinutes = std::max(0, p.value("reloadMinutes", d.reloadMinutes));

    const bool cssChanged = css != d.css;
    const bool transparentChanged = transparent != d.transparent;
    d.zoom = zoom;
    d.fps = fps;
    d.muted = muted;
    d.css = css;
    d.transparent = transparent;
    if (transparentChanged)
        return; // a cor de fundo e definida na criacao: pump() recria o navegador
    d.applyBrowserSettings();
    if (cssChanged)
        d.injectCss();
}

void WebSession::open(const Json& m)
{
    Impl& d = *impl_;
    d.url = m.value("url", std::string("about:blank"));
    d.width = std::clamp(m.value("w", 1920), 16, 8192);
    d.height = std::clamp(m.value("h", 1080), 16, 8192);
    const Json p = m.value("params", Json::object());
    d.zoom = p.value("zoom", 1.0);
    d.fps = p.value("fps", 30) >= 60 ? 60 : 30;
    d.transparent = p.value("transparent", false);
    d.reloadMinutes = std::max(0, p.value("reloadMinutes", 0));
    d.muted = p.value("muted", true);
    d.css = p.value("css", std::string());

    d.status("Iniciando navegador…");
    d.sink = createFrameSink(d.request);
    if (!d.sink || !d.sink->resize(d.width, d.height)) {
        d.fatal("não foi possível criar o canal de imagem");
        return;
    }
    d.useGpu = d.sink->supportsGpuHandle();
    if (!d.startCef())
        return;
    d.createBrowser();
    d.status("Carregando página…");
}

void WebSession::resize(int w, int h)
{
    Impl& d = *impl_;
    if (w == d.width && h == d.height)
        return;
    d.width = w;
    d.height = h;
    if (d.sink && std::string(d.sink->kind()) == "texture" && d.useGpu) {
        // a textura nova e criada quando o primeiro quadro do tamanho novo chegar
    }
    if (d.browser) {
        d.browser->GetHost()->NotifyScreenInfoChanged();
        d.browser->GetHost()->WasResized();
    }
}

void WebSession::useShm()
{
    Impl& d = *impl_;
    if (!d.sink || std::string(d.sink->kind()) == "shm")
        return;
    d.request.transport = "shm";
    d.sink = createShmSink(d.request);
    if (d.sink)
        d.sink->resize(d.width, d.height);
    d.ch.send(Json{{"t", "transport"}, {"kind", "shm"}, {"why", "o app não conseguiu abrir a textura compartilhada"}});
    d.useGpu = false; // pump() recria o navegador sem shared_texture_enabled
}

void WebSession::handle(const Json& m)
{
    Impl& d = *impl_;
    const std::string t = m.value("t", std::string());
    if (t == "url") {
        d.url = m.value("url", d.url);
        if (d.browser)
            d.browser->GetMainFrame()->LoadURL(d.url);
        d.gotFrame = false;
        return;
    }
    if (t == "reload") {
        if (d.browser)
            d.browser->ReloadIgnoreCache();
        return;
    }
    if (!d.browser)
        return;
    CefRefPtr<CefBrowserHost> host = d.browser->GetHost();
    if (t == "mouse") {
        CefMouseEvent ev;
        ev.x = m.value("x", 0);
        ev.y = m.value("y", 0);
        const int button = m.value("button", 0);
        ev.modifiers = static_cast<uint32_t>(modifiersFrom(m.value("mods", 0)));
        const std::string type = m.value("type", std::string());
        if (type == "move") {
            if (button & 1)
                ev.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
            if (button & 2)
                ev.modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
            if (button & 4)
                ev.modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
            host->SendMouseMoveEvent(ev, false);
        } else if (type == "leave") {
            host->SendMouseMoveEvent(ev, true);
        } else if (type == "down" || type == "up") {
            const cef_mouse_button_type_t bt = button == 1 ? MBT_RIGHT : (button == 2 ? MBT_MIDDLE : MBT_LEFT);
            if (type == "down")
                host->SetFocus(true);
            host->SendMouseClickEvent(ev, bt, type == "up", std::max(1, m.value("clicks", 1)));
        }
    } else if (t == "wheel") {
        CefMouseEvent ev;
        ev.x = m.value("x", 0);
        ev.y = m.value("y", 0);
        ev.modifiers = 0;
        host->SendMouseWheelEvent(ev, static_cast<int>(std::lround(m.value("dx", 0.0))),
                                  static_cast<int>(std::lround(m.value("dy", 0.0))));
    } else if (t == "key") {
        CefKeyEvent ke;
        const std::string type = m.value("type", std::string());
        const int code = m.value("code", 0);
        const unsigned ch = m.value("ch", 0u);
        ke.modifiers = static_cast<uint32_t>(modifiersFrom(m.value("mods", 0)));
        ke.is_system_key = false;
        if (type == "char") {
            ke.type = KEYEVENT_CHAR;
            ke.windows_key_code = static_cast<int>(ch);
            ke.character = static_cast<char16_t>(ch);
            ke.unmodified_character = static_cast<char16_t>(ch);
        } else {
            ke.type = type == "up" ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
            ke.windows_key_code = code;
            ke.native_key_code = static_cast<int>((MapVirtualKeyW(static_cast<UINT>(code), MAPVK_VK_TO_VSC) << 16) | 1);
        }
        host->SendKeyEvent(ke);
    }
}

void WebSession::pump()
{
    Impl& d = *impl_;
    if (!d.cefStarted)
        return;
    CefDoMessageLoopWork();

    // Opcoes que so valem na criacao (textura compartilhada, fundo transparente): fecha e recria
    if (d.browser && !d.closing && (d.browserGpu != d.useGpu || d.browserTransparent != d.transparent)) {
        d.closing = true;
        d.browser->GetHost()->CloseBrowser(true);
    }
    if (d.closing && !d.browser && d.closedFlag) {
        d.closing = false;
        d.gotFrame = false;
        d.gpuFailures = 0;
        d.status("Carregando página…");
        d.createBrowser();
    }
    if (!d.browser)
        return;

    d.submitPending();
    const auto now = Clock::now();
    if (d.useGpu && !d.gotFrame && !d.closing && now - d.createdAt > std::chrono::seconds(10))
        d.fallbackToCpu("o navegador nao entregou quadros por textura em 10 s");
    if (d.invalidatePending && now - d.lastInvalidate > std::chrono::milliseconds(33)) {
        d.invalidatePending = false;
        d.lastInvalidate = now;
        d.browser->GetHost()->Invalidate(PET_VIEW);
    }
    if (d.reloadMinutes > 0 && now - d.lastReload > std::chrono::minutes(d.reloadMinutes)) {
        d.lastReload = now;
        d.browser->ReloadIgnoreCache();
    }
}

} // namespace bsl::worker
