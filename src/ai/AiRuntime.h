#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl::ai {

enum class ModelKind { Depth = 0, Person = 1 };
inline constexpr int kModelKindCount = 2;

enum class DevicePref { Auto, Gpu, Cpu };

struct ModelSpec
{
    ModelKind kind;
    const char* key;
    const char* label;
    const char* fileName;
    const char* license;
    int inputW;
    int inputH;
};
const ModelSpec& modelSpec(ModelKind kind);

std::filesystem::path modelsDir();
void setModelsDirOverride(const std::filesystem::path& dir, bool enable = true);
std::filesystem::path modelPath(ModelKind kind);
bool modelPresent(ModelKind kind);

struct AdapterInfo
{
    std::string name;
    std::uint64_t vramBytes = 0;
    bool software = false;
    bool usable = false;
};
std::vector<AdapterInfo> listAdapters();

struct RuntimeInfo
{
    bool dllFound = false;
    bool loaded = false;
    std::string version;
    std::string error;
    bool gpuAvailable = false;
    std::string gpuName;
    std::string gpuError;
};
RuntimeInfo runtimeInfo();

struct RuntimeSnapshot
{
    bool tried = false;
    RuntimeInfo info;
};
RuntimeSnapshot peekRuntime();

class Session
{
public:
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    static std::unique_ptr<Session> create(ModelKind kind, DevicePref pref, std::string& error);

    ModelKind kind() const { return kind_; }
    bool onGpu() const { return onGpu_; }
    const std::string& deviceName() const { return deviceName_; }
    int inputW() const { return w_; }
    int inputH() const { return h_; }
    double lastMs() const { return lastMs_; }

    bool run(const std::uint8_t* rgb, std::vector<float>& out, std::string& error);

private:
    Session() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ModelKind kind_ = ModelKind::Depth;
    bool onGpu_ = false;
    std::string deviceName_;
    int w_ = 0, h_ = 0;
    double lastMs_ = 0.0;
};

}
