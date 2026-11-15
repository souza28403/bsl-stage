#include "platform/DynLib.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bslplat {

bool DynLib::open(const std::filesystem::path& path, bool alteredSearch)
{
    close();
#ifdef _WIN32
    m_handle = reinterpret_cast<void*>(LoadLibraryExW(path.c_str(), nullptr, alteredSearch ? LOAD_WITH_ALTERED_SEARCH_PATH : 0));
#else
    (void)alteredSearch;
    m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    return m_handle != nullptr;
}

void DynLib::close()
{
    if (!m_handle)
        return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(m_handle));
#else
    dlclose(m_handle);
#endif
    m_handle = nullptr;
}

void* DynLib::symbol(const char* name) const
{
    if (!m_handle)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(m_handle), name));
#else
    return dlsym(m_handle, name);
#endif
}

}
