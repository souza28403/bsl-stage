#pragma once

#include <filesystem>
#include <string>

namespace bslplat {

std::filesystem::path configDir();
std::filesystem::path dataDir();
std::filesystem::path cacheDir();
std::filesystem::path desktopDir();
std::filesystem::path documentsDir();
std::filesystem::path homeDir();

std::filesystem::path xdgDir(const std::string& envValue, const std::filesystem::path& home, const char* fallbackRelative);

}
