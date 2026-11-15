#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace bsl::dialogs {

std::optional<std::filesystem::path> openProject(void* owner, const std::filesystem::path& initialDir = {});
std::optional<std::filesystem::path> saveProject(void* owner, const std::filesystem::path& currentFile = {});
std::optional<std::filesystem::path> openMedia(void* owner, const std::filesystem::path& initialDir = {});

std::optional<std::filesystem::path> openFile(void* owner, const std::wstring& filterName, const std::wstring& spec,
                                              const std::filesystem::path& initialDir = {});

std::optional<std::filesystem::path> saveFile(void* owner, const std::wstring& title, const std::wstring& filterName,
                                              const std::wstring& spec, const std::wstring& defaultName,
                                              const std::wstring& defaultExt, const std::filesystem::path& initialDir = {});

std::optional<std::filesystem::path> pickFolder(void* owner, const std::filesystem::path& initialDir = {});

}
