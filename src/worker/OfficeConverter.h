#pragma once

#include <filesystem>
#include <string>

namespace bsl::worker {

struct ConvertResult
{
    bool ok = false;
    std::filesystem::path pdf;
    std::string error;
    bool fromCache = false;
    std::string via;
};

std::filesystem::path docCachePath(const std::filesystem::path& source);

ConvertResult convertToPdf(const std::filesystem::path& source);

}
