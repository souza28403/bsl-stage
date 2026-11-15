#pragma once

#include <cstddef>
#include <cstdint>

namespace bsl::wp {

#ifdef _WIN32
using ShmLong = long;
#else
using ShmLong = std::int32_t;
#endif
using ShmLong64 = long long;

inline constexpr int kProtocolVersion = 1;
inline constexpr std::uint32_t kShmMagic = 0x57534C42;

struct ShmHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    volatile ShmLong latest;
    std::uint32_t reserved0;
    volatile ShmLong64 seq[2];
    std::uint8_t pad[24];
};
static_assert(sizeof(ShmHeader) == 64, "ShmHeader deve ocupar 64 bytes");

inline std::size_t shmFrameBytes(int w, int h)
{
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
}
inline std::size_t shmTotalBytes(int w, int h)
{
    return sizeof(ShmHeader) + 2u * shmFrameBytes(w, h);
}
inline std::uint8_t* shmBuffer(void* base, int w, int h, int index)
{
    return static_cast<std::uint8_t*>(base) + sizeof(ShmHeader) + static_cast<std::size_t>(index) * shmFrameBytes(w, h);
}

inline constexpr const char* kOfficeExtensions[] = {"pptx", "ppt", "docx", "doc", "xlsx", "xls", "odp", "odt", "ods"};

#ifdef _WIN32
inline constexpr const wchar_t* kWorkerExeName = L"bslstage_worker.exe";
#else
inline constexpr const wchar_t* kWorkerExeName = L"bslstage_worker";
#endif

}
