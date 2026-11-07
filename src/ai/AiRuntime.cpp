#include "ai/AiRuntime.h"

#include "app/Log.h"

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace bsl::ai {

using Microsoft::WRL::ComPtr;

namespace {

const ModelSpec kSpecs[kModelKindCount] = {
    {ModelKind::Depth, "depth", "Profundidade (Depth Anything V2 Small)", "depth_anything_v2_small_fp16.onnx",
     "Apache-2.0", 378, 210},
    {ModelKind::Person, "person", "Pessoa (MODNet)", "modnet_fp32.onnx", "Apache-2.0", 384, 224},
};

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

std::filesystem::path exeDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

std::uint16_t floatToHalf(float f)
{
    std::uint32_t x;
    std::memcpy(&x, &f, 4);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const std::int32_t exp = static_cast<std::int32_t>((x >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mant = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFF) == 0xFF)
        return static_cast<std::uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0u));
    if (exp >= 31)
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (exp <= 0) {
        if (exp < -10)
            return static_cast<std::uint16_t>(sign);
        mant |= 0x800000u;
        const int shift = 14 - exp;
        return static_cast<std::uint16_t>(sign | ((mant + (1u << (shift - 1))) >> shift));
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exp) << 10) | ((mant + 0x1000u) >> 13));
}

float halfToFloat(std::uint16_t h)
{
    const std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000u) << 16;
    std::uint32_t exp = (h >> 10) & 0x1F;
    std::uint32_t mant = h & 0x3FFu;
    std::uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400u)) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

struct Global
{
    std::mutex mutex;
    bool tried = false;
    RuntimeInfo info;
    HMODULE ortDll = nullptr;
    HMODULE dmlDll = nullptr;
    std::unique_ptr<Ort::Env> env;
    const OrtDmlApi* dmlApi = nullptr;
    ComPtr<ID3D12Device> d3d;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDMLDevice> dml;
};

Global& global()
{
    static Global* g = new Global();
    return *g;
}

void initGpu(Global& g)
{
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        g.info.gpuError = "DXGI indisponível";
        return;
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
        g.info.gpuError = "nenhuma GPU com Direct3D 12";
        return;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(g.d3d->CreateCommandQueue(&qd, IID_PPV_ARGS(&g.queue)))) {
        g.info.gpuError = "falha ao criar a fila de comandos D3D12";
        return;
    }
    using CreateFn = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);
    const auto create = reinterpret_cast<CreateFn>(g.dmlDll ? GetProcAddress(g.dmlDll, "DMLCreateDevice") : nullptr);
    if (!create) {
        g.info.gpuError = "DirectML.dll não encontrada";
        return;
    }
    if (FAILED(create(g.d3d.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&g.dml)))) {
        g.info.gpuError = "falha ao criar o dispositivo DirectML";
        return;
    }
    const void* dmlApi = nullptr;
    try {
        Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, &dmlApi));
    } catch (...) {
    }
    if (!dmlApi) {
        g.info.gpuError = "o onnxruntime.dll não tem o provedor DirectML";
        return;
    }
    g.dmlApi = static_cast<const OrtDmlApi*>(dmlApi);
    g.info.gpuName = narrow(chosenDesc.Description);
    g.info.gpuAvailable = true;
}

Global& ensureGlobal()
{
    Global& g = global();
    std::lock_guard lock(g.mutex);
    if (g.tried)
        return g;
    g.tried = true;

    const std::filesystem::path dir = exeDir();
    const std::filesystem::path ortPath = dir / L"onnxruntime.dll";
    std::error_code ec;
    g.info.dllFound = std::filesystem::exists(ortPath, ec);
    if (!g.info.dllFound) {
        g.info.error = "onnxruntime.dll não encontrada ao lado do programa";
        return g;
    }
    const std::filesystem::path dmlPath = dir / L"DirectML.dll";
    if (std::filesystem::exists(dmlPath, ec))
        g.dmlDll = LoadLibraryExW(dmlPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g.dmlDll)
        g.dmlDll = LoadLibraryExW(L"DirectML.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    g.ortDll = LoadLibraryExW(ortPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g.ortDll) {
        g.info.error = "falha ao carregar onnxruntime.dll (erro " + std::to_string(GetLastError()) + ")";
        return g;
    }
    using GetBaseFn = const OrtApiBase*(ORT_API_CALL*)();
    const auto getBase = reinterpret_cast<GetBaseFn>(GetProcAddress(g.ortDll, "OrtGetApiBase"));
    const OrtApiBase* base = getBase ? getBase() : nullptr;
    const OrtApi* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        g.info.error = "o onnxruntime.dll não é compatível (versão da API)";
        return g;
    }
    try {
        Ort::InitApi(api);
        g.env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "bslstage");
        try {
            g.env->DisableTelemetryEvents();
        } catch (const std::exception& e) {
            log::warn(std::string("IA: não foi possível desligar a telemetria do ONNX Runtime: ") + e.what());
        }
        g.info.version = base->GetVersionString();
        g.info.loaded = true;
    } catch (const std::exception& e) {
        g.info.error = std::string("falha ao iniciar o ONNX Runtime: ") + e.what();
        return g;
    }
    try {
        initGpu(g);
    } catch (const std::exception& e) {
        g.info.gpuAvailable = false;
        g.info.gpuError = std::string("DirectML: ") + e.what();
    }
    if (g.info.gpuAvailable)
        log::info("IA: ONNX Runtime " + g.info.version + ", GPU " + g.info.gpuName + " (DirectML)");
    else
        log::info("IA: ONNX Runtime " + g.info.version + ", sem GPU DirectML (" + g.info.gpuError + "): usa a CPU");
    return g;
}

}

