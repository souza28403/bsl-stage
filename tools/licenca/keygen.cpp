#include "license/Licenca.h"
#include "platform/TimeUtil.h"
#include "platform/Utf.h"

#include <windows.h>
#include <bcrypt.h>

#include <monocypher.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace bsl::licenca;

namespace {

fs::path pastaLicencas()
{
    return (fs::path(BSL_SOURCE_DIR) / "Licencas BSL Stage").lexically_normal();
}

fs::path arquivoPrivado()
{
    return pastaLicencas() / "chave-privada.bin";
}

fs::path arquivoPublico()
{
    return fs::path(BSL_SOURCE_DIR) / "src" / "license" / "ChavePublica.h";
}

fs::path arquivoClientes()
{
    return pastaLicencas() / "clientes.csv";
}

bool lerSemente(Semente& semente)
{
    std::ifstream in(arquivoPrivado(), std::ios::binary);
    if (!in)
        return false;
    in.read(reinterpret_cast<char*>(semente.data()), static_cast<std::streamsize>(semente.size()));
    char extra;
    return in.gcount() == static_cast<std::streamsize>(semente.size()) && !in.get(extra);
}

std::string campoCsv(const std::string& texto)
{
    if (texto.find_first_of(",\"\r\n") == std::string::npos)
        return texto;
    std::string saida = "\"";
    for (char c : texto) {
        if (c == '"')
            saida.push_back('"');
        saida.push_back(c);
    }
    saida.push_back('"');
    return saida;
}

std::string dataAgora()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    bslplat::localTime(t, tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

int iniciar()
{
    std::error_code ec;
    if (fs::exists(arquivoPrivado(), ec)) {
        std::printf("Ja existe uma chave privada em: %s\nNada foi alterado.\n", arquivoPrivado().string().c_str());
        return 1;
    }
    fs::create_directories(pastaLicencas(), ec);
    Semente semente{};
    if (BCryptGenRandom(nullptr, semente.data(), static_cast<ULONG>(semente.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        std::printf("Falha ao gerar numeros aleatorios.\n");
        return 1;
    }
    const Publica publica = derivarPublica(semente);
    {
        std::ofstream out(arquivoPrivado(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(semente.data()), static_cast<std::streamsize>(semente.size()));
        if (!out) {
            crypto_wipe(semente.data(), semente.size());
            std::printf("Falha ao gravar a chave privada.\n");
            return 1;
        }
    }
    crypto_wipe(semente.data(), semente.size());

    std::string texto = "#pragma once\n\n#include <cstdint>\n\nnamespace bsl::licenca {\n\ninline constexpr std::uint8_t kChavePublica[32] = {\n";
    for (std::size_t i = 0; i < publica.size(); ++i) {
        char b[8];
        std::snprintf(b, sizeof(b), "0x%02x", publica[i]);
        texto += (i % 16 == 0) ? "    " : " ";
        texto += b;
        if (i + 1 < publica.size())
            texto += ",";
        if (i % 16 == 15 || i + 1 == publica.size())
            texto += (i + 1 == publica.size()) ? "};\n" : "\n";
    }
    texto += "\n}\n";
    std::ofstream hdr(arquivoPublico(), std::ios::binary | std::ios::trunc);
    hdr << texto;
    if (!hdr) {
        std::printf("Falha ao gravar %s\n", arquivoPublico().string().c_str());
        return 1;
    }
    std::printf("Par de chaves criado.\nChave privada: %s\nFaca backup dela agora. Sem ela nao ha como gerar chaves compativeis.\nChave publica gravada em: %s\nRecompile o projeto.\n",
                arquivoPrivado().string().c_str(), arquivoPublico().string().c_str());
    return 0;
}

std::string dataJaRegistrada(const std::string& emailNormalizado)
{
    std::ifstream in(arquivoClientes(), std::ios::binary);
    for (std::string linha; std::getline(in, linha);) {
        const size_t a = linha.find(',');
        if (a == std::string::npos)
            continue;
        const size_t b = linha.find(',', a + 1);
        if (b == std::string::npos)
            continue;
        std::string campo = linha.substr(a + 1, b - a - 1);
        if (campo.size() >= 2 && campo.front() == '"' && campo.back() == '"')
            campo = campo.substr(1, campo.size() - 2);
        if (campo == emailNormalizado)
            return linha.substr(0, a);
    }
    return {};
}

int criarChave(const std::string& email, const std::string& nome, std::string& chave, std::string& registroAnterior)
{
    Semente semente{};
    if (!lerSemente(semente)) {
        std::printf("Chave privada ausente ou invalida em: %s\n", arquivoPrivado().string().c_str());
        return 1;
    }
    const std::string normalizado = normalizarEmail(email);
    if (normalizado.empty()) {
        crypto_wipe(semente.data(), semente.size());
        std::printf("E-mail vazio.\n");
        return 1;
    }
    chave = gerarChave(semente, email);
    crypto_wipe(semente.data(), semente.size());
    if (!verificarChave(normalizado, chave)) {
        std::printf("A chave gerada nao confere com a chave publica compilada. Recompile o projeto depois do init.\n");
        return 1;
    }
    registroAnterior = dataJaRegistrada(normalizado);
    if (!registroAnterior.empty())
        return 0;
    std::error_code ec;
    fs::create_directories(pastaLicencas(), ec);
    const bool novo = !fs::exists(arquivoClientes(), ec);
    std::ofstream csv(arquivoClientes(), std::ios::binary | std::ios::app);
    if (!csv) {
        std::printf("Falha ao abrir %s\n", arquivoClientes().string().c_str());
        return 1;
    }
    if (novo)
        csv << "data,email,nome,chave\r\n";
    csv << campoCsv(dataAgora()) << ',' << campoCsv(normalizado) << ',' << campoCsv(nome) << ',' << campoCsv(chave) << "\r\n";
    return 0;
}

int gerar(const std::string& email, const std::string& nome)
{
    std::string chave;
    std::string anterior;
    const int rc = criarChave(email, nome, chave, anterior);
    if (rc == 0)
        std::printf("%s\n", chave.c_str());
    return rc;
}

std::string aparar(std::string s)
{
    const char* espacos = " \t\r\n";
    const size_t ini = s.find_first_not_of(espacos);
    if (ini == std::string::npos)
        return {};
    const size_t fim = s.find_last_not_of(espacos);
    return s.substr(ini, fim - ini + 1);
}

std::string lerLinha(const char* rotulo)
{
    std::printf("%s", rotulo);
    std::fflush(stdout);
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD modo = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &modo)) {
        std::wstring w;
        wchar_t buf[512];
        DWORD lidos = 0;
        while (ReadConsoleW(h, buf, static_cast<DWORD>(std::size(buf)), &lidos, nullptr) && lidos > 0) {
            w.append(buf, lidos);
            if (w.back() == L'\n')
                break;
        }
        return aparar(bslplat::wideToUtf8(w));
    }
    std::string s;
    std::getline(std::cin, s);
    return aparar(s);
}

bool copiarParaAreaDeTransferencia(const std::string& texto)
{
    const std::wstring w = bslplat::utf8ToWide(texto);
    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();
    const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    bool ok = false;
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            std::memcpy(p, w.c_str(), bytes);
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok)
            GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

int interativo()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"BSL Stage - Gerar chave de ativação");
    DWORD modo = 0;
    const bool console = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &modo) != 0;
    std::printf("BSL Stage - gerar chave de ativação\n");
    std::printf("===================================\n\n");
    for (;;) {
        const std::string email = lerLinha("E-mail do cliente (deixe vazio e tecle Enter para sair): ");
        if (email.empty())
            return 0;
        if (email.find('@') == std::string::npos || email.find(' ') != std::string::npos) {
            std::printf("Esse e-mail não parece válido. Tente de novo.\n\n");
            continue;
        }
        const std::string nome = lerLinha("Nome do cliente (opcional): ");
        std::string chave;
        std::string anterior;
        if (criarChave(email, nome, chave, anterior) != 0) {
            lerLinha("\nTecle Enter para fechar.");
            return 1;
        }
        std::printf("\nChave de %s:\n\n%s\n\n", normalizarEmail(email).c_str(), chave.c_str());
        if (console && copiarParaAreaDeTransferencia(chave))
            std::printf("A chave já foi copiada. É só colar (Ctrl+V) na mensagem para o cliente.\n");
        if (anterior.empty())
            std::printf("Cliente registrado em: %s\n\n", arquivoClientes().string().c_str());
        else
            std::printf("Esse e-mail já estava registrado desde %s. A chave é a mesma de antes.\n\n", anterior.c_str());
    }
}

int verificar(const std::string& email, const std::string& chave)
{
    const bool ok = verificarChave(email, chave);
    std::printf(ok ? "VALIDA\n" : "INVALIDA\n");
    return ok ? 0 : 1;
}

int uso()
{
    std::printf("Uso:\n  bslstage_keygen init\n  bslstage_keygen gerar <email> [nome]\n  bslstage_keygen verificar <email> <chave>\n");
    return 2;
}

}

int main(int argc, char** argv)
{
    if (argc < 2)
        return interativo();
    const std::string cmd = argv[1];
    if (cmd == "init" && argc == 2)
        return iniciar();
    if (cmd == "gerar" && (argc == 3 || argc == 4))
        return gerar(argv[2], argc == 4 ? argv[3] : "");
    if (cmd == "verificar" && argc == 4)
        return verificar(argv[2], argv[3]);
    return uso();
}
