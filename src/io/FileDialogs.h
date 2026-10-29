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

// Salvar um arquivo qualquer (ex.: o XML que o LED Mapper exporta). 'spec' ex.: L"*.xml"; 'defaultExt' sem ponto
std::optional<std::filesystem::path> saveFile(HWND owner, const std::wstring& title, const std::wstring& filterName,
                                              const std::wstring& spec, const std::wstring& defaultName,
                                              const std::wstring& defaultExt, const std::filesystem::path& initialDir = {});

// Escolha de uma pasta (pastas ISF extras)
std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initialDir = {});

} // namespace bsl::dialogs
