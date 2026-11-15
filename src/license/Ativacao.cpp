#include "license/Ativacao.h"

#include "license/Licenca.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace bsl::licenca {

namespace {

bool espaco(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string aparar(std::string_view texto)
{
    std::size_t ini = 0;
    std::size_t fim = texto.size();
    while (ini < fim && espaco(texto[ini]))
        ++ini;
    while (fim > ini && espaco(texto[fim - 1]))
        --fim;
    return std::string(texto.substr(ini, fim - ini));
}

std::string semEspacos(std::string_view texto)
{
    std::string saida;
    saida.reserve(texto.size());
    for (char c : texto) {
        if (!espaco(c))
            saida.push_back(c);
    }
    return saida;
}

}

bool lerAtivacao(const std::filesystem::path& arquivo, std::string& email)
{
    std::ifstream in(arquivo, std::ios::binary);
    if (!in)
        return false;
    std::string conteudo((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (conteudo.size() > 4096)
        return false;
    const std::size_t quebra = conteudo.find('\n');
    if (quebra == std::string::npos)
        return false;
    const std::string lido = aparar(std::string_view(conteudo).substr(0, quebra));
    const std::string chave = semEspacos(std::string_view(conteudo).substr(quebra + 1));
    if (lido.empty() || chave.empty() || !verificarChave(lido, chave))
        return false;
    email = lido;
    return true;
}

bool ativar(const std::filesystem::path& arquivo, std::string_view email, std::string_view chave)
{
    const std::string limpoEmail = aparar(email);
    const std::string limpaChave = semEspacos(chave);
    if (limpoEmail.empty() || limpaChave.empty() || !verificarChave(limpoEmail, limpaChave))
        return false;
    std::error_code ec;
    if (arquivo.has_parent_path())
        std::filesystem::create_directories(arquivo.parent_path(), ec);
    std::ofstream out(arquivo, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << limpoEmail << '\n' << limpaChave << '\n';
    out.flush();
    return static_cast<bool>(out);
}

}