struct Session::Impl
{
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    ONNXTensorElementDataType inputType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    std::vector<float> chw;
    std::vector<std::uint16_t> chwHalf;
};

const ModelSpec& modelSpec(ModelKind kind)
{
    return kSpecs[static_cast<int>(kind)];
}

namespace {
std::mutex g_modelsOverrideMutex;
std::filesystem::path g_modelsOverride;
bool g_modelsOverrideSet = false;
}

void setModelsDirOverride(const std::filesystem::path& dir, bool enable)
{
    std::lock_guard lock(g_modelsOverrideMutex);
    g_modelsOverride = dir;
    g_modelsOverrideSet = enable;
}

std::filesystem::path modelsDir()
{
    {
        std::lock_guard lock(g_modelsOverrideMutex);
        if (g_modelsOverrideSet)
            return g_modelsOverride;
    }
    static const std::filesystem::path cached = [] {
        std::error_code ec;
        std::filesystem::path dir = exeDir();
        for (int i = 0; i < 5 && !dir.empty(); ++i) {
            if (std::filesystem::is_directory(dir / L"assets" / L"models", ec))
                return dir / L"assets" / L"models";
            dir = dir.parent_path();
        }
        return std::filesystem::path();
    }();
    return cached;
}

std::filesystem::path modelPath(ModelKind kind)
{
    const std::filesystem::path dir = modelsDir();
    return dir.empty() ? std::filesystem::path() : dir / modelSpec(kind).fileName;
}

bool modelPresent(ModelKind kind)
{
    const std::filesystem::path p = modelPath(kind);
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
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

RuntimeInfo runtimeInfo()
{
    return ensureGlobal().info;
}

RuntimeSnapshot peekRuntime()
{
    Global& g = global();
    std::unique_lock lock(g.mutex, std::try_to_lock);
    RuntimeSnapshot snap;
    if (!lock.owns_lock()) {
        snap.tried = true;
        return snap;
    }
    snap.tried = g.tried;
    snap.info = g.info;
    return snap;
}

Session::~Session() = default;

std::unique_ptr<Session> Session::create(ModelKind kind, DevicePref pref, std::string& error)
{
    const ModelSpec& spec = modelSpec(kind);
    const std::filesystem::path path = modelPath(kind);
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
        error = std::string("modelo ausente: ") + spec.fileName;
        return nullptr;
    }
    Global& g = ensureGlobal();
    if (!g.info.loaded) {
        error = g.info.error;
        return nullptr;
    }
    if (pref == DevicePref::Gpu && !g.info.gpuAvailable) {
        error = "GPU DirectML indisponível: " + g.info.gpuError;
        return nullptr;
    }

    auto tryCreate = [&](bool gpu, std::string& err) -> std::unique_ptr<Session> {
        auto s = std::unique_ptr<Session>(new Session());
        s->impl_ = std::make_unique<Impl>();
        s->kind_ = kind;
        s->w_ = spec.inputW;
        s->h_ = spec.inputH;
        try {
            Ort::SessionOptions so;
            so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            so.AddFreeDimensionOverrideByName("batch_size", 1);
            so.AddFreeDimensionOverrideByName("height", s->h_);
            so.AddFreeDimensionOverrideByName("width", s->w_);
            if (gpu) {
                so.DisableMemPattern();
                so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
                Ort::ThrowOnError(g.dmlApi->SessionOptionsAppendExecutionProvider_DML1(so, g.dml.Get(), g.queue.Get()));
            } else {
                so.AddConfigEntry("optimization.disable_specified_optimizers", "SimplifiedLayerNormFusion");
                const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
                so.SetIntraOpNumThreads(static_cast<int>(std::clamp(hw / 2, 1u, 6u)));
            }
            s->impl_->session = std::make_unique<Ort::Session>(*g.env, path.c_str(), so);
            Ort::AllocatorWithDefaultOptions alloc;
            s->impl_->inputName = s->impl_->session->GetInputNameAllocated(0, alloc).get();
            s->impl_->outputName = s->impl_->session->GetOutputNameAllocated(0, alloc).get();
            s->impl_->inputType = s->impl_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetElementType();
        } catch (const std::exception& e) {
            err = std::string(gpu ? "DirectML: " : "CPU: ") + e.what();
            return nullptr;
        }
        s->onGpu_ = gpu;
        s->deviceName_ = gpu ? g.info.gpuName + " (DirectML)" : std::string("CPU");
        return s;
    };

    std::string gpuErr;
    if (pref != DevicePref::Cpu && g.info.gpuAvailable) {
        if (auto s = tryCreate(true, gpuErr))
            return s;
        log::warn("IA: " + std::string(spec.key) + " não carregou no DirectML (" + gpuErr + ")");
        if (pref == DevicePref::Gpu) {
            error = gpuErr;
            return nullptr;
        }
    }
    std::string cpuErr;
    if (auto s = tryCreate(false, cpuErr))
        return s;
    error = cpuErr;
    return nullptr;
}

