#pragma once

#include <filesystem>

namespace bslplat {

class DynLib {
public:
    DynLib() = default;
    ~DynLib() { close(); }
    DynLib(const DynLib&) = delete;
    DynLib& operator=(const DynLib&) = delete;
    DynLib(DynLib&& o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
    DynLib& operator=(DynLib&& o) noexcept
    {
        if (this != &o) {
            close();
            m_handle = o.m_handle;
            o.m_handle = nullptr;
        }
        return *this;
    }

    bool open(const std::filesystem::path& path, bool alteredSearch = false);
    void close();
    bool valid() const { return m_handle != nullptr; }
    void* symbol(const char* name) const;

    template <typename Fn>
    Fn symbolAs(const char* name) const
    {
        return reinterpret_cast<Fn>(symbol(name));
    }

private:
    void* m_handle = nullptr;
};

}
