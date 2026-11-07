#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace bsl::dialogs {

std::optional<std::filesystem::path> openProject(HWND owner, const std::filesystem::path& initialDir = {});
std::optional<std::filesystem::path> saveProject(HWND owner, const std::filesystem::path& currentFile = {});
std::optional<std::filesystem::path> openMedia(HWND owner, const std::filesystem::path& initialDir = {});

std::optional<std::filesystem::path> openFile(HWND owner, const std::wstring& filterName, const std::wstring& spec,
                                              const std::filesystem::path& initialDir = {});

std::optional<std::filesystem::path> saveFile(HWND owner, const std::wstring& title, const std::wstring& filterName,
                                              const std::wstring& spec, const std::wstring& defaultName,
                                              const std::wstring& defaultExt, const std::filesystem::path& initialDir = {});

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initialDir = {});

}