bool Session::run(const std::uint8_t* rgb, std::vector<float>& out, std::string& error)
{
    try {
        const size_t plane = static_cast<size_t>(w_) * static_cast<size_t>(h_);
        Impl& im = *impl_;
        im.chw.resize(plane * 3);
        const bool depth = kind_ == ModelKind::Depth;
        const float mean[3] = {depth ? 0.485f : 0.5f, depth ? 0.456f : 0.5f, depth ? 0.406f : 0.5f};
        const float inv[3] = {depth ? 1.0f / (0.229f * 255.0f) : 1.0f / (0.5f * 255.0f),
                              depth ? 1.0f / (0.224f * 255.0f) : 1.0f / (0.5f * 255.0f),
                              depth ? 1.0f / (0.225f * 255.0f) : 1.0f / (0.5f * 255.0f)};
        for (int c = 0; c < 3; ++c) {
            float* dst = im.chw.data() + plane * static_cast<size_t>(c);
            const float off = mean[c] * 255.0f * inv[c];
            for (size_t i = 0; i < plane; ++i)
                dst[i] = static_cast<float>(rgb[i * 3 + static_cast<size_t>(c)]) * inv[c] - off;
        }

        const std::array<int64_t, 4> shape = {1, 3, h_, w_};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input{nullptr};
        if (im.inputType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            im.chwHalf.resize(im.chw.size());
            for (size_t i = 0; i < im.chw.size(); ++i)
                im.chwHalf[i] = floatToHalf(im.chw[i]);
            input = Ort::Value::CreateTensor(mem, im.chwHalf.data(), im.chwHalf.size() * 2, shape.data(), shape.size(),
                                             ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
        } else {
            input = Ort::Value::CreateTensor<float>(mem, im.chw.data(), im.chw.size(), shape.data(), shape.size());
        }

        const char* inNames[] = {im.inputName.c_str()};
        const char* outNames[] = {im.outputName.c_str()};
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Ort::Value> outputs = im.session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
        lastMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        const Ort::TensorTypeAndShapeInfo info = outputs[0].GetTensorTypeAndShapeInfo();
        const size_t count = info.GetElementCount();
        if (count != plane) {
            error = "saída com tamanho inesperado (" + std::to_string(count) + " em vez de " + std::to_string(plane) + ")";
            return false;
        }
        out.resize(plane);
        if (info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            const std::uint16_t* src = outputs[0].GetTensorData<std::uint16_t>();
            for (size_t i = 0; i < plane; ++i)
                out[i] = halfToFloat(src[i]);
        } else {
            std::memcpy(out.data(), outputs[0].GetTensorData<float>(), plane * sizeof(float));
        }
        size_t bad = 0;
        for (size_t i = 0; i < plane; i += 7)
            if (!std::isfinite(out[i]))
                ++bad;
        if (bad > plane / 7 / 2) {
            error = "saída inválida (NaN)";
            return false;
        }
        for (float& v : out)
            if (!std::isfinite(v))
                v = 0.0f;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}
