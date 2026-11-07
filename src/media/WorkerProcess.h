#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace bsl::media {

std::wstring workerExePath();

HANDLE workerJob();

}
