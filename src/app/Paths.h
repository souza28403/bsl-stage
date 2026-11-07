#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::paths {

void setAppDataOverride(const std::filesystem::path& dir);

std::filesystem::path appDataDir();
std::filesystem::path desktopDir();
std::filesystem::path documentsDir();
std::filesystem::path isfDir();

std::filesystem::path localDataDir();
std::filesystem::path pluginsDir();

std::filesystem::path assetsDir();

void revealInExplorer(const std::filesystem::path& file);

std::string toUtf8(const std::filesystem::path& p);

}
