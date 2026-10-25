#include "render/D3D11Interop.h"

#include "app/Log.h"

#include <windows.h>
#include <d3d11_4.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace bsl::render {

using Microsoft::WRL::ComPtr;

namespace {

constexpr GLenum kWglAccessReadOnly = 0x0000; // WGL_ACCESS_READ_ONLY_NV

using PfnGetExtensionsString = const char*(WINAPI*)(HDC);
using PfnDxOpenDevice = HANDLE(WINAPI*)(void*);
using PfnDxCloseDevice = BOOL(WINAPI*)(HANDLE);
using PfnDxRegisterObject = HANDLE(WINAPI*)(HANDLE, void*, GLuint, GLenum, GLenum);
using PfnDxUnregisterObject = BOOL(WINAPI*)(HANDLE, HANDLE);
using PfnDxLockObjects = BOOL(WINAPI*)(HANDLE, GLint, HANDLE*);
using PfnDxUnlockObjects = BOOL(WINAPI*)(HANDLE, GLint, HANDLE*);

struct WglDx
{
    PfnDxOpenDevice open = nullptr;
    PfnDxCloseDevice close = nullptr;
    PfnDxRegisterObject registerObject = nullptr;
    PfnDxUnregisterObject unregisterObject = nullptr;
    PfnDxLockObjects lock = nullptr;
    PfnDxUnlockObjects unlock = nullptr;
    bool loaded = false;
    bool tried = false;

    // Precisa do contexto GL corrente
    void load()
    {
        if (tried && loaded)
            return;
        tried = true;
        loaded = false;
        const auto getExt = reinterpret_cast<PfnGetExtensionsString>(wglGetProcAddress("wglGetExtensionsStringARB"));
        const HDC dc = wglGetCurrentDC();
        if (!getExt || !dc)
            return;
        const char* ext = getExt(dc);
        if (!ext || !std::strstr(ext, "WGL_NV_DX_interop"))
            return;
        open = reinterpret_cast<PfnDxOpenDevice>(wglGetProcAddress("wglDXOpenDeviceNV"));
        close = reinterpret_cast<PfnDxCloseDevice>(wglGetProcAddress("wglDXCloseDeviceNV"));
        registerObject = reinterpret_cast<PfnDxRegisterObject>(wglGetProcAddress("wglDXRegisterObjectNV"));
        unregisterObject = reinterpret_cast<PfnDxUnregisterObject>(wglGetProcAddress("wglDXUnregisterObjectNV"));
        lock = reinterpret_cast<PfnDxLockObjects>(wglGetProcAddress("wglDXLockObjectsNV"));
        unlock = reinterpret_cast<PfnDxUnlockObjects>(wglGetProcAddress("wglDXUnlockObjectsNV"));
        loaded = open && close && registerObject && unregisterObject && lock && unlock;
    }
};

WglDx g_wgl;

std::string toUtf8(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Fabricante (PCI vendor id) deduzido do GL_VENDOR / GL_RENDERER
UINT vendorFromGl(const std::string& text)
{
    auto has = [&](const char* needle) {
        std::string t = text, n = needle;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return t.find(n) != std::string::npos;
    };
    if (has("nvidia"))
        return 0x10DE;
    if (has("intel"))
        return 0x8086;
    if (has("amd") || has("ati ") || has("radeon"))
        return 0x1002;
    return 0;
}

bool sameVendor(UINT adapterVendor, UINT wanted)
{
    if (wanted == 0x1002)
        return adapterVendor == 0x1002 || adapterVendor == 0x1022;
    return adapterVendor == wanted;
}

} // namespace

// ----------------------------------------------------------------------------- InteropTexture

InteropTexture::~InteropTexture()
{
    if (shareHandle_ && owner_ && owner_->interopDevice_) {
        HANDLE h = shareHandle_;
        if (locked_)
            g_wgl.unlock(owner_->interopDevice_, 1, &h);
        g_wgl.unregisterObject(owner_->interopDevice_, h);
    }
    if (glTex_)
        glDeleteTextures(1, &glTex_);
}

void InteropTexture::beginD3D()
{
    if (shareHandle_ && locked_) {
        HANDLE h = shareHandle_;
        g_wgl.unlock(owner_->interopDevice_, 1, &h);
        locked_ = false;
    }
}

void InteropTexture::endD3D()
{
    ID3D11DeviceContext* ctx = owner_->context();
    if (shareHandle_) {
        ctx->Flush(); // os comandos D3D precisam ter sido enviados antes de o GL pegar a textura
        HANDLE h = shareHandle_;
        locked_ = g_wgl.lock(owner_->interopDevice_, 1, &h) != FALSE;
        if (!locked_)
            log::warn("D3D11Interop: wglDXLockObjectsNV falhou");
        return;
    }

    // Fallback: textura -> staging -> memoria do sistema -> glTexSubImage2D
    ctx->CopyResource(staging_.Get(), tex_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return;
    GLint prevTex = 0, prevAlign = 0, prevRow = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow);
    glBindTexture(GL_TEXTURE_2D, glTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(mapped.RowPitch / 4));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_BGRA, GL_UNSIGNED_BYTE, mapped.pData);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    ctx->Unmap(staging_.Get(), 0);
}

