#include "ai/AiDml.h"

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <dml_provider_factory.h>

namespace bsl::ai {

using Microsoft::WRL::ComPtr;

namespace {

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}

struct DmlDevice::Impl
{
    HMODULE dmlDll = nullptr;
    const OrtDmlApi* dmlApi = nullptr;
    ComPtr<ID3D12Device> d3d;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDMLDevice> dml;
};

DmlDevice::DmlDevice() : impl_(std::make_unique<Impl>()) {}

DmlDevice::~DmlDevice() = default;

std::unique_ptr<DmlDevice> DmlDevice::load(const std::filesystem::path& dir)
{
    std::unique_ptr<DmlDevice> d(new DmlDevice());
    std::error_code ec;
    const std::filesystem::path dmlPath = dir / L"DirectML.dll";
    if (std::filesystem::exists(dmlPath, ec))
        d->impl_->dmlDll = LoadLibraryExW(dmlPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!d->impl_->dmlDll)
        d->impl_->dmlDll = LoadLibraryExW(L"DirectML.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return d;
}

bool DmlDevice::init(std::string& gpuName, std::string& error)
{
    Impl& g = *impl_;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        error = "DXGI indisponível";
        return false;
    }
    ComPtr<IDXGIAdapter1> chosen;
    DXGI_ADAPTER_DESC1 chosenDesc{};
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 desc{};
        a->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        ComPtr<ID3D12Device> dev;
        if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
            chosen = a;
            chosenDesc = desc;
            g.d3d = dev;
            break;
        }
    }
    if (!g.d3d) {
        error = "nenhuma GPU com Direct3D 12";
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(g.d3d->CreateCommandQueue(&qd, IID_PPV_ARGS(&g.queue)))) {
        error = "falha ao criar a fila de comandos D3D12";
        return false;
    }
    using CreateFn = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);
    const auto create = reinterpret_cast<CreateFn>(g.dmlDll ? GetProcAddress(g.dmlDll, "DMLCreateDevice") : nullptr);
    if (!create) {
        error = "DirectML.dll não encontrada";
        return false;
    }
    if (FAILED(create(g.d3d.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&g.dml)))) {
        error = "falha ao criar o dispositivo DirectML";
        return false;
    }
    const void* dmlApi = nullptr;
    try {
        Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, &dmlApi));
    } catch (...) {
    }
    if (!dmlApi) {
        error = "o onnxruntime.dll não tem o provedor DirectML";
        return false;
    }
    g.dmlApi = static_cast<const OrtDmlApi*>(dmlApi);
    gpuName = narrow(chosenDesc.Description);
    return true;
}

void DmlDevice::appendProvider(Ort::SessionOptions& options) const
{
    Ort::ThrowOnError(impl_->dmlApi->SessionOptionsAppendExecutionProvider_DML1(options, impl_->dml.Get(), impl_->queue.Get()));
}

std::vector<AdapterInfo> listAdapters()
{
    std::vector<AdapterInfo> out;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
        return out;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 desc{};
        a->GetDesc1(&desc);
        AdapterInfo info;
        info.name = narrow(desc.Description);
        info.vramBytes = desc.DedicatedVideoMemory;
        info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        info.usable = !info.software && SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
        out.push_back(std::move(info));
    }
    return out;
}

}
