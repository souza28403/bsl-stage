#include "ndi/NdiRuntime.h"

#include "app/Log.h"

#include "platform/DynLib.h"
#include "platform/ExePath.h"

#include <cstdlib>
#include <system_error>

namespace bsl::ndi {

namespace fs = std::filesystem;

namespace {

fs::path g_override;
std::mutex g_instanceMutex;

fs::path envDir(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return {};
    return fs::path(v);
}

bool fileExists(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

}

std::vector<fs::path> Runtime::candidates(const fs::path& explicitDll)
{
    std::vector<fs::path> out;
    if (!explicitDll.empty()) {
        out.push_back(explicitDll);
        return out;
    }
    if (const fs::path dir = bslplat::exeDir(); !dir.empty())
        out.push_back(dir / kRuntimeDllName);
    for (const char* var : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"})
        if (const fs::path dir = envDir(var); !dir.empty())
            out.push_back(dir / kRuntimeDllName);
    return out;
}

Runtime::~Runtime()
{
    if (initialized_ && api_ && api_->destroy)
        api_->destroy();
}

void Runtime::setLibraryOverride(const fs::path& dll)
{
    std::lock_guard lk(g_instanceMutex);
    g_override = dll;
}

Runtime& Runtime::instance()
{
    static Runtime* rt = [] {
        auto* r = new Runtime();
        fs::path override_;
        {
            std::lock_guard lk(g_instanceMutex);
            override_ = g_override;
        }
        if (r->load(override_))
            log::info("NDI: runtime carregado de " + r->path_.string() + " (" + r->version_ + ")");
        else
            log::info("NDI: " + r->error_);
        return r;
    }();
    return *rt;
}

bool Runtime::load(const fs::path& explicitDll)
{
    if (api_)
        return true;
    error_.clear();

    std::vector<fs::path> tried;
    for (const fs::path& candidate : candidates(explicitDll)) {
        tried.push_back(candidate);
        if (!fileExists(candidate)) {
            error_ = std::string(kMissingText) + " (arquivo inexistente: " + candidate.string() + ")";
            continue;
        }
        auto* lib = new bslplat::DynLib();
        if (!lib->open(candidate, true)) {
            delete lib;
            error_ = std::string(kMissingText) + " (não foi possível carregar " + candidate.string() + ")";
            continue;
        }
        auto loader = lib->symbolAs<NDIlib_load_fn>("NDIlib_v6_load");
        if (!loader)
            loader = lib->symbolAs<NDIlib_load_fn>("NDIlib_v5_load");
        if (!loader) {
            delete lib;
            error_ = std::string(kMissingText) + " (" + candidate.string() + " não exporta NDIlib_v5_load/v6_load)";
            continue;
        }
        const NDIlib_v5_t* api = loader();
        if (!api || !api->initialize || !api->destroy || !api->find_create_v2 || !api->recv_create_v3
            || !api->recv_capture_v3 || !api->send_create || !api->send_send_video_async_v2) {
            error_ = std::string(kMissingText) + " (tabela de funções incompleta em " + candidate.string() + ")";
            continue;
        }
        if (api->is_supported_CPU && !api->is_supported_CPU()) {
            error_ = "NDI: esta CPU não é suportada pelo runtime";
            continue;
        }
        if (!api->initialize()) {
            error_ = "NDI: NDIlib_initialize falhou";
            continue;
        }
        api_ = api;
        initialized_ = true;
        path_ = candidate;
        const char* v = api->version ? api->version() : nullptr;
        version_ = v ? v : "";
        error_.clear();
        return true;
    }
    if (tried.empty() || error_.empty())
        error_ = kMissingText;
    return false;
}

}
