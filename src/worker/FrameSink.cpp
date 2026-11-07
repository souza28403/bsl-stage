#include "worker/FrameSink.h"

#include "media/WorkerProtocol.h"
#include "worker/WorkerUtil.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace bsl::worker {

using Microsoft::WRL::ComPtr;

namespace {

std::uint64_t duplicateToApp(HANDLE appProcess, HANDLE local)
{
    HANDLE remote = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), local, appProcess, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return 0;
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(remote));
}

void unpremultiplyRow(const std::uint8_t* src, std::uint8_t* dst, int pixels)
{
    for (int x = 0; x < pixels; ++x) {
        const std::uint8_t* s = src + x * 4;
        std::uint8_t* d = dst + x * 4;
        const unsigned a = s[3];
        if (a == 0 || a == 255) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        } else {
            d[0] = static_cast<std::uint8_t>(std::min(255u, (s[0] * 255u + a / 2) / a));
            d[1] = static_cast<std::uint8_t>(std::min(255u, (s[1] * 255u + a / 2) / a));
            d[2] = static_cast<std::uint8_t>(std::min(255u, (s[2] * 255u + a / 2) / a));
        }
        d[3] = static_cast<std::uint8_t>(a);
    }
}

class ShmSink final : public FrameSink
{
public:
    ShmSink(PipeChannel* ch, DWORD appPid) : ch_(ch)
    {
        appProcess_ = OpenProcess(PROCESS_DUP_HANDLE, FALSE, appPid);
    }
    ~ShmSink() override
    {
        release();
        if (appProcess_)
            CloseHandle(appProcess_);
    }

    const char* kind() const override { return "shm"; }
    bool ok() const { return appProcess_ != nullptr; }

    bool resize(int w, int h) override
    {
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
            return false;
        release();
        const std::size_t total = wp::shmTotalBytes(w, h);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      static_cast<DWORD>(static_cast<unsigned long long>(total) >> 32),
                                      static_cast<DWORD>(total & 0xFFFFFFFFu), nullptr);
        if (!mapping_)
            return false;
        view_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!view_) {
            release();
            return false;
        }
        std::memset(view_, 0, sizeof(wp::ShmHeader));
        auto* hdr = static_cast<wp::ShmHeader*>(view_);
        hdr->magic = wp::kShmMagic;
        hdr->version = wp::kProtocolVersion;
        hdr->width = static_cast<std::uint32_t>(w);
        hdr->height = static_cast<std::uint32_t>(h);
        hdr->latest = -1;
        width_ = w;
        height_ = h;
        const std::uint64_t remote = duplicateToApp(appProcess_, mapping_);
        if (!remote) {
            release();
            return false;
        }
        ch_->send(Json{{"t", "shm"}, {"w", w}, {"h", h}, {"handle", remote}});
        return true;
    }

    bool submitBgra(const std::uint8_t* bgra, int stride, bool premultiplied, int page) override
    {
        if (!view_)
            return false;
        auto* hdr = static_cast<wp::ShmHeader*>(view_);
        const LONG latest = hdr->latest;
        const int idx = latest == 0 ? 1 : 0;
        InterlockedIncrement64(&hdr->seq[idx]);
        std::uint8_t* dst = wp::shmBuffer(view_, width_, height_, idx);
        for (int y = 0; y < height_; ++y) {
            const std::uint8_t* s = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
            std::uint8_t* d = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4u;
            if (premultiplied) {
                unpremultiplyRow(s, d, width_);
            } else {
                std::memcpy(d, s, static_cast<std::size_t>(width_) * 4u);
            }
            for (int x = 0; x < width_; ++x)
                std::swap(d[x * 4], d[x * 4 + 2]);
        }
        const LONG64 seq = InterlockedIncrement64(&hdr->seq[idx]);
        InterlockedExchange(&hdr->latest, idx);
        ch_->send(Json{{"t", "frame"}, {"i", idx}, {"seq", static_cast<long long>(seq)}, {"page", page}});
        return true;
    }

private:
    void release()
    {
        if (view_) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (mapping_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
    }

    PipeChannel* ch_;
    HANDLE appProcess_ = nullptr;
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
};

const char* kBlitShader = R"(
Texture2D src : register(t0);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
float4 ps(VSOut i) : SV_Target
{
    float4 c = src.Sample(smp, i.uv);
    if (c.a > 0.0001) c.rgb = saturate(c.rgb / c.a);
    return c;
}
)";

