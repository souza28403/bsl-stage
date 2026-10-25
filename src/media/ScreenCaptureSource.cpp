#include "media/ScreenCaptureSource.h"

#include "app/Log.h"
#include "media/CaptureTargets.h"
#include "render/D3D11Interop.h"

#include <unknwn.h>
#include <windows.h>
#include <inspectable.h>

#include <d3d11.h>
#include <dxgi1_5.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#pragma warning(push)
#pragma warning(disable : 4458 4100 4189 4702 4244)
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#pragma warning(pop)

#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

namespace bsl::media {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wgd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace {

CaptureBackend g_forcedBackend = CaptureBackend::Auto;
bool g_forceCpu = false;

// Acesso ao ID3D11Texture2D por tras de uma IDirect3DSurface (a interface nao vem nos headers C++/WinRT)
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDxgiSurfaceAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
};

void ensureApartment()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // MTA; se a thread ja esta em STA (RPC_E_CHANGED_MODE) o WinRT funciona do mesmo jeito
        RoInitialize(RO_INIT_MULTITHREADED);
    });
}

std::string errorText(const winrt::hresult_error& e)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(e.code().value));
    return winrt::to_string(e.message()) + " (" + buf + ")";
}

struct Region
{
    int x = 0, y = 0, w = 0, h = 0;
};

const std::vector<ParamOption> kFps = {{"15", "15 fps"}, {"30", "30 fps"}, {"60", "60 fps"}};

} // namespace

// ----------------------------------------------------------------------------- Impl

struct ScreenCaptureSource::Impl
{
    enum class State { Idle, Capturing, Waiting, Failed };
    enum class Backend { None, Wgc, Dup };

    struct Settings
    {
        Json target;
        int rx = 0, ry = 0, rw = 0, rh = 0;
        bool cursor = true;
        int fps = 30;
        bool borderless = true;
    };

    // Desktop Duplication (so monitor, device proprio no adaptador do monitor)
    struct Dup
    {
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        ComPtr<IDXGIOutputDuplication> dup;
        ComPtr<ID3D11Texture2D> staging;
        int stagingW = 0, stagingH = 0;
        int deskW = 0, deskH = 0;
    };

    explicit Impl(ScreenCaptureSource& s) : self(s) { ensureApartment(); }

    ScreenCaptureSource& self;
    std::shared_ptr<render::D3D11Interop> interop;

    Settings cur;
    bool haveSettings = false;
    bool paramsDirty = true;

    State state = State::Idle;
    Backend backend = Backend::None;
    std::string stateText;
    std::string borderInfo;
    Clock::time_point lastRetry{}, lastCopy{}, startedAt{};
    unsigned long long frames = 0;

    // WGC
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wgd3d::IDirect3DDevice rtDevice{nullptr};
    winrt::event_token closedToken{};
    std::shared_ptr<std::atomic<bool>> closedFlag;
    winrt::Windows::Graphics::SizeInt32 poolSize{};

    // Saida
    std::unique_ptr<render::InteropTexture> tex; // WGC
    GLuint ddTex = 0;                            // Desktop Duplication
    int ddTexW = 0, ddTexH = 0;
    int outW = 0, outH = 0;
    Dup dup;
    bool pathLogged = false;

    // --------------------------------------------------------------------- configuracao

    Settings readSettings() const
    {
        const ParamSet p = self.currentParams();
        Settings s;
        s.target = p.raw("target");
        s.rx = p.i("regionX");
        s.ry = p.i("regionY");
        s.rw = p.i("regionW");
        s.rh = p.i("regionH");
        s.cursor = p.b("cursor");
        s.fps = std::atoi(p.s("fps").c_str());
        if (s.fps != 15 && s.fps != 60)
            s.fps = 30;
        s.borderless = p.b("borderless");
        return s;
    }

    void applySettings()
    {
        paramsDirty = false;
        const Settings n = readSettings();
        const bool restart = !haveSettings || n.target != cur.target;
        const Settings old = cur;
        cur = n;
        haveSettings = true;
        if (restart) {
            start();
            return;
        }
        if (session) {
            try {
                if (n.cursor != old.cursor)
                    session.IsCursorCaptureEnabled(n.cursor);
                if (n.borderless != old.borderless)
                    applyBorder();
                applyMinInterval();
            } catch (const winrt::hresult_error& e) {
                log::warn("Captura de tela: " + errorText(e));
            }
        }
    }

