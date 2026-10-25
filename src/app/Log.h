#pragma once

#include <string>

namespace bsl::log {

// Log simples: vai para %APPDATA%\BSL Stage\bslstage.log, stderr e OutputDebugString.
// Avisos tambem ficam disponiveis por alguns segundos para a barra de status.
void info(const std::string& message);
void warn(const std::string& message);

// Ultimo aviso, se tiver menos de 'seconds' segundos (senao string vazia).
std::string recentWarning(double seconds = 8.0);

} // namespace bsl::log
