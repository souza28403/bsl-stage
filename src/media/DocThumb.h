#pragma once

#include "media/Frame.h"

#include <filesystem>
#include <string>

namespace bsl::media {

bool renderDocThumbnail(const std::filesystem::path& file, int w, int h, Frame& out, int* pages = nullptr,
                        std::string* error = nullptr);

void cancelDocThumbnails();

}
