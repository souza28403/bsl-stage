#include "seq/SeqEdit.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <utility>

namespace bsl::seq {

namespace {

void aplicarNaLista(std::vector<int>& lista, const std::vector<int>& uids, ModoEdicao modo)
{
    normalizarPasso(lista);
    std::set<int> alvo;
    for (int u : uids)
        if (u > 0)
            alvo.insert(u);
    std::set<int> atual(lista.begin(), lista.end());
    for (int u : alvo) {
        switch (modo) {
        case ModoEdicao::Ligar:
            atual.insert(u);
            break;
        case ModoEdicao::Desligar:
            atual.erase(u);
            break;
        case ModoEdicao::Inverter:
            if (!atual.erase(u))
                atual.insert(u);
            break;
        }
    }
    lista.assign(atual.begin(), atual.end());
}

template <typename Lista>
bool moverEm(Lista& lista, int de, int para)
{
    const int n = static_cast<int>(lista.size());
    if (de < 0 || de >= n || para < 0 || para >= n)
        return false;
    if (de == para)
        return true;
    auto item = std::move(lista[static_cast<size_t>(de)]);
    lista.erase(lista.begin() + de);
    lista.insert(lista.begin() + para, std::move(item));
    return true;
}

}

bool passoContem(const Passo& passo, int uid)
{
    return std::binary_search(passo.begin(), passo.end(), uid);
}

void aplicarEdicao(Passo& passo, const std::vector<int>& uids, ModoEdicao modo)
{
    aplicarNaLista(passo, uids, modo);
}

void aplicarEdicaoVisao(Visao& visao, const std::vector<int>& uids, ModoEdicao modo)
{
    aplicarNaLista(visao.ligados, uids, modo);
}

bool passoValido(const Sequencia& sequencia, int indice)
{
    return indice >= 0 && indice < static_cast<int>(sequencia.passos.size());
}

bool copiarPasso(Sequencia& sequencia, int origem, int destino)
{
    if (!passoValido(sequencia, origem) || !passoValido(sequencia, destino))
        return false;
    if (origem != destino)
        sequencia.passos[static_cast<size_t>(destino)] = sequencia.passos[static_cast<size_t>(origem)];
    return true;
}

int copiarPassoACadaK(Sequencia& sequencia, int origem, int k)
{
    if (!passoValido(sequencia, origem) || k < 1)
        return 0;
    int copias = 0;
    for (int d = origem + k; d < static_cast<int>(sequencia.passos.size()); d += k) {
        sequencia.passos[static_cast<size_t>(d)] = sequencia.passos[static_cast<size_t>(origem)];
        ++copias;
    }
    return copias;
}

bool trocarPassos(Sequencia& sequencia, int a, int b)
{
    if (!passoValido(sequencia, a) || !passoValido(sequencia, b))
        return false;
    std::swap(sequencia.passos[static_cast<size_t>(a)], sequencia.passos[static_cast<size_t>(b)]);
    return true;
}

bool inserirPasso(Sequencia& sequencia, int posicao)
{
    if (static_cast<int>(sequencia.passos.size()) >= kMaxPassos)
        return false;
    posicao = std::clamp(posicao, 0, static_cast<int>(sequencia.passos.size()));
    sequencia.passos.insert(sequencia.passos.begin() + posicao, Passo{});
    return true;
}

bool removerPasso(Sequencia& sequencia, int indice)
{
    if (!passoValido(sequencia, indice) || sequencia.passos.size() <= 1)
        return false;
    sequencia.passos.erase(sequencia.passos.begin() + indice);
    return true;
}

bool limparPasso(Sequencia& sequencia, int indice)
{
    if (!passoValido(sequencia, indice))
        return false;
    sequencia.passos[static_cast<size_t>(indice)].clear();
    return true;
}

void limparSequencia(Sequencia& sequencia)
{
    for (Passo& p : sequencia.passos)
        p.clear();
}

int repetirParaFrente(Sequencia& sequencia, int indice)
{
    if (!passoValido(sequencia, indice))
        return 0;
    int copias = 0;
    for (int d = indice + 1; d < static_cast<int>(sequencia.passos.size()); ++d) {
        sequencia.passos[static_cast<size_t>(d)] = sequencia.passos[static_cast<size_t>(indice)];
        ++copias;
    }
    return copias;
}

bool vaiEVolta(Sequencia& sequencia)
{
    const int n = static_cast<int>(sequencia.passos.size());
    if (n < 3)
        return false;
    if (n + (n - 2) > kMaxPassos)
        return false;
    for (int i = n - 2; i >= 1; --i)
        sequencia.passos.push_back(sequencia.passos[static_cast<size_t>(i)]);
    return true;
}

bool moverSequencia(SequenceSet& conjunto, int de, int para)
{
    return moverEm(conjunto.sequencias, de, para);
}

bool moverVisao(SequenceSet& conjunto, int de, int para)
{
    return moverEm(conjunto.visoes, de, para);
}

bool renomearSequencia(SequenceSet& conjunto, int id, const std::string& nome)
{
    Sequencia* s = acharSequencia(conjunto, id);
    if (!s || nome.empty())
        return false;
    s->nome = nome;
    return true;
}

bool renomearVisao(SequenceSet& conjunto, int id, const std::string& nome)
{
    Visao* v = acharVisao(conjunto, id);
    if (!v || nome.empty())
        return false;
    v->nome = nome;
    return true;
}

bool passosDiferentes(const SequenceSet& conjunto)
{
    if (conjunto.sequencias.size() < 2)
        return false;
    const size_t n = conjunto.sequencias.front().passos.size();
    return std::any_of(std::next(conjunto.sequencias.begin()), conjunto.sequencias.end(),
                       [n](const Sequencia& s) { return s.passos.size() != n; });
}

}
