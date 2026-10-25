#pragma once

// Localizacao e job object dos processos auxiliares (bslstage_worker.exe). Sem dependencia de GL.

#include <windows.h>

#include <filesystem>
#include <string>

namespace bsl::media {

// <pasta do exe>\bslstage_worker.exe
std::wstring workerExePath();

// Job object unico do app com KILL_ON_JOB_CLOSE: tudo que for colocado nele morre junto com o app.
HANDLE workerJob();

} // namespace bsl::media
