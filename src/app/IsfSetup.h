#pragma once

#include "app/Settings.h"

namespace bsl {

// Aponta a biblioteca ISF para as pastas das configuracoes (Documents\BSL Stage\ISF + as extras) e inicia a varredura
// em segundo plano. Na primeira vez a pasta principal e criada com os shaders de exemplo (assets/isf).
void configureIsfLibrary(const Settings& settings);

} // namespace bsl
