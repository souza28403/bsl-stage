#pragma once

#include <filesystem>

namespace bsl::crash {

void install(const std::filesystem::path& folder);
std::filesystem::path folder();

}
