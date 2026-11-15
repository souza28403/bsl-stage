#include "license/Licenca.h"

#include <windows.h>
#include <bcrypt.h>
#include <msiquery.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl::licenca;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHA: %s\n", what.c_str());
    }
}

#define CHECK(cond) check((cond), #cond " (linha " + std::to_string(__LINE__) + ")")

void section(const char* nome)
{
    std::printf("[%s]\n", nome);
}

Semente novaSemente()
{
    Semente s{};
    BCryptGenRandom(nullptr, s.data(), static_cast<ULONG>(s.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return s;
}

std::string trocarCaractere(std::string chave, std::size_t pos)
{
    chave[pos] = chave[pos] == '2' ? '3' : '2';
    return chave;
}

void testeIdaEVolta(const Semente& semente, const Publica& publica)
{
    section("ida e volta");
    const std::string chave = gerarChave(semente, "cliente@exemplo.com");
    CHECK(chave.rfind("BSL1-", 0) == 0);
    CHECK(chave.size() == 128);
    CHECK(verificarChave(publica, "cliente@exemplo.com", chave));
    CHECK(gerarChave(semente, "cliente@exemplo.com") == chave);
    CHECK(gerarChave(semente, "outro@exemplo.com") != chave);
    std::size_t grupos = 0;
    std::size_t inicio = 5;
    while (inicio < chave.size()) {
        const std::size_t fim = chave.find('-', inicio);
        const std::size_t tam = (fim == std::string::npos ? chave.size() : fim) - inicio;
        CHECK(tam == 5 || (fim == std::string::npos && tam == 3));
        ++grupos;
        if (fim == std::string::npos)
            break;
        inicio = fim + 1;
    }
    CHECK(grupos == 21);
    CHECK(chave.find_first_of("ILOU", 5) == std::string::npos);
}

void testeEmail(const Semente& semente, const Publica& publica)
{
    section("normalizacao do e-mail");
    CHECK(normalizarEmail("  Teste@Exemplo.COM \t") == "teste@exemplo.com");
    CHECK(normalizarEmail("   ").empty());
    CHECK(mensagemAssinada(" A@B.com ") == "BSL Stage|v1|a@b.com");
    const std::string chave = gerarChave(semente, "teste@exemplo.com");
    CHECK(verificarChave(publica, "  TESTE@Exemplo.com  ", chave));
    CHECK(!verificarChave(publica, "teste2@exemplo.com", chave));
    CHECK(!verificarChave(publica, "", chave));
    const Publica outra = derivarPublica(novaSemente());
    CHECK(!verificarChave(outra, "teste@exemplo.com", chave));
}

void testeAdulteracao(const Semente& semente, const Publica& publica)
{
    section("chave adulterada");
    const std::string chave = gerarChave(semente, "teste@exemplo.com");
    const std::size_t posicoes[] = {6, 30, 70, 120};
    for (std::size_t pos : posicoes) {
        if (chave[pos] == '-')
            continue;
        CHECK(!verificarChave(publica, "teste@exemplo.com", trocarCaractere(chave, pos)));
    }
    std::string alterada = chave;
    alterada[7] = alterada[7] == '0' ? '8' : '0';
    CHECK(!verificarChave(publica, "teste@exemplo.com", alterada));
    CHECK(!verificarChave(publica, "teste@exemplo.com", "BSL2" + chave.substr(4)));
    CHECK(!verificarChave(publica, "teste@exemplo.com", ""));
}

void testeLeituraTolerante(const Semente& semente, const Publica& publica)
{
    section("leitura tolerante");
    const std::string email = "teste@exemplo.com";
    const std::string chave = gerarChave(semente, email);
    std::string minus = chave;
    std::transform(minus.begin(), minus.end(), minus.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    CHECK(verificarChave(publica, email, minus));
    std::string semHifen;
    for (char c : chave)
        if (c != '-')
            semHifen.push_back(c);
    CHECK(verificarChave(publica, email, semHifen));
    std::string espacos;
    for (char c : chave)
        espacos += (c == '-') ? std::string(" ") : std::string(1, c);
    CHECK(verificarChave(publica, email, "  " + espacos + "\r\n"));
    std::string confundido = chave;
    for (std::size_t i = 5; i < confundido.size(); ++i) {
        if (confundido[i] == '0')
            confundido[i] = (i % 2) ? 'O' : 'o';
        else if (confundido[i] == '1')
            confundido[i] = (i % 3 == 0) ? 'I' : (i % 3 == 1 ? 'l' : 'L');
    }
    CHECK(verificarChave(publica, email, confundido));
    CHECK(verificarChave(publica, email, "bsl1" + chave.substr(4)));
    CHECK(verificarChave(publica, email, chave.substr(5)) == false);
    Assinatura a{};
    CHECK(lerChave(chave, a));
    CHECK(formatarChave(a) == chave);
}

void testeTamanho(const Semente& semente, const Publica& publica)
{
    section("tamanho invalido");
    const std::string email = "teste@exemplo.com";
    const std::string chave = gerarChave(semente, email);
    Assinatura a{};
    CHECK(!lerChave("", a));
    CHECK(!lerChave("BSL1", a));
    CHECK(!lerChave("BSL1-", a));
    CHECK(!lerChave(chave.substr(0, chave.size() - 1), a));
    CHECK(!lerChave(chave + "0", a));
    CHECK(!lerChave(chave + "-00000", a));
    CHECK(!verificarChave(publica, email, chave.substr(0, chave.size() - 6)));
    std::string invalido = chave;
    invalido[10] = 'U';
    CHECK(!lerChave(invalido, a));
    invalido[10] = '*';
    CHECK(!lerChave(invalido, a));
}

std::string lerPrimeiraChaveDoCsv(std::string& emailSaida)
{
    std::ifstream in(fs::path(BSL_SOURCE_DIR) / "Licencas BSL Stage" / "clientes.csv", std::ios::binary);
    std::string linha;
    std::getline(in, linha);
    while (std::getline(in, linha)) {
        while (!linha.empty() && (linha.back() == '\r' || linha.back() == '\n'))
            linha.pop_back();
        std::vector<std::string> campos;
        std::string atual;
        bool aspas = false;
        for (std::size_t i = 0; i < linha.size(); ++i) {
            const char c = linha[i];
            if (aspas) {
                if (c == '"' && i + 1 < linha.size() && linha[i + 1] == '"') {
                    atual.push_back('"');
                    ++i;
                } else if (c == '"') {
                    aspas = false;
                } else {
                    atual.push_back(c);
                }
            } else if (c == '"') {
                aspas = true;
            } else if (c == ',') {
                campos.push_back(atual);
                atual.clear();
            } else {
                atual.push_back(c);
            }
        }
        campos.push_back(atual);
        if (campos.size() == 4 && campos[3].rfind("BSL1-", 0) == 0) {
            emailSaida = campos[1];
            return campos[3];
        }
    }
    return {};
}

std::string lerProp(MSIHANDLE h, const wchar_t* nome)
{
    wchar_t buf[64] = {0};
    DWORD n = 64;
    if (MsiGetPropertyW(h, nome, buf, &n) != ERROR_SUCCESS)
        return "?";
    std::string s;
    for (DWORD i = 0; i < n; ++i)
        s.push_back(static_cast<char>(buf[i]));
    return s;
}

std::string rodarAcao(const fs::path& msi, const std::wstring& email, const std::wstring& chave)
{
    MSIHANDLE h = 0;
    const UINT aberto = MsiOpenPackageExW(msi.c_str(), 0, &h);
    if (aberto != ERROR_SUCCESS)
        return "abrir" + std::to_string(aberto);
    MsiSetPropertyW(h, L"BSL_EMAIL", email.c_str());
    MsiSetPropertyW(h, L"BSL_CHAVE", chave.c_str());
    const UINT r = MsiDoActionW(h, L"VerificarLicenca");
    std::string resultado = r == ERROR_SUCCESS ? lerProp(h, L"BSL_LICENCA_OK") : "acao" + std::to_string(r);
    std::printf("  resultado da acao: %s\n", resultado.c_str());
    MsiCloseHandle(h);
    return resultado;
}

std::wstring larga(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void testePacote()
{
    section("pacote msi (acao VerificarLicenca, sem instalar)");
    const fs::path dist = fs::path(BSL_SOURCE_DIR) / "dist";
    fs::path msi;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dist, ec)) {
        const std::string nome = e.path().filename().string();
        if (e.path().extension() == ".msi" && nome.rfind("BSL-Stage-", 0) == 0 &&
            (msi.empty() || fs::last_write_time(e.path(), ec) > fs::last_write_time(msi, ec)))
            msi = e.path();
    }
    if (msi.empty()) {
        std::printf("  pulado: nenhum .msi em dist\n");
        return;
    }
    const fs::path temp = fs::temp_directory_path(ec) / "bslstage_licencatest";
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);
    const fs::path copia = temp / msi.filename();
    fs::copy_file(msi, copia, fs::copy_options::overwrite_existing, ec);
    CHECK(!ec);
    CHECK(rodarAcao(copia, L"teste@exemplo.com", L"BSL1-00000-00000") == "0");
    CHECK(rodarAcao(copia, L"", L"") == "0");
    std::string email;
    const std::string chave = lerPrimeiraChaveDoCsv(email);
    if (chave.empty()) {
        std::printf("  sem chave real em clientes.csv: so o caso negativo foi conferido\n");
    } else {
        CHECK(rodarAcao(copia, larga(email), larga(chave)) == "1");
        CHECK(rodarAcao(copia, larga("  " + email + " "), larga(chave)) == "1");
        CHECK(rodarAcao(copia, L"outro@exemplo.com", larga(chave)) == "0");
    }
    fs::remove_all(temp, ec);
}

}

int main()
{
    const Semente semente = novaSemente();
    const Publica publica = derivarPublica(semente);
    testeIdaEVolta(semente, publica);
    testeEmail(semente, publica);
    testeAdulteracao(semente, publica);
    testeLeituraTolerante(semente, publica);
    testeTamanho(semente, publica);
    testePacote();
    std::printf("%d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
