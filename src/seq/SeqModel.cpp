#include "seq/SeqModel.h"

#include "output/OutputSetup.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <utility>

namespace bsl::seq {

namespace {

constexpr int kMaxUid = 1 << 30;
constexpr size_t kMaxUidsPorPasso = 65536;

bool lerNumero(const std::string& texto, size_t& pos, int& valor)
{
    const char* ini = texto.data() + pos;
    const char* fim = texto.data() + texto.size();
    const auto r = std::from_chars(ini, fim, valor);
    if (r.ec != std::errc() || valor < 1 || valor > kMaxUid)
        return false;
    pos += static_cast<size_t>(r.ptr - ini);
    return true;
}

void pularEspacos(const std::string& texto, size_t& pos)
{
    while (pos < texto.size() && texto[pos] == ' ')
        ++pos;
}

template <typename T>
T* acharPorId(std::vector<T>& lista, int id)
{
    for (T& e : lista)
        if (e.id == id)
            return &e;
    return nullptr;
}

template <typename T>
const T* acharPorId(const std::vector<T>& lista, int id)
{
    for (const T& e : lista)
        if (e.id == id)
            return &e;
    return nullptr;
}

std::set<int> uidsUsados(const SequenceSet& conjunto)
{
    std::set<int> usados;
    for (const Sequencia& s : conjunto.sequencias)
        for (const Passo& p : s.passos)
            usados.insert(p.begin(), p.end());
    for (const Visao& v : conjunto.visoes)
        usados.insert(v.ligados.begin(), v.ligados.end());
    return usados;
}

std::set<int> uidsVivos(const OutputSetup& saida)
{
    std::set<int> vivos;
    for (const Screen& t : saida.screens)
        for (const Slice& sl : t.slices)
            if (sl.uid > 0)
                vivos.insert(sl.uid);
    return vivos;
}

}

void normalizarPasso(Passo& passo)
{
    passo.erase(std::remove_if(passo.begin(), passo.end(), [](int u) { return u < 1; }), passo.end());
    std::sort(passo.begin(), passo.end());
    passo.erase(std::unique(passo.begin(), passo.end()), passo.end());
}

std::string passoParaTexto(const Passo& passoEntrada)
{
    Passo passo = passoEntrada;
    normalizarPasso(passo);
    std::string texto;
    size_t i = 0;
    while (i < passo.size()) {
        size_t j = i;
        while (j + 1 < passo.size() && passo[j + 1] == passo[j] + 1)
            ++j;
        if (!texto.empty())
            texto += ',';
        texto += std::to_string(passo[i]);
        if (j == i + 1) {
            texto += ',';
            texto += std::to_string(passo[j]);
        } else if (j > i) {
            texto += '-';
            texto += std::to_string(passo[j]);
        }
        i = j + 1;
    }
    return texto;
}

bool textoParaPasso(const std::string& texto, Passo& passo)
{
    Passo lido;
    size_t pos = 0;
    pularEspacos(texto, pos);
    if (pos == texto.size()) {
        passo.clear();
        return true;
    }
    while (true) {
        pularEspacos(texto, pos);
        int a = 0;
        if (!lerNumero(texto, pos, a))
            return false;
        int b = a;
        pularEspacos(texto, pos);
        if (pos < texto.size() && texto[pos] == '-') {
            ++pos;
            pularEspacos(texto, pos);
            if (!lerNumero(texto, pos, b) || b < a)
                return false;
            pularEspacos(texto, pos);
        }
        if (lido.size() + static_cast<size_t>(b - a) + 1 > kMaxUidsPorPasso)
            return false;
        for (int u = a; u <= b; ++u)
            lido.push_back(u);
        if (pos == texto.size())
            break;
        if (texto[pos] != ',')
            return false;
        ++pos;
    }
    normalizarPasso(lido);
    passo = std::move(lido);
    return true;
}

int criarSequencia(SequenceSet& conjunto, const std::string& nome, int passos)
{
    if (static_cast<int>(conjunto.sequencias.size()) >= kMaxSequencias)
        return 0;
    Sequencia s;
    s.id = conjunto.proximoId++;
    s.nome = nome;
    s.passos.assign(static_cast<size_t>(std::clamp(passos, 1, kMaxPassos)), Passo{});
    conjunto.sequencias.push_back(std::move(s));
    return conjunto.sequencias.back().id;
}

int criarVisao(SequenceSet& conjunto, const std::string& nome)
{
    if (static_cast<int>(conjunto.visoes.size()) >= kMaxVisoes)
        return 0;
    Visao v;
    v.id = conjunto.proximoId++;
    v.nome = nome;
    conjunto.visoes.push_back(std::move(v));
    return conjunto.visoes.back().id;
}

Sequencia* acharSequencia(SequenceSet& conjunto, int id)
{
    return acharPorId(conjunto.sequencias, id);
}

const Sequencia* acharSequencia(const SequenceSet& conjunto, int id)
{
    return acharPorId(conjunto.sequencias, id);
}

Visao* acharVisao(SequenceSet& conjunto, int id)
{
    return acharPorId(conjunto.visoes, id);
}

const Visao* acharVisao(const SequenceSet& conjunto, int id)
{
    return acharPorId(conjunto.visoes, id);
}

bool excluirSequencia(SequenceSet& conjunto, int id)
{
    const auto it = std::find_if(conjunto.sequencias.begin(), conjunto.sequencias.end(),
                                 [id](const Sequencia& s) { return s.id == id; });
    if (it == conjunto.sequencias.end())
        return false;
    conjunto.sequencias.erase(it);
    return true;
}

bool excluirVisao(SequenceSet& conjunto, int id)
{
    if (id == kVisaoTudo)
        return false;
    const auto it = std::find_if(conjunto.visoes.begin(), conjunto.visoes.end(),
                                 [id](const Visao& v) { return v.id == id; });
    if (it == conjunto.visoes.end())
        return false;
    conjunto.visoes.erase(it);
    return true;
}

int duplicarSequencia(SequenceSet& conjunto, int id)
{
    const Sequencia* origem = acharSequencia(conjunto, id);
    if (!origem || static_cast<int>(conjunto.sequencias.size()) >= kMaxSequencias)
        return 0;
    Sequencia copia = *origem;
    copia.id = conjunto.proximoId++;
    copia.nome += " (cópia)";
    conjunto.sequencias.push_back(std::move(copia));
    return conjunto.sequencias.back().id;
}

int duplicarVisao(SequenceSet& conjunto, int id)
{
    const Visao* origem = acharVisao(conjunto, id);
    if (!origem || static_cast<int>(conjunto.visoes.size()) >= kMaxVisoes)
        return 0;
    Visao copia = *origem;
    copia.id = conjunto.proximoId++;
    copia.nome += " (cópia)";
    conjunto.visoes.push_back(std::move(copia));
    return conjunto.visoes.back().id;
}

bool visaoContem(const SequenceSet& conjunto, int idVisao, int uid)
{
    if (idVisao == kVisaoTudo)
        return true;
    const Visao* v = acharVisao(conjunto, idVisao);
    if (!v)
        return false;
    return std::binary_search(v->ligados.begin(), v->ligados.end(), uid);
}

void definirTamanho(Sequencia& sequencia, int passos)
{
    sequencia.passos.resize(static_cast<size_t>(std::clamp(passos, 1, kMaxPassos)));
}

void normalizar(SequenceSet& conjunto)
{
    std::set<int> usados;
    int maiorId = 0;
    auto registrar = [&](int& id) {
        if (id < 1 || usados.count(id)) {
            id = 0;
            return;
        }
        usados.insert(id);
        maiorId = std::max(maiorId, id);
    };
    for (Sequencia& s : conjunto.sequencias)
        registrar(s.id);
    for (Visao& v : conjunto.visoes)
        registrar(v.id);
    conjunto.proximoId = std::max({conjunto.proximoId, maiorId + 1, 1});
    for (Sequencia& s : conjunto.sequencias)
        if (s.id == 0)
            s.id = conjunto.proximoId++;
    for (Visao& v : conjunto.visoes)
        if (v.id == 0)
            v.id = conjunto.proximoId++;
    for (Sequencia& s : conjunto.sequencias) {
        if (s.passos.empty())
            s.passos.emplace_back();
        if (static_cast<int>(s.passos.size()) > kMaxPassos)
            s.passos.resize(static_cast<size_t>(kMaxPassos));
        for (Passo& p : s.passos)
            normalizarPasso(p);
    }
    for (Visao& v : conjunto.visoes)
        normalizarPasso(v.ligados);
}

void atualizarAncoras(SequenceSet& conjunto, const OutputSetup& saida)
{
    const std::set<int> usados = uidsUsados(conjunto);
    std::map<int, AncoraSlice> resultado;
    for (const AncoraSlice& a : conjunto.ancoras)
        if (usados.count(a.uid))
            resultado[a.uid] = a;
    for (const Screen& t : saida.screens) {
        int indice = 0;
        for (const Slice& sl : t.slices) {
            if (sl.uid > 0 && usados.count(sl.uid))
                resultado[sl.uid] = {sl.uid, t.name, sl.name, indice};
            ++indice;
        }
    }
    conjunto.ancoras.clear();
    for (auto& [uid, a] : resultado)
        conjunto.ancoras.push_back(std::move(a));
}

std::vector<int> uidsOrfaos(const SequenceSet& conjunto, const OutputSetup& saida)
{
    const std::set<int> vivos = uidsVivos(saida);
    std::vector<int> orfaos;
    for (int u : uidsUsados(conjunto))
        if (!vivos.count(u))
            orfaos.push_back(u);
    return orfaos;
}

int religarSlices(SequenceSet& conjunto, const OutputSetup& saida)
{
    const std::vector<int> orfaos = uidsOrfaos(conjunto, saida);
    if (orfaos.empty())
        return 0;
    const std::set<int> usados = uidsUsados(conjunto);
    std::set<int> reservados;
    std::map<int, AncoraSlice> porUid;
    for (const AncoraSlice& a : conjunto.ancoras)
        porUid[a.uid] = a;
    auto livre = [&](int uid) { return uid > 0 && !usados.count(uid) && !reservados.count(uid); };

    std::map<int, int> troca;
    for (int o : orfaos) {
        const auto it = porUid.find(o);
        if (it == porUid.end())
            continue;
        const AncoraSlice& a = it->second;
        int achado = 0;
        for (const Screen& t : saida.screens) {
            if (t.name != a.tela)
                continue;
            for (const Slice& sl : t.slices)
                if (!achado && sl.name == a.nome && livre(sl.uid))
                    achado = sl.uid;
            if (!achado && a.indice >= 0 && a.indice < static_cast<int>(t.slices.size())) {
                const int candidato = t.slices[static_cast<size_t>(a.indice)].uid;
                if (livre(candidato))
                    achado = candidato;
            }
            if (achado)
                break;
        }
        if (achado) {
            troca[o] = achado;
            reservados.insert(achado);
        }
    }
    if (troca.empty())
        return 0;
    auto aplicar = [&](Passo& p) {
        for (int& u : p) {
            const auto it = troca.find(u);
            if (it != troca.end())
                u = it->second;
        }
        normalizarPasso(p);
    };
    for (Sequencia& s : conjunto.sequencias)
        for (Passo& p : s.passos)
            aplicar(p);
    for (Visao& v : conjunto.visoes)
        aplicar(v.ligados);
    for (AncoraSlice& a : conjunto.ancoras) {
        const auto it = troca.find(a.uid);
        if (it != troca.end())
            a.uid = it->second;
    }
    atualizarAncoras(conjunto, saida);
    return static_cast<int>(troca.size());
}

}
