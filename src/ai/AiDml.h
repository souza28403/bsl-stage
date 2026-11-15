#pragma once

#include "ai/AiRuntime.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef ORT_API_MANUAL_INIT
#define ORT_API_MANUAL_INIT
#endif
#include <onnxruntime_cxx_api.h>

namespace bsl::ai {

class DmlDevice
{
public:
    ~DmlDevice();
    DmlDevice(const DmlDevice&) = delete;
    DmlDevice& operator=(const DmlDevice&) = delete;

    static std::unique_ptr<DmlDevice> load(const std::filesystem::path& dir);

    bool init(std::string& gpuName, std::string& error);
    void appendProvider(Ort::SessionOptions& options) const;

private:
    DmlDevice();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
