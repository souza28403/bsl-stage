#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::worker {

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);
std::string lowerExt(const std::filesystem::path& p);

std::filesystem::path localDataDir();

void fileLog(const std::string& line);

std::vector<DWORD> processIds(const wchar_t* exeName);

std::string fnv1aHex(const std::string& data);

bool isOfficeExtension(const std::string& ext);

}
