#include "app/ShowCacheProc.h"

namespace bsl::detail {

ShowProc::~ShowProc() = default;

std::unique_ptr<ShowProc> ShowProc::start(const std::filesystem::path&, const std::filesystem::path&, std::string& error)
{
    error = "Conversão disponível só no Windows";
    return nullptr;
}

bool ShowProc::running(int&)
{
    return false;
}

void ShowProc::kill() {}

}
