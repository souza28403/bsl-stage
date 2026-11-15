#include "ffgl/FfglPe.h"
#include "ffgl/FfglPlugin.h"

#include <cstring>

namespace bsl::ffgl {

float CallResult::f32() const
{
    const std::uint32_t bits = u32();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

Word floatWord(float v)
{
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return static_cast<Word>(bits);
}

std::string exceptionText(unsigned long code)
{
    return "falha " + std::to_string(code);
}

void Plugin::setLogSink(LogSink) {}

std::shared_ptr<Plugin> Plugin::open(const std::filesystem::path&, std::string* error)
{
    if (error)
        *error = "Plugins FFGL n찾o est찾o disponØveis nesta plataforma.";
    return nullptr;
}

Plugin::~Plugin() = default;

CallResult Plugin::call(std::uint32_t, Word, void*)
{
    return {};
}

bool Plugin::initialise(std::string* error)
{
    if (error)
        *error = "Plugins FFGL n찾o est찾o disponØveis nesta plataforma.";
    return false;
}

void Plugin::deinitialise() {}

void Plugin::unload() {}

bool Plugin::readMemory(Word, void*, std::size_t)
{
    return false;
}

std::string Plugin::readString(Word, std::size_t)
{
    return {};
}

ScanResult scanPluginFile(const std::filesystem::path&, std::shared_ptr<Plugin>*)
{
    ScanResult r;
    r.error = "Plugins FFGL n찾o est찾o disponØveis nesta plataforma.";
    return r;
}

PeVerdict inspectPe(const std::filesystem::path&, std::string* detail)
{
    if (detail)
        detail->clear();
    return PeVerdict::NotPe;
}

const char* peVerdictText(PeVerdict)
{
    return "indisponivel nesta plataforma";
}

}
