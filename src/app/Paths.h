#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::paths {

// Testes (--qa): usa outra pasta no lugar de %APPDATA%\BSL Stage (settings, imgui.ini, log, autosave)
void setAppDataOverride(const std::filesystem::path& dir);

// %APPDATA%\BSL Stage (criada se nao existir)
std::filesystem::path appDataDir();
std::filesystem::path desktopDir();
// Documents\BSL Stage (nao e criada)
std::filesystem::path documentsDir();
// Pasta principal dos shaders ISF: Documents\BSL Stage\ISF (nos testes --qa: appdata/ISF do teste)
std::filesystem::path isfDir();

// %LOCALAPPDATA%\BSL Stage (cache do FFGL); nos testes --qa: a mesma pasta de appdata do teste. Criada se nao existir.
std::filesystem::path localDataDir();
// Pasta principal dos plugins FFGL: Documents\BSL Stage\Plugins (nos testes --qa: appdata/Plugins do teste)
std::filesystem::path pluginsDir();
// Documents\Resolume Arena\Extra Effects e Documents\Resolume Avenue\Extra Effects, so as que existem
std::vector<std::filesystem::path> resolumeEffectDirs();

// Pasta "assets" (fontes, marca). Procura ao lado do exe e, no desenvolvimento,
// sobe ate a raiz do projeto. Vazio se nao achar.
std::filesystem::path assetsDir();

// Abre o Explorer com o arquivo selecionado (explorer /select,)
void revealInExplorer(const std::filesystem::path& file);

// Caminho -> string UTF-8 (para exibir na UI)
std::string toUtf8(const std::filesystem::path& p);

} // namespace bsl::paths