    Region region(int cw, int ch) const
    {
        Region r;
        r.x = std::clamp(cur.rx, 0, std::max(0, cw - 1));
        r.y = std::clamp(cur.ry, 0, std::max(0, ch - 1));
        r.w = cur.rw > 0 ? std::min(cur.rw, cw - r.x) : cw - r.x;
        r.h = cur.rh > 0 ? std::min(cur.rh, ch - r.y) : ch - r.y;
        r.w = std::max(1, r.w);
        r.h = std::max(1, r.h);
        return r;
    }

    // --------------------------------------------------------------------- inicio / parada

    void stopWgc()
    {
        try {
            if (item && closedToken.value)
                item.Closed(closedToken);
        } catch (...) {
        }
        closedToken = {};
        try {
            if (session)
                session.Close();
            if (pool)
                pool.Close();
        } catch (...) {
        }
        session = nullptr;
        pool = nullptr;
        item = nullptr;
        rtDevice = nullptr;
        closedFlag.reset();
    }

    void stopDup()
    {
        dup = Dup{};
    }

    void stop()
    {
        stopWgc();
        stopDup();
        backend = Backend::None;
        if (state == State::Capturing)
            state = State::Idle;
    }

    void releaseOutput()
    {
        tex.reset();
        if (ddTex) {
            glDeleteTextures(1, &ddTex);
            ddTex = 0;
        }
        ddTexW = ddTexH = outW = outH = 0;
    }

    void ensureTex(int w, int h)
    {
        if (tex && tex->width() == w && tex->height() == h)
            return;
        tex.reset();
        tex = interop->createTexture(w, h, g_forceCpu);
        if (!tex) {
            log::warn("Captura de tela: não foi possível criar a textura " + std::to_string(w) + "x" + std::to_string(h));
            return;
        }
        outW = w;
        outH = h;
        if (!pathLogged) {
            pathLogged = true;
            log::info(std::string("Captura de tela [") + (backend == Backend::Wgc ? "Windows Graphics Capture" : "DXGI Desktop Duplication") +
                      "]: " + interop->describe() + " | textura em uso: " +
                      (tex->usesInterop() ? "interop (GPU)" : "cópia por CPU"));
        }
    }

    void setWaiting(const std::string& text)
    {
        stop();
        state = State::Waiting;
        stateText = text;
        lastRetry = Clock::now();
        if (tex)
            tex->clear(0.0f, 0.0f, 0.0f, 1.0f); // sem sinal: preto
    }

    void start()
    {
        stop();
        frames = 0;
        startedAt = Clock::now();
        pathLogged = false;
        stateText.clear();

        const Json& t = cur.target;
        const std::string kind = t.is_object() ? t.value("kind", std::string("monitor")) : "monitor";
        HWND hwnd = nullptr;
        HMONITOR mon = nullptr;
        if (kind == "window") {
            const std::string exe = t.value("exe", std::string());
            const std::string title = t.value("title", std::string());
            hwnd = reinterpret_cast<HWND>(findWindow(exe, title));
            if (!hwnd) {
                setWaiting("Aguardando a janela: " + (title.empty() ? exe : title));
                return;
            }
        } else {
            const int index = (t.is_object() && t.contains("index") && t["index"].is_number_integer()) ? t["index"].get<int>() : 1;
            for (const MonitorTarget& m : listMonitors())
                if (m.index == index)
                    mon = reinterpret_cast<HMONITOR>(m.handle);
            if (!mon) {
                setWaiting("Monitor " + std::to_string(index) + " não encontrado");
                return;
            }
        }

        std::string error;
        bool ok = false;
        if (g_forcedBackend != CaptureBackend::DesktopDuplication)
            ok = startWgc(hwnd, mon, error);
        if (!ok && mon && g_forcedBackend != CaptureBackend::WindowsGraphicsCapture) {
            if (!error.empty())
                log::warn("Captura de tela: Windows Graphics Capture indisponível (" + error + "); usando DXGI Desktop Duplication");
            ok = startDup(mon, error);
        }
        if (!ok) {
            state = State::Failed;
            stateText = error.empty() ? "captura indisponível" : error;
            log::warn("Captura de tela: " + stateText);
            return;
        }
        state = State::Capturing;
    }