void InteropTexture::clear(float r, float g, float b, float a)
{
    beginD3D();
    const float color[4] = {r, g, b, a};
    owner_->context()->ClearRenderTargetView(rtv_.Get(), color);
    endD3D();
}

// ----------------------------------------------------------------------------- D3D11Interop

D3D11Interop::~D3D11Interop()
{
    closeInterop();
}

void D3D11Interop::closeInterop()
{
    if (interopDevice_) {
        g_wgl.close(interopDevice_);
        interopDevice_ = nullptr;
    }
}

// Abre o device no interop e confirma com um registro de verdade (a extensao existir nao basta)
bool D3D11Interop::openInterop()
{
    g_wgl.load();
    if (!g_wgl.loaded)
        return false;
    interopDevice_ = g_wgl.open(device_.Get());
    if (!interopDevice_)
        return false;

    bool ok = false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 16;
    desc.Height = 16;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> tex;
    if (SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &tex))) {
        GLuint gl = 0;
        glGenTextures(1, &gl);
        glBindTexture(GL_TEXTURE_2D, gl);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        HANDLE h = g_wgl.registerObject(interopDevice_, tex.Get(), gl, GL_TEXTURE_2D, kWglAccessReadOnly);
        if (h) {
            ok = g_wgl.lock(interopDevice_, 1, &h) != FALSE;
            if (ok)
                g_wgl.unlock(interopDevice_, 1, &h);
            g_wgl.unregisterObject(interopDevice_, h);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &gl);
    }
    if (!ok)
        closeInterop();
    return ok;
}

bool D3D11Interop::initOnAdapter(IDXGIAdapter1* adapter, bool testInterop)
{
    closeInterop();
    device_.Reset();
    context_.Reset();
    adapter_.Reset();

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, static_cast<UINT>(std::size(levels)),
                                   D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (hr == E_INVALIDARG) // sem 11_1 no runtime
        hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, static_cast<UINT>(std::size(levels)) - 1,
                               D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr))
        return false;

    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(dev.As(&mt)))
        mt->SetMultithreadProtected(TRUE); // a captura (WinRT) pode tocar no device de outras threads

    device_ = dev;
    context_ = ctx;
    ComPtr<IDXGIDevice> dxgiDev;
    if (SUCCEEDED(dev.As(&dxgiDev))) {
        ComPtr<IDXGIAdapter> a;
        if (SUCCEEDED(dxgiDev->GetAdapter(&a)))
            a.As(&adapter_);
    }
    adapterName_.clear();
    if (adapter_) {
        DXGI_ADAPTER_DESC1 d{};
        adapter_->GetDesc1(&d);
        adapterName_ = toUtf8(d.Description);
    }
    if (testInterop)
        openInterop();
    return true;
}

