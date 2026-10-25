#pragma once

// Utilitarios do processo auxiliar (sem dependencia do resto do app)

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::worker {

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);
std::string lowerExt(const std::filesystem::path& p); // sem ponto, minuscula

// %LOCALAPPDATA%\BSL Stage (criada se nao existir)
std::filesystem::path localDataDir();

// Anexa uma linha ao %LOCALAPPDATA%\BSL Stage\worker.log (diagnostico de falhas antes do pipe conectar)
void fileLog(const std::string& line);

// PIDs dos processos com esse nome de executavel (ex.: L"POWERPNT.EXE")
std::vector<DWORD> processIds(const wchar_t* exeName);

// Hash FNV-1a de 64 bits em hexadecimal (16 caracteres)
std::string fnv1aHex(const std::string& data);

bool isOfficeExtension(const std::string& ext);

} // namespace bsl::worker
