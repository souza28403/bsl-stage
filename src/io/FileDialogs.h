#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace bsl::dialogs {

// Dialogos nativos do Windows (IFileOpenDialog / IFileSaveDialog). Bloqueiam ate o usuario fechar;
// nullopt = cancelado (ou falha ao abrir o dialogo).

std::optional<std::filesystem::path> openProject(HWND owner, const std::filesystem::path& initialDir = {});
std::optional<std::filesystem::path> saveProject(HWND owner, const std::filesystem::path& currentFile = {});
// "Localizar...": midia suportada (videos e imagens) ou qualquer arquivo
std::optional<std::filesystem::path> openMedia(HWND owner, const std::filesystem::path& initialDir = {});

// Escolha de um arquivo qualquer (parametros de fonte do tipo "arquivo"). 'spec' ex.: L"*.png;*.jpg"
std::optional<std::filesystem::path> openFile(HWND owner, const std::wstring& filterName, const std::wstring& spec,
                                              const std::filesystem::path& initialDir = {});

} // namespace bsl::dialogs
