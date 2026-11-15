#pragma once

#include <filesystem>
#include <string>

namespace bsl::media {

std::wstring workerExePath();

void* workerJob();

}
