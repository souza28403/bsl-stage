#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace bsl::licenca {

constexpr std::size_t kTamanhoAssinatura = 64;
constexpr std::size_t kTamanhoSemente = 32;
constexpr std::size_t kTamanhoPublica = 32;

using Semente = std::array<std::uint8_t, kTamanhoSemente>;
using Publica = std::array<std::uint8_t, kTamanhoPublica>;
using Assinatura = std::array<std::uint8_t, kTamanhoAssinatura>;

std::string normalizarEmail(std::string_view email);
std::string mensagemAssinada(std::string_view email);

std::string formatarChave(const Assinatura& assinatura);
bool lerChave(std::string_view texto, Assinatura& assinatura);

Publica derivarPublica(const Semente& semente);
std::string gerarChave(const Semente& semente, std::string_view email);

bool verificarChave(const Publica& publica, std::string_view email, std::string_view chave);
bool verificarChave(std::string_view email, std::string_view chave);

}