std::shared_ptr<D3D11Interop> D3D11Interop::create(std::string* error)
{
    auto fail = [&](const std::string& msg) -> std::shared_ptr<D3D11Interop> {
        if (error)
            *error = msg;
        return nullptr;
    };

    std::shared_ptr<D3D11Interop> self(new D3D11Interop());
    const auto* vendorStr = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const auto* rendererStr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    self->glRenderer_ = std::string(rendererStr ? rendererStr : "?");
    const UINT wanted = vendorFromGl(std::string(vendorStr ? vendorStr : "") + " " + self->glRenderer_);

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return fail("CreateDXGIFactory1 falhou");

    struct Candidate
    {
        ComPtr<IDXGIAdapter1> adapter;
        std::string name;
        bool match = false;
    };
    std::vector<Candidate> candidates;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        Candidate c;
        c.adapter = a;
        c.name = toUtf8(d.Description);
        c.match = wanted != 0 && sameVendor(d.VendorId, wanted);
        candidates.push_back(std::move(c));
    }
    // Adaptadores do mesmo fabricante do GL primeiro; os outros so entram como ultimo recurso
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.match && !b.match; });

    g_wgl.load();
    const bool anyMatch = !candidates.empty() && candidates.front().match;
    for (const Candidate& c : candidates) {
        if (anyMatch && !c.match)
            break; // nao tenta adaptadores de outro fabricante quando ha um do mesmo do GL
        if (self->initOnAdapter(c.adapter.Get(), true) && self->interopAvailable())
            return self;
    }
    // Sem interop em nenhum candidato: device no adaptador do fabricante do GL (ou no primeiro) e copia por CPU
    if (!candidates.empty() && self->initOnAdapter(candidates.front().adapter.Get(), false))
        return self;
    if (self->initOnAdapter(nullptr, false))
        return self;
    return fail("não foi possível criar um device D3D11");
}

std::shared_ptr<D3D11Interop> D3D11Interop::shared(std::string* error)
{
    static std::weak_ptr<D3D11Interop> weak;
    if (auto p = weak.lock())
        return p;
    auto p = create(error);
    weak = p;
    return p;
}

std::string D3D11Interop::describe() const
{
    return "adaptador D3D11: " + adapterName_ + " | GL: " + glRenderer_ + " | caminho: " +
           (interopAvailable() ? "WGL_NV_DX_interop2 (GPU)" : "cópia por CPU (staging)");
}

std::unique_ptr<InteropTexture> D3D11Interop::createTexture(int width, int height, bool forceCpu, DXGI_FORMAT format,
                                                            bool keepAlpha)
{
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return nullptr;

    std::unique_ptr<InteropTexture> t(new InteropTexture());
    t->owner_ = this;
    t->width_ = width;
    t->height_ = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &t->tex_)))
        return nullptr;
    if (FAILED(device_->CreateRenderTargetView(t->tex_.Get(), nullptr, &t->rtv_)))
        return nullptr;

    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGenTextures(1, &t->glTex_);
    glBindTexture(GL_TEXTURE_2D, t->glTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // A captura de tela e opaca: alpha sempre 1 (o canal alpha do BGRA nao e confiavel)
    if (!keepAlpha)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);

    if (!forceCpu && interopDevice_) {
        HANDLE h = g_wgl.registerObject(interopDevice_, t->tex_.Get(), t->glTex_, GL_TEXTURE_2D, kWglAccessReadOnly);
        if (h)
            t->shareHandle_ = h;
        else
            log::warn("D3D11Interop: wglDXRegisterObjectNV falhou; usando copia por CPU");
    }
    if (!t->shareHandle_) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&sd, nullptr, &t->staging_))) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
            return nullptr;
        }
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    return t;
}

} // namespace bsl::render
