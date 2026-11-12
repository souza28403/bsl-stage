#pragma once

#include "seq/Envelope.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace bsl::seq {

nlohmann::json envelopeParaJson(const Envelope& envelope);
Envelope envelopeDeJson(const nlohmann::json& j);

nlohmann::json bibliotecaParaJson(const BibliotecaEnvelopes& biblioteca);
BibliotecaEnvelopes bibliotecaDeJson(const nlohmann::json& j);

std::filesystem::path caminhoBiblioteca();
bool salvarBiblioteca(const BibliotecaEnvelopes& biblioteca, const std::filesystem::path& arquivo);
BibliotecaEnvelopes carregarBiblioteca(const std::filesystem::path& arquivo);

std::map<int, int> mesclarNaBiblioteca(BibliotecaEnvelopes& biblioteca, const std::vector<Envelope>& doProjeto);

}
