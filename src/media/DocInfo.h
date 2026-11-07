#pragma once

#include <filesystem>

namespace bsl::media {

void setDocPageCount(const std::filesystem::path& file, int pages);
int docPageCount(const std::filesystem::path& file);

}