    void applyBorder()
    {
        if (!session)
            return;
        if (cur.borderless) {
            static int access = -1; // -1: ainda nao pediu; 0: negado; 1: concedido
            if (access < 0) {
                access = 0;
                try {
                    auto op = wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless);
                    for (int i = 0; i < 50 && op.Status() == winrt::Windows::Foundation::AsyncStatus::Started; ++i)
                        Sleep(10);
                    if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
                        access = op.GetResults() == winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus::Allowed ? 1 : 0;
                } catch (const winrt::hresult_error&) {
                    // API ausente (Windows mais antigo): tenta direto abaixo
                }
            }
        }
        try {
            session.IsBorderRequired(!cur.borderless);
            borderInfo = cur.borderless ? "sem borda amarela" : "";
        } catch (const winrt::hresult_error& e) {
            borderInfo = "borda amarela mantida (o Windows não permitiu removê-la)";
            if (cur.borderless)
                log::info("Captura de tela: IsBorderRequired(false) não aceito: " + errorText(e));
        }
    }

    void applyMinInterval()
    {
        try {
            session.MinUpdateInterval(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                std::chrono::microseconds(1000000 / std::max(1, cur.fps))));
        } catch (const winrt::hresult_error&) {
            // Windows anterior ao 11 22H2: o limite de FPS fica por conta do update()
        }
    }

    bool startWgc(HWND hwnd, HMONITOR mon, std::string& error)
    {
        try {
            if (!wgc::GraphicsCaptureSession::IsSupported()) {
                error = "Windows Graphics Capture não é suportado nesta versão do Windows";
                return false;
            }
            auto factory = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            if (hwnd)
                winrt::check_hresult(factory->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                              winrt::put_abi(item)));
            else
                winrt::check_hresult(factory->CreateForMonitor(mon, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                               winrt::put_abi(item)));

            winrt::com_ptr<IDXGIDevice> dxgi;
            winrt::check_hresult(interop->device()->QueryInterface(IID_PPV_ARGS(dxgi.put())));
            winrt::com_ptr<::IInspectable> inspectable;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
            rtDevice = inspectable.as<wgd3d::IDirect3DDevice>();

            poolSize = item.Size();
            if (poolSize.Width <= 0 || poolSize.Height <= 0)
                throw winrt::hresult_error(E_FAIL, L"alvo com tamanho zero");
            pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                rtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, poolSize);
            session = pool.CreateCaptureSession(item);

            closedFlag = std::make_shared<std::atomic<bool>>(false);
            const auto flag = closedFlag;
            closedToken = item.Closed([flag](auto&&, auto&&) { *flag = true; });

            try {
                session.IsCursorCaptureEnabled(cur.cursor);
            } catch (const winrt::hresult_error&) {
            }
            applyBorder();
            applyMinInterval();
            session.StartCapture();

            backend = Backend::Wgc;
            // Textura inicial (preta) ja no tamanho da regiao: o compositor nunca ve textura invalida
            const Region r = region(poolSize.Width, poolSize.Height);
            ensureTex(r.w, r.h);
            if (tex)
                tex->clear(0.0f, 0.0f, 0.0f, 1.0f);
            return tex != nullptr;
        } catch (const winrt::hresult_error& e) {
            error = errorText(e);
            stopWgc();
            return false;
        }
    }

    bool startDup(HMONITOR mon, std::string& error)
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            error = "CreateDXGIFactory1 falhou";
            return false;
        }
        for (UINT ai = 0;; ++ai) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(ai, &adapter) == DXGI_ERROR_NOT_FOUND)
                break;
            for (UINT oi = 0;; ++oi) {
                ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(oi, &output) == DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_OUTPUT_DESC od{};
                output->GetDesc(&od);
                if (od.Monitor != mon)
                    continue;
                // O device precisa estar no adaptador que dirige o monitor (no Optimus, o da Intel)
                const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
                Dup d;
                if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                             levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &d.dev, nullptr,
                                             &d.ctx))) {
                    error = "D3D11CreateDevice falhou no adaptador do monitor";
                    return false;
                }
                ComPtr<IDXGIOutput1> out1;
                if (FAILED(output.As(&out1))) {
                    error = "IDXGIOutput1 indisponível";
                    return false;
                }
                HRESULT hr = E_FAIL;
                // DuplicateOutput1 (Win10 1803+) funciona em mais configuracoes de GPU hibrida que o DuplicateOutput
                ComPtr<IDXGIOutput5> out5;
                if (SUCCEEDED(output.As(&out5))) {
                    const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
                    hr = out5->DuplicateOutput1(d.dev.Get(), 0, 1, formats, &d.dup);
                }
                if (FAILED(hr))
                    hr = out1->DuplicateOutput(d.dev.Get(), &d.dup);
                if (FAILED(hr)) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(hr));
                    error = std::string("DuplicateOutput falhou ") + buf;
                    return false;
                }
                d.deskW = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
                d.deskH = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
                dup = std::move(d);
                backend = Backend::Dup;
                const Region r = region(dup.deskW, dup.deskH);
                outW = r.w;
                outH = r.h;
                uploadDup(nullptr, 0, r.w, r.h); // textura GL preta
                if (!pathLogged) {
                    pathLogged = true;
                    log::info("Captura de tela [DXGI Desktop Duplication]: cópia por CPU (staging -> glTexSubImage2D), sem cursor");
                }
                return true;
            }
        }
        error = "monitor não encontrado nos adaptadores DXGI";
        return false;
    }

    // --------------------------------------------------------------------- quadros

    void uploadDup(const void* data, UINT rowPitch, int w, int h)
    {
        GLint prevTex = 0, prevAlign = 0, prevRow = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow);
        if (!ddTex) {
            glGenTextures(1, &ddTex);
            glBindTexture(GL_TEXTURE_2D, ddTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
            ddTexW = ddTexH = 0;
        }
        glBindTexture(GL_TEXTURE_2D, ddTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowPitch ? static_cast<GLint>(rowPitch / 4) : 0);
        if (ddTexW != w || ddTexH != h) {
            std::vector<unsigned char> black;
            if (!data)
                black.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, data ? data : black.data());
            ddTexW = w;
            ddTexH = h;
        } else if (data) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, data);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
        outW = w;
        outH = h;
    }

    bool gatePassed(Clock::time_point now) const
    {
        const double interval = 1.0 / std::max(1, cur.fps);
        return std::chrono::duration<double>(now - lastCopy).count() >= interval * 0.9;
    }

    void pumpWgc(Clock::time_point now)
    {
        if (!pool)
            return;
        if (closedFlag && *closedFlag) {
            const Json t = cur.target;
            log::warn("Captura de tela: o alvo foi fechado; procurando de novo a cada segundo");
            setWaiting(t.is_object() && t.value("kind", std::string()) == "window"
                           ? "Janela fechada, aguardando ela voltar: " + t.value("title", std::string())
                           : "Monitor desconectado, aguardando");
            return;
        }
        if (!gatePassed(now))
            return;
        try {
            wgc::Direct3D11CaptureFrame last{nullptr};
            for (;;) {
                auto f = pool.TryGetNextFrame();
                if (!f)
                    break;
                if (last)
                    last.Close();
                last = f;
            }
            if (!last)
                return;

            const auto cs = last.ContentSize();
            if (cs.Width > 0 && cs.Height > 0) {
                auto access = last.Surface().as<IDxgiSurfaceAccess>();
                winrt::com_ptr<ID3D11Texture2D> src;
                winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), src.put_void()));
                D3D11_TEXTURE2D_DESC sd{};
                src->GetDesc(&sd);
                const int cw = std::min(cs.Width, static_cast<int>(sd.Width));
                const int ch = std::min(cs.Height, static_cast<int>(sd.Height));
                const Region r = region(cw, ch);
                ensureTex(r.w, r.h);
                if (tex) {
                    const D3D11_BOX box{static_cast<UINT>(r.x), static_cast<UINT>(r.y), 0, static_cast<UINT>(r.x + r.w),
                                        static_cast<UINT>(r.y + r.h), 1};
                    tex->beginD3D();
                    interop->context()->CopySubresourceRegion(tex->d3d(), 0, 0, 0, 0, src.get(), 0, &box);
                    tex->endD3D();
                    ++frames;
                    lastCopy = now;
                }
            }
            if (cs.Width != poolSize.Width || cs.Height != poolSize.Height) {
                poolSize = cs; // a janela mudou de tamanho: os proximos quadros vem no tamanho novo
                if (cs.Width > 0 && cs.Height > 0)
                    pool.Recreate(rtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, cs);
            }
            last.Close();
        } catch (const winrt::hresult_error& e) {
            log::warn("Captura de tela: " + errorText(e));
            setWaiting("Captura interrompida, tentando de novo");
        }
    }

    void pumpDup(Clock::time_point now)
    {
        if (!dup.dup || !gatePassed(now))
            return;
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> res;
        const HRESULT hr = dup.dup->AcquireNextFrame(0, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return;
        if (FAILED(hr)) { // DXGI_ERROR_ACCESS_LOST (troca de resolucao, UAC...): recria a duplicacao
            setWaiting("Captura perdida, tentando de novo");
            return;
        }
        if (info.LastPresentTime.QuadPart != 0) { // 0 = so o ponteiro do mouse mudou
            ComPtr<ID3D11Texture2D> src;
            if (SUCCEEDED(res.As(&src))) {
                D3D11_TEXTURE2D_DESC sd{};
                src->GetDesc(&sd);
                const Region r = region(static_cast<int>(sd.Width), static_cast<int>(sd.Height));
                if (!dup.staging || dup.stagingW != r.w || dup.stagingH != r.h) {
                    D3D11_TEXTURE2D_DESC st = sd;
                    st.Width = static_cast<UINT>(r.w);
                    st.Height = static_cast<UINT>(r.h);
                    st.Usage = D3D11_USAGE_STAGING;
                    st.BindFlags = 0;
                    st.MiscFlags = 0;
                    st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    dup.staging.Reset();
                    dup.stagingW = dup.stagingH = 0;
                    if (SUCCEEDED(dup.dev->CreateTexture2D(&st, nullptr, &dup.staging))) {
                        dup.stagingW = r.w;
                        dup.stagingH = r.h;
                    }
                }
                if (dup.staging) {
                    const D3D11_BOX box{static_cast<UINT>(r.x), static_cast<UINT>(r.y), 0, static_cast<UINT>(r.x + r.w),
                                        static_cast<UINT>(r.y + r.h), 1};
                    dup.ctx->CopySubresourceRegion(dup.staging.Get(), 0, 0, 0, 0, src.Get(), 0, &box);
                    D3D11_MAPPED_SUBRESOURCE m{};
                    if (SUCCEEDED(dup.ctx->Map(dup.staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
                        uploadDup(m.pData, m.RowPitch, r.w, r.h);
                        dup.ctx->Unmap(dup.staging.Get(), 0);
                        ++frames;
                        lastCopy = now;
                    }
                }
            }
        }
        dup.dup->ReleaseFrame();
    }

    void update()
    {
        if (paramsDirty)
            applySettings();
        const auto now = Clock::now();
        switch (state) {
        case State::Capturing:
            if (backend == Backend::Wgc)
                pumpWgc(now);
            else if (backend == Backend::Dup)
                pumpDup(now);
            break;
        case State::Waiting:
            if (now - lastRetry >= std::chrono::seconds(1)) {
                lastRetry = now;
                start();
                if (state == State::Capturing)
                    log::info("Captura de tela: alvo encontrado de novo");
            }
            break;
        case State::Failed:
            if (now - lastRetry >= std::chrono::seconds(3)) {
                lastRetry = now;
                start();
            }
            break;
        case State::Idle:
            break;
        }
    }
};

// ----------------------------------------------------------------------------- ScreenCaptureSource

ScreenCaptureSource::ScreenCaptureSource() = default;

ScreenCaptureSource::~ScreenCaptureSource()
{
    close();
}

void ScreenCaptureSource::forceBackend(CaptureBackend backend)
{
    g_forcedBackend = backend;
}

void ScreenCaptureSource::forceCpuPath(bool cpu)
{
    g_forceCpu = cpu;
}

std::vector<ParamDesc> ScreenCaptureSource::describe(const ParamValues&)
{
    using namespace param;
    std::vector<ParamDesc> d;
    d.push_back(makeCustom("target", "Alvo", "screen-target", monitorTargetJson(1), "Captura"));
    d.push_back(makeBool("cursor", "Mostrar cursor", true));
    d.push_back(makeEnum("fps", "FPS máx.", kFps, "30"));
    d.push_back(makeBool("borderless", "Sem borda amarela", true));
    d.back().tooltip = "Remove a borda amarela do Windows ao redor do alvo, quando o Windows permitir (11 22H2 ou mais novo).";
    d.push_back(makeInt("regionX", "Região X", 0, 0, 16384, "%d px", "Região"));
    d.push_back(makeInt("regionY", "Região Y", 0, 0, 16384, "%d px"));
    d.push_back(makeInt("regionW", "Região largura", 0, 0, 16384, "%d px"));
    d.back().tooltip = "0 = até o fim do alvo.";
    d.push_back(makeInt("regionH", "Região altura", 0, 0, 16384, "%d px"));
    d.back().tooltip = "0 = até o fim do alvo.";
    return d;
}

std::string ScreenCaptureSource::label(const ParamValues& values)
{
    const ParamSet p(describe(values), values);
    const Json t = p.raw("target");
    if (t.is_object() && t.value("kind", std::string()) == "window") {
        std::string title = t.value("title", std::string());
        if (title.size() > 24) {
            size_t cut = 22;
            while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80)
                --cut;
            title = title.substr(0, cut) + "…";
        }
        return "Captura: " + (title.empty() ? t.value("exe", std::string("janela")) : title);
    }
    const int index = (t.is_object() && t.contains("index") && t["index"].is_number_integer()) ? t["index"].get<int>() : 1;
    return "Captura: monitor " + std::to_string(index);
}

bool ScreenCaptureSource::open(const std::filesystem::path& /*path*/, std::string* error)
{
    close();
    std::string err;
    auto interop = render::D3D11Interop::shared(&err);
    if (!interop) {
        if (error)
            *error = err;
        return false;
    }
    impl_ = std::make_unique<Impl>(*this);
    impl_->interop = std::move(interop);
    clock_ = ClipClock{};
    open_ = true;
    impl_->paramsDirty = true;
    impl_->applySettings();
    return true;
}

void ScreenCaptureSource::close()
{
    if (impl_) {
        impl_->stop();
        impl_->releaseOutput();
        impl_.reset(); // solta o interop por ultimo (as texturas ja foram liberadas)
    }
    open_ = false;
}

void ScreenCaptureSource::onParamsChanged()
{
    if (impl_)
        impl_->paramsDirty = true;
}

void ScreenCaptureSource::update(double /*dt*/)
{
    if (impl_)
        impl_->update();
}

GLuint ScreenCaptureSource::texture() const
{
    if (!impl_)
        return 0;
    if (impl_->backend == Impl::Backend::Dup)
        return impl_->ddTex;
    return impl_->tex ? impl_->tex->gl() : 0;
}

int ScreenCaptureSource::width() const
{
    return impl_ ? impl_->outW : 0;
}

int ScreenCaptureSource::height() const
{
    return impl_ ? impl_->outH : 0;
}

SourceStatus ScreenCaptureSource::status() const
{
    if (!impl_)
        return {};
    using Level = SourceStatus::Level;
    switch (impl_->state) {
    case Impl::State::Waiting: return {Level::Warning, impl_->stateText};
    case Impl::State::Failed: return {Level::Warning, "Captura indisponível: " + impl_->stateText};
    case Impl::State::Capturing: {
        if (impl_->frames == 0 &&
            Clock::now() - impl_->startedAt > std::chrono::seconds(2))
            return {Level::Info, "Aguardando o primeiro quadro (a tela pode estar parada ou minimizada)"};
        std::string text = backendName() + " | " + pathName();
        if (!impl_->borderInfo.empty())
            text += " | " + impl_->borderInfo;
        return {Level::Info, text};
    }
    default: return {};
    }
}

std::string ScreenCaptureSource::backendName() const
{
    if (!impl_)
        return {};
    switch (impl_->backend) {
    case Impl::Backend::Wgc: return "Windows Graphics Capture";
    case Impl::Backend::Dup: return "DXGI Desktop Duplication";
    default: return {};
    }
}

std::string ScreenCaptureSource::pathName() const
{
    if (!impl_ || impl_->backend == Impl::Backend::None)
        return {};
    return usesInterop() ? "interop (GPU)" : "cópia por CPU";
}

bool ScreenCaptureSource::usesInterop() const
{
    return impl_ && impl_->backend == Impl::Backend::Wgc && impl_->tex && impl_->tex->usesInterop();
}

unsigned long long ScreenCaptureSource::frameCount() const
{
    return impl_ ? impl_->frames : 0;
}

} // namespace bsl::media
