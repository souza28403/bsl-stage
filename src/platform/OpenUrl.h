#pragma once

#include <filesystem>
#include <string>

namespace bslplat {

bool openUrl(const std::string& urlUtf8);
bool openFolder(const std::filesystem::path& dir);
bool revealFile(const std::filesystem::path& file);

}
