#pragma once

#include <string>

namespace bsl::worker {

[[noreturn]] void runFfglScan(const std::wstring& dll, const std::wstring& outFile);

}
