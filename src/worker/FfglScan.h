#pragma once

#include <string>

namespace bsl::worker {

// bslstage_worker.exe --scan-ffgl <dll> [<saida.json>]: carrega a DLL, le informacoes e parametros do plugin FFGL e
// escreve o JSON (ffgl::ScanResult) na saida padrao ou no arquivo. Nao volta: termina o processo (o plugin pode
// travar ao descarregar). Codigo de saida 0 = o JSON foi gravado (mesmo com ok=false).
[[noreturn]] void runFfglScan(const std::wstring& dll, const std::wstring& outFile);

} // namespace bsl::worker
