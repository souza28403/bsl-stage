#pragma once

#include "app/Settings.h"

namespace bsl {

// Aponta o catalogo de plugins FFGL para as pastas: Documents\BSL Stage\Plugins
// (criada se nao existir), as pastas extras das configuracoes e, se ligado, o Extra Effects do Resolume. Inicia a
// varredura em segundo plano (cache em %LOCALAPPDATA%\BSL Stage\ffgl-cache.json).
void configureFfglCatalog(const Settings& settings);

} // namespace bsl
