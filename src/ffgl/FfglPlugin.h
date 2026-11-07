#pragma once

#include "ffgl/FfglInfo.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace bsl::ffgl {

using Word = std::uintptr_t;

struct CallResult
{
    bool ok = false;
    Word value = 0;
    unsigned long exception = 0;
    std::uint32_t u32() const { return static_cast<std::uint32_t>(value); }
    float f32() const;
};

std::string exceptionText(unsigned long code);
Word floatWord(float v);

class Plugin
{
public:
    using LogSink = void (*)(const std::string& message);
    static void setLogSink(LogSink sink);

    static std::shared_ptr<Plugin> open(const std::filesystem::path& file, std::string* error);
    ~Plugin();

    const std::filesystem::path& path() const { return path_; }

    CallResult call(std::uint32_t code, Word in = 0, void* instance = nullptr);
    CallResult callPtr(std::uint32_t code, const void* in, void* instance = nullptr)
    {
        return call(code, reinterpret_cast<Word>(in), instance);
    }

    bool initialise(std::string* error);
    bool initialised() const { return initialised_; }
    void deinitialise();
    void unload();

    bool readMemory(Word ptr, void* dst, std::size_t n);
    std::string readString(Word ptr, std::size_t maxLen = 512);

private:
    Plugin() = default;

    std::filesystem::path path_;
    void* module_ = nullptr;
    void* plugMain_ = nullptr;
    bool initialised_ = false;
};

namespace fn {
inline constexpr std::uint32_t GetInfo = 0;
inline constexpr std::uint32_t InitialiseV2 = 34;
inline constexpr std::uint32_t Deinitialise = 2;
inline constexpr std::uint32_t GetNumParameters = 4;
inline constexpr std::uint32_t GetParameterName = 5;
inline constexpr std::uint32_t GetParameterDefault = 6;
inline constexpr std::uint32_t GetParameterDisplay = 7;
inline constexpr std::uint32_t SetParameter = 8;
inline constexpr std::uint32_t GetParameter = 9;
inline constexpr std::uint32_t GetPluginCaps = 10;
inline constexpr std::uint32_t EnablePluginCap = 49;
inline constexpr std::uint32_t GetExtendedInfo = 13;
inline constexpr std::uint32_t GetParameterType = 15;
inline constexpr std::uint32_t ProcessOpenGL = 17;
inline constexpr std::uint32_t InstantiateGL = 18;
inline constexpr std::uint32_t DeinstantiateGL = 19;
inline constexpr std::uint32_t SetTime = 20;
inline constexpr std::uint32_t Resize = 23;
inline constexpr std::uint32_t GetNumParameterElements = 31;
inline constexpr std::uint32_t GetParameterElementName = 35;
inline constexpr std::uint32_t GetParameterElementValue = 36;
inline constexpr std::uint32_t GetParameterUsage = 32;
inline constexpr std::uint32_t SetBeatInfo = 38;
inline constexpr std::uint32_t SetHostInfo = 39;
inline constexpr std::uint32_t GetRange = 41;
inline constexpr std::uint32_t GetParamGroup = 50;
inline constexpr std::uint32_t GetNumFileExtensions = 43;
inline constexpr std::uint32_t GetFileExtension = 44;
inline constexpr std::uint32_t GetParamVisibility = 45;
inline constexpr std::uint32_t GetParamEvents = 46;
}

ScanResult scanPluginFile(const std::filesystem::path& file, std::shared_ptr<Plugin>* loaded = nullptr);

}
