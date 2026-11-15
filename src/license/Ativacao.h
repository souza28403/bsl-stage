#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace bsl::licenca {

bool lerAtivacao(const std::filesystem::path& arquivo, std::string& email);
bool ativar(const std::filesystem::path& arquivo, std::string_view email, std::string_view chave);

}
