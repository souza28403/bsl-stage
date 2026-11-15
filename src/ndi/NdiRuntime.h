#pragma once

#include "ndi_min.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::ndi {

#if defined(_WIN32)
inline constexpr const char* kRuntimeDllName = "Processing.NDI.Lib.x64.dll";
#elif defined(__APPLE__)
inline constexpr const char* kRuntimeDllName = "libndi.dylib";
#else
inline constexpr const char* kRuntimeDllName = "libndi.so.6";
#endif
inline constexpr const char* kDownloadUrl = "https://ndi.video/tools/";
inline constexpr const char* kMissingText = "NDI Runtime não encontrado";

class Runtime
{
public:
    Runtime() = default;
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    static Runtime& instance();
    static void setLibraryOverride(const std::filesystem::path& dll);

    bool load(const std::filesystem::path& explicitDll = {});

    static std::vector<std::filesystem::path> candidates(const std::filesystem::path& explicitDll);

    bool available() const { return api_ != nullptr; }
    const NDIlib_v5_t* api() const { return api_; }
    const std::string& version() const { return version_; }
    const std::string& error() const { return error_; }
    const std::filesystem::path& libraryPath() const { return path_; }

private:
    const NDIlib_v5_t* api_ = nullptr;
    std::string version_;
    std::string error_;
    std::filesystem::path path_;
    bool initialized_ = false;
};

}
