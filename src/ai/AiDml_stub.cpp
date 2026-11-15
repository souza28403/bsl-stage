#include "ai/AiDml.h"

namespace bsl::ai {

struct DmlDevice::Impl
{
};

DmlDevice::DmlDevice() : impl_(std::make_unique<Impl>()) {}

DmlDevice::~DmlDevice() = default;

std::unique_ptr<DmlDevice> DmlDevice::load(const std::filesystem::path&)
{
    return std::unique_ptr<DmlDevice>(new DmlDevice());
}

bool DmlDevice::init(std::string&, std::string& error)
{
    error = "DirectML indisponivel nesta plataforma";
    return false;
}

void DmlDevice::appendProvider(Ort::SessionOptions&) const {}

std::vector<AdapterInfo> listAdapters()
{
    return {};
}

}