class TextureSink final : public FrameSink
{
public:
    TextureSink(PipeChannel* ch, DWORD appPid) : ch_(ch)
    {
        appProcess_ = OpenProcess(PROCESS_DUP_HANDLE, FALSE, appPid);
    }
    ~TextureSink() override
    {
        for (Slot& s : slots_)
            s = Slot{};
        if (appProcess_)
            CloseHandle(appProcess_);
    }

    const char* kind() const override { return "texture"; }
    bool supportsGpuHandle() const override { return true; }

    bool init(bool haveLuid, LONG luidHigh, DWORD luidLow, std::string* why)
    {
        if (!appProcess_) {
            *why = "não foi possível abrir o processo do app";
            return false;
        }
        if (!haveLuid) {
            *why = "o app não informou o adaptador gráfico";
            return false;
        }
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            *why = "CreateDXGIFactory1 falhou";
            return false;
        }
        LUID luid{luidLow, luidHigh};
        ComPtr<IDXGIAdapter1> adapter;
        wchar_t forced[16];
        if (GetEnvironmentVariableW(L"BSLSTAGE_WORKER_ADAPTER_INDEX", forced, 16) > 0) {
            ComPtr<IDXGIFactory1> f1;
            factory.As(&f1);
            if (!f1 || FAILED(f1->EnumAdapters1(static_cast<UINT>(_wtoi(forced)), &adapter))) {
                *why = "adaptador forçado inexistente";
                return false;
            }
        } else if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) {
            *why = "adaptador do app não encontrado";
            return false;
        }
        DXGI_ADAPTER_DESC1 ad{};
        adapter->GetDesc1(&ad);
        adapterName_ = toUtf8(ad.Description) + " luid=" + std::to_string(ad.AdapterLuid.HighPart) + "," + std::to_string(ad.AdapterLuid.LowPart);

        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       levels, 2, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (hr == E_INVALIDARG)
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   levels + 1, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(hr)) {
            *why = "D3D11CreateDevice falhou";
            return false;
        }
        if (FAILED(dev.As(&dev1_))) {
            *why = "o driver não suporta D3D11.1 (OpenSharedResource1)";
            return false;
        }
        dev_ = dev;
        ctx_ = ctx;
        return true;
    }

    const std::string& adapterName() const { return adapterName_; }

    bool resize(int w, int h) override
    {
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
            return false;
        for (Slot& s : slots_)
            s = Slot{};
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(w);
        d.Height = static_cast<UINT>(h);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        std::uint64_t remote[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
            Slot& s = slots_[i];
            if (FAILED(dev_->CreateTexture2D(&d, nullptr, &s.tex)))
                return false;
            if (FAILED(dev_->CreateRenderTargetView(s.tex.Get(), nullptr, &s.rtv)))
                return false;
            if (FAILED(s.tex.As(&s.mutex)))
                return false;
            ComPtr<IDXGIResource1> res;
            if (FAILED(s.tex.As(&res)))
                return false;
            HANDLE shared = nullptr;
            if (FAILED(res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared)))
                return false;
            remote[i] = duplicateToApp(appProcess_, shared);
            CloseHandle(shared);
            if (!remote[i])
                return false;
        }
        width_ = w;
        height_ = h;
        next_ = 0;
        ch_->send(Json{{"t", "tex"}, {"w", w}, {"h", h}, {"handles", Json::array({remote[0], remote[1]})}});
        return true;
    }

    bool submitBgra(const std::uint8_t* bgra, int stride, bool premultiplied, int page) override
    {
        Slot* s = acquire();
        if (!s)
            return false;
        const std::uint8_t* data = bgra;
        int rowPitch = stride;
        if (premultiplied) {
            scratch_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
            for (int y = 0; y < height_; ++y)
                unpremultiplyRow(bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                                 scratch_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4u,
                                 width_);
            data = scratch_.data();
            rowPitch = width_ * 4;
        }
        ctx_->UpdateSubresource(s->tex.Get(), 0, nullptr, data, static_cast<UINT>(rowPitch), 0);
        publish(*s, page);
        return true;
    }

    bool submitSharedHandle(HANDLE sharedHandle, int page, std::string* error) override
    {
        if (error)
            error->clear();
        ComPtr<ID3D11Texture2D> src;
        HRESULT hr = dev1_->OpenSharedResource1(sharedHandle, IID_PPV_ARGS(&src));
        if (FAILED(hr)) {
            if (error)
                *error = "OpenSharedResource1 falhou (0x" + hex(static_cast<unsigned long>(hr)) + ")";
            return false;
        }
        D3D11_TEXTURE2D_DESC sd{};
        src->GetDesc(&sd);
        if (static_cast<int>(sd.Width) != width_ || static_cast<int>(sd.Height) != height_) {
            if (!resize(static_cast<int>(sd.Width), static_cast<int>(sd.Height))) {
                if (error)
                    *error = "não foi possível redimensionar a textura de saída";
                return false;
            }
        }
        if (!ensureBlit(error))
            return false;

        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(dev_->CreateShaderResourceView(src.Get(), nullptr, &srv))) {
            D3D11_TEXTURE2D_DESC id = sd;
            id.Usage = D3D11_USAGE_DEFAULT;
            id.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            id.MiscFlags = 0;
            id.CPUAccessFlags = 0;
            ComPtr<ID3D11Texture2D> mid;
            if (FAILED(dev_->CreateTexture2D(&id, nullptr, &mid)) || FAILED(dev_->CreateShaderResourceView(mid.Get(), nullptr, &srv))) {
                if (error)
                    *error = "não foi possível ler a textura do navegador";
                return false;
            }
            ctx_->CopyResource(mid.Get(), src.Get());
        }

        Slot* s = acquire();
        if (!s)
            return false;
        ID3D11RenderTargetView* rtv = s->rtv.Get();
        ctx_->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
        ctx_->RSSetViewports(1, &vp);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(nullptr);
        ctx_->VSSetShader(vs_.Get(), nullptr, 0);
        ctx_->PSSetShader(ps_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* views[1] = {srv.Get()};
        ctx_->PSSetShaderResources(0, 1, views);
        ID3D11SamplerState* samplers[1] = {sampler_.Get()};
        ctx_->PSSetSamplers(0, 1, samplers);
        ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        ctx_->Draw(3, 0);
        ID3D11ShaderResourceView* none[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none);
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        publish(*s, page);
        return true;
    }

