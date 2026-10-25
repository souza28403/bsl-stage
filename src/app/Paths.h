#pragma once

#include <filesystem>
#include <string>

namespace bsl::paths {

// Testes (--qa): usa outra pasta no lugar de %APPDATA%\BSL Stage (settings, imgui.ini, log, autosave)
void setAppDataOverride(const std::filesystem::path& dir);

// %APPDATA%\BSL Stage (criada se nao existir)
std::filesystem::path appDataDir();
std::filesystem::path desktopDir();

// Pasta "assets" (fontes, marca). Procura ao lado do exe e, no desenvolvimento,
// sobe ate a raiz do projeto. Vazio se nao achar.
std::filesystem::path assetsDir();

// Abre o Explorer com o arquivo selecionado (explorer /select,)
void revealInExplorer(const std::filesystem::path& file);

// Caminho -> string UTF-8 (para exibir na UI)
std::string toUtf8(const std::filesystem::path& p);

} // namespace bsl::paths
