#include "ai/AiRuntime.h"

#include "platform/Unavailable.h"

#include <mutex>

namespace bsl::ai {

namespace {

const ModelSpec kSpecs[kModelKindCount] = {
    {ModelKind::Depth, "depth", "Profundidade (Depth Anything V2 Small)", "depth_anything_v2_small_fp16.onnx",
     "Apache-2.0", 378, 210},
    {ModelKind::Person, "person", "Pessoa (MODNet)", "modnet_fp32.onnx", "Apache-2.0", 384, 224},
};

std::mutex g_overrideMutex;
std::filesystem::path g_override;
bool g_overrideSet = false;

}

const ModelSpec& modelSpec(ModelKind kind)
{
    return kSpecs[static_cast<int>(kind)];
}

void setModelsDirOverride(const std::filesystem::path& dir, bool enable)
{
    std::lock_guard lock(g_overrideMutex);
    g_override = dir;
    g_overrideSet = enable;
}

std::filesystem::path modelsDir()
{
    std::lock_guard lock(g_overrideMutex);
    return g_overrideSet ? g_override : std::filesystem::path();
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
    return {};
}

RuntimeInfo runtimeInfo()
{
    RuntimeInfo info;
    info.error = bslplat::unavailableText("IA");
    info.gpuError = info.error;
    return info;
}

RuntimeSnapshot peekRuntime()
{
    RuntimeSnapshot snap;
    snap.tried = true;
    snap.info = runtimeInfo();
    return snap;
}

struct Session::Impl
{
};

Session::~Session() = default;

std::unique_ptr<Session> Session::create(ModelKind, DevicePref, std::string& error)
{
    error = bslplat::unavailableText("IA");
    return nullptr;
}

bool Session::run(const std::uint8_t*, std::vector<float>&, std::string& error)
{
    error = bslplat::unavailableText("IA");
    return false;
}

}