private:
    struct Slot
    {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<IDXGIKeyedMutex> mutex;
    };

    static std::string hex(unsigned long v)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08lX", v);
        return buf;
    }

    Slot* acquire()
    {
        for (int k = 0; k < 2; ++k) {
            Slot& s = slots_[(next_ + k) & 1];
            if (s.mutex && s.mutex->AcquireSync(0, 0) == S_OK) {
                held_ = (next_ + k) & 1;
                return &s;
            }
        }
        return nullptr;
    }

    void publish(Slot& s, int page)
    {
        s.mutex->ReleaseSync(1);
        ch_->send(Json{{"t", "frame"}, {"i", held_}, {"seq", ++seq_}, {"page", page}});
        next_ = (held_ + 1) & 1;
    }

    bool ensureBlit(std::string* error)
    {
        if (vs_)
            return true;
        ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
        if (FAILED(D3DCompile(kBlitShader, std::strlen(kBlitShader), nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0,
                              &vsBlob, &errBlob))
            || FAILED(D3DCompile(kBlitShader, std::strlen(kBlitShader), nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0,
                                 &psBlob, &errBlob))) {
            if (error)
                *error = "não foi possível compilar o shader de cópia";
            return false;
        }
        if (FAILED(dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_))
            || FAILED(dev_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_))) {
            if (error)
                *error = "não foi possível criar o shader de cópia";
            return false;
        }
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        dev_->CreateSamplerState(&sd, &sampler_);
        return true;
    }

    PipeChannel* ch_;
    HANDLE appProcess_ = nullptr;
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11Device1> dev1_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11SamplerState> sampler_;
    Slot slots_[2];
    int next_ = 0;
    int held_ = 0;
    long long seq_ = 0;
    std::string adapterName_;
    std::vector<std::uint8_t> scratch_;
};

}

std::unique_ptr<FrameSink> createShmSink(const SinkRequest& request)
{
    auto sink = std::make_unique<ShmSink>(request.channel, request.appPid);
    if (!sink->ok())
        return nullptr;
    return sink;
}

std::unique_ptr<FrameSink> createFrameSink(const SinkRequest& request)
{
    std::string why;
    if (request.transport != "shm") {
        auto sink = std::make_unique<TextureSink>(request.channel, request.appPid);
        if (sink->init(request.haveLuid, request.luidHigh, request.luidLow, &why)) {
            request.channel->send(Json{{"t", "transport"}, {"kind", "texture"}, {"why", "adaptador: " + sink->adapterName()}});
            return sink;
        }
        if (request.transport == "texture") {
            request.channel->send(Json{{"t", "error"}, {"msg", "textura compartilhada indisponível: " + why}, {"fatal", true}});
            return nullptr;
        }
    } else {
        why = "memória compartilhada pedida";
    }
    auto shm = createShmSink(request);
    if (shm)
        request.channel->send(Json{{"t", "transport"}, {"kind", "shm"}, {"why", why}});
    return shm;
}

}
