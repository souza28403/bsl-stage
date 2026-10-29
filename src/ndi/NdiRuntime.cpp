#include "ndi/NdiRuntime.h"

#include "app/Log.h"

#include <windows.h>

#include <cstdlib>
#include <system_error>

namespace bsl::ndi {

namespace fs = std::filesystem;

namespace {

fs::path g_override;
std::mutex g_instanceMutex;

fs::path exeDir()
{
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf))
        return {};
    return fs::path(buf).parent_path();
}

fs::path envDir(const wchar_t* name)
{
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf))
        return {};
    return fs::path(buf);
}

bool fileExists(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

} // namespace

std::vector<fs::path> Runtime::candidates(const fs::path& explicitDll)
{
    std::vector<fs::path> out;
    if (!explicitDll.empty()) { // caminho explícito é exclusivo
        out.push_back(explicitDll);
        return out;
    }
    if (const fs::path dir = exeDir(); !dir.empty())
        out.push_back(dir / kRuntimeDllName);
    for (const wchar_t* var : {L"NDI_RUNTIME_DIR_V6", L"NDI_RUNTIME_DIR_V5"})
        if (const fs::path dir = envDir(var); !dir.empty())
            out.push_back(dir / kRuntimeDllName);
    return out;
}

Runtime::~Runtime()
{
    if (initialized_ && api_ && api_->destroy)
        api_->destroy();
    // A DLL fica carregada de propósito: o NDI mantém threads internas e descarregar no encerramento não ganha nada.
}

void Runtime::setLibraryOverride(const fs::path& dll)
{
    std::lock_guard lk(g_instanceMutex);
    g_override = dll;
}

Runtime& Runtime::instance()
{
    // Alocado e nunca destruído: o NDI_destroy no encerramento do processo disputaria com as threads do próprio NDI
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
        // ALTERED_SEARCH_PATH: as dependências da DLL são procuradas na pasta dela
        HMODULE mod = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!mod) {
            error_ = std::string(kMissingText) + " (não foi possível carregar " + candidate.string() + ", erro "
                     + std::to_string(GetLastError()) + ")";
            continue;
        }
        auto loader = reinterpret_cast<NDIlib_load_fn>(GetProcAddress(mod, "NDIlib_v6_load"));
        if (!loader)
            loader = reinterpret_cast<NDIlib_load_fn>(GetProcAddress(mod, "NDIlib_v5_load"));
        if (!loader) {
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

} // namespace bsl::ndi
