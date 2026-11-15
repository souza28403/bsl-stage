#include "license/Licenca.h"

#include "license/ChavePublica.h"

#include <monocypher.h>

#include <cstring>

namespace bsl::licenca {

namespace {

constexpr char kAlfabeto[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::string_view kPrefixo = "BSL1";
constexpr std::size_t kCaracteres = (kTamanhoAssinatura * 8 + 4) / 5;
constexpr std::size_t kGrupo = 5;

bool espaco(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

char maiuscula(char c)
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

char minuscula(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

int valorDe(char c)
{
    if (c == 'O')
        c = '0';
    else if (c == 'I' || c == 'L')
        c = '1';
    for (int i = 0; i < 32; ++i)
        if (kAlfabeto[i] == c)
            return i;
    return -1;
}

std::string codificar(const Assinatura& dados)
{
    std::string saida;
    std::uint32_t acumulado = 0;
    int bits = 0;
    for (std::uint8_t b : dados) {
        acumulado = (acumulado << 8) | b;
        bits += 8;
        while (bits >= 5) {
            saida.push_back(kAlfabeto[(acumulado >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    if (bits > 0)
        saida.push_back(kAlfabeto[(acumulado << (5 - bits)) & 31]);
    return saida;
}

bool decodificar(std::string_view texto, Assinatura& saida)
{
    if (texto.size() != kCaracteres)
        return false;
    std::uint32_t acumulado = 0;
    int bits = 0;
    std::size_t posicao = 0;
    for (char c : texto) {
        const int v = valorDe(c);
        if (v < 0)
            return false;
        acumulado = (acumulado << 5) | static_cast<std::uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            saida[posicao++] = static_cast<std::uint8_t>((acumulado >> (bits - 8)) & 255);
            bits -= 8;
        }
    }
    return posicao == kTamanhoAssinatura && (acumulado & ((1u << bits) - 1)) == 0;
}

}

std::string normalizarEmail(std::string_view email)
{
    std::size_t ini = 0;
    std::size_t fim = email.size();
    while (ini < fim && espaco(email[ini]))
        ++ini;
    while (fim > ini && espaco(email[fim - 1]))
        --fim;
    std::string saida;
    saida.reserve(fim - ini);
    for (std::size_t i = ini; i < fim; ++i)
        saida.push_back(minuscula(email[i]));
    return saida;
}

std::string mensagemAssinada(std::string_view email)
{
    return "BSL Stage|v1|" + normalizarEmail(email);
}

std::string formatarChave(const Assinatura& assinatura)
{
    const std::string corpo = codificar(assinatura);
    std::string saida(kPrefixo);
    for (std::size_t i = 0; i < corpo.size(); i += kGrupo) {
        saida.push_back('-');
        saida.append(corpo, i, kGrupo);
    }
    return saida;
}

bool lerChave(std::string_view texto, Assinatura& assinatura)
{
    std::string limpo;
    for (char c : texto) {
        if (espaco(c) || c == '-')
            continue;
        limpo.push_back(maiuscula(c));
    }
    if (limpo.size() != kPrefixo.size() + kCaracteres)
        return false;
    for (std::size_t i = 0; i < kPrefixo.size(); ++i) {
        const int a = valorDe(limpo[i]);
        const int b = valorDe(kPrefixo[i]);
        if (a < 0 || a != b)
            return false;
    }
    return decodificar(std::string_view(limpo).substr(kPrefixo.size()), assinatura);
}

Publica derivarPublica(const Semente& semente)
{
    Semente copia = semente;
    std::uint8_t secreta[64];
    Publica publica{};
    crypto_eddsa_key_pair(secreta, publica.data(), copia.data());
    crypto_wipe(secreta, sizeof(secreta));
    crypto_wipe(copia.data(), copia.size());
    return publica;
}

std::string gerarChave(const Semente& semente, std::string_view email)
{
    Semente copia = semente;
    std::uint8_t secreta[64];
    std::uint8_t publica[32];
    crypto_eddsa_key_pair(secreta, publica, copia.data());
    const std::string msg = mensagemAssinada(email);
    Assinatura assinatura{};
    crypto_eddsa_sign(assinatura.data(), secreta, reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
    crypto_wipe(secreta, sizeof(secreta));
    crypto_wipe(copia.data(), copia.size());
    return formatarChave(assinatura);
}

bool verificarChave(const Publica& publica, std::string_view email, std::string_view chave)
{
    Assinatura assinatura{};
    if (normalizarEmail(email).empty() || !lerChave(chave, assinatura))
        return false;
    const std::string msg = mensagemAssinada(email);
    return crypto_eddsa_check(assinatura.data(), publica.data(), reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()) == 0;
}

bool verificarChave(std::string_view email, std::string_view chave)
{
    Publica publica{};
    std::memcpy(publica.data(), kChavePublica, publica.size());
    return verificarChave(publica, email, chave);
}

}
