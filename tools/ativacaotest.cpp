#include "license/Ativacao.h"
#include "license/Licenca.h"

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int g_falhas = 0;

void conferir(bool ok, const char* nome)
{
    std::printf("%s %s\n", ok ? "ok   " : "FALHA", nome);
    if (!ok)
        ++g_falhas;
}

std::string ambiente(const char* nome)
{
#ifdef _WIN32
    char* v = nullptr;
    std::size_t n = 0;
    std::string r;
    if (_dupenv_s(&v, &n, nome) == 0 && v)
        r = v;
    std::free(v);
    return r;
#else
    const char* v = std::getenv(nome);
    return v ? std::string(v) : std::string();
#endif
}

void escrever(const std::filesystem::path& p, const std::string& conteudo)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << conteudo;
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "bslstage_ativacaotest";
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path arquivo = base / "sub" / "licenca.txt";
    std::string email;

    conferir(!bsl::licenca::lerAtivacao(arquivo, email), "arquivo ausente retorna false");

    conferir(!bsl::licenca::ativar(arquivo, "pessoa@exemplo.com", "BSL1-INVALIDA-00000"), "chave invalida recusada");
    conferir(!fs::exists(arquivo), "chave invalida nao grava");

    fs::create_directories(base, ec);
    escrever(base / "lixo.txt", "isto nao e uma licenca");
    conferir(!bsl::licenca::lerAtivacao(base / "lixo.txt", email), "arquivo corrompido retorna false");
    escrever(base / "lixo2.txt", "pessoa@exemplo.com\nBSL1-ABCDE-FGHJK\n");
    conferir(!bsl::licenca::lerAtivacao(base / "lixo2.txt", email), "arquivo com chave falsa retorna false");
    escrever(base / "vazio.txt", "");
    conferir(!bsl::licenca::lerAtivacao(base / "vazio.txt", email), "arquivo vazio retorna false");

    const std::string emailOk = ambiente("BSL_TESTE_EMAIL");
    const std::string chaveOk = ambiente("BSL_TESTE_CHAVE");
    if (emailOk.empty() || chaveOk.empty()) {
        std::printf("pulado: defina BSL_TESTE_EMAIL e BSL_TESTE_CHAVE para testar uma chave valida\n");
    } else {
        conferir(bsl::licenca::verificarChave(emailOk, chaveOk), "variaveis de ambiente trazem chave valida");

        std::string quebrada;
        for (std::size_t i = 0; i < chaveOk.size(); ++i) {
            quebrada += chaveOk[i];
            if (i % 7 == 6)
                quebrada += (i % 2 ? "\n  " : " ");
        }
        const std::string emailSujo = "  " + emailOk + " \n";
        conferir(bsl::licenca::ativar(arquivo, emailSujo, " " + quebrada + "\r\n"), "ativar aceita chave colada com espacos e quebras");
        conferir(fs::exists(arquivo), "ativar grava o arquivo");
        email.clear();
        conferir(bsl::licenca::lerAtivacao(arquivo, email), "lerAtivacao confere o arquivo gravado");
        conferir(email == bsl::licenca::normalizarEmail(emailOk) || email == emailOk, "e-mail lido corresponde");

        std::string maiusculo = emailOk;
        for (char& c : maiusculo)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        conferir(bsl::licenca::verificarChave("  " + maiusculo + " ", chaveOk) ==
                     bsl::licenca::verificarChave(emailOk, chaveOk),
                 "e-mail com maiusculas e espacos normaliza igual a verificarChave");

        std::string errada = chaveOk;
        errada[errada.size() - 1] = errada.back() == 'A' ? 'B' : 'A';
        const fs::path outro = base / "outro.txt";
        conferir(!bsl::licenca::ativar(outro, emailOk, errada), "chave alterada recusada");
        conferir(!fs::exists(outro), "chave alterada nao grava");
        conferir(!bsl::licenca::ativar(outro, "outra@exemplo.com", chaveOk), "e-mail diferente recusado");
    }

    fs::remove_all(base, ec);
    std::printf(g_falhas ? "RESULTADO: %d falha(s)\n" : "RESULTADO: tudo certo\n", g_falhas);
    return g_falhas ? 1 : 0;
}
