#include "seq/SeqJson.h"

#include <algorithm>
#include <cmath>

namespace bsl::seq {

namespace {

using Json = nlohmann::json;

constexpr int kMaxBordas = 4096;
constexpr const char* kNomesDirecao[] = {"horizontal", "vertical", "linha", "coluna", "aleatorio"};

int inteiro(const Json& j, const char* chave, int padrao, int minimo, int maximo)
{
    const auto it = j.find(chave);
    if (it == j.end() || !it->is_number())
        return padrao;
    const double v = it->get<double>();
    if (!std::isfinite(v))
        return padrao;
    return static_cast<int>(std::clamp(v, static_cast<double>(minimo), static_cast<double>(maximo)));
}

float real(const Json& j, const char* chave, float padrao, float minimo, float maximo)
{
    const auto it = j.find(chave);
    if (it == j.end() || !it->is_number())
        return padrao;
    const double v = it->get<double>();
    if (!std::isfinite(v))
        return padrao;
    return static_cast<float>(std::clamp(v, static_cast<double>(minimo), static_cast<double>(maximo)));
}

bool logico(const Json& j, const char* chave, bool padrao)
{
    const auto it = j.find(chave);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : padrao;
}

std::string texto(const Json& j, const char* chave)
{
    const auto it = j.find(chave);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

Json bordasParaJson(const std::vector<float>& bordas)
{
    Json a = Json::array();
    for (float b : bordas)
        a.push_back(b);
    return a;
}

std::vector<float> bordasDeJson(const Json& j, const char* chave)
{
    std::vector<float> bordas;
    const auto it = j.find(chave);
    if (it == j.end() || !it->is_array())
        return bordas;
    for (const Json& e : *it) {
        if (!e.is_number() || static_cast<int>(bordas.size()) >= kMaxBordas)
            continue;
        const double v = e.get<double>();
        if (std::isfinite(v))
            bordas.push_back(static_cast<float>(v));
    }
    return bordas;
}

Json geradorParaJson(const ParametrosGerador& g)
{
    Json j = Json::object();
    j["direcao"] = kNomesDirecao[static_cast<int>(g.direcao)];
    j["inverterX"] = g.inverterX;
    j["inverterY"] = g.inverterY;
    j["bloco"] = g.bloco;
    j["modulo"] = g.modulo;
    j["espelhoX"] = g.espelhoX;
    j["espelhoY"] = g.espelhoY;
    j["embaralhar"] = g.embaralhar;
    j["semente"] = g.semente;
    j["limiar"] = g.limiar;
    j["bordasManuais"] = g.bordasManuais;
    if (g.bordasManuais) {
        j["bordasX"] = bordasParaJson(g.bordasX);
        j["bordasY"] = bordasParaJson(g.bordasY);
    }
    return j;
}

ParametrosGerador geradorDeJson(const Json& j)
{
    ParametrosGerador g;
    const std::string d = texto(j, "direcao");
    for (int i = 0; i < 5; ++i)
        if (d == kNomesDirecao[i])
            g.direcao = static_cast<DirecaoGerador>(i);
    g.inverterX = logico(j, "inverterX", false);
    g.inverterY = logico(j, "inverterY", false);
    g.bloco = inteiro(j, "bloco", 1, 1, kMaxPassos);
    g.modulo = inteiro(j, "modulo", 0, 0, kMaxPassos);
    g.espelhoX = logico(j, "espelhoX", false);
    g.espelhoY = logico(j, "espelhoY", false);
    g.embaralhar = logico(j, "embaralhar", false);
    g.semente = inteiro(j, "semente", 1, 0, 1 << 30);
    g.limiar = real(j, "limiar", 0.5f, 0.0f, 1.0f);
    g.bordasManuais = logico(j, "bordasManuais", false);
    g.bordasX = bordasDeJson(j, "bordasX");
    g.bordasY = bordasDeJson(j, "bordasY");
    return g;
}

}

Json conjuntoParaJson(const SequenceSet& conjunto)
{
    Json raiz = Json::object();
    raiz["nextId"] = conjunto.proximoId;
    Json seqs = Json::array();
    for (const Sequencia& s : conjunto.sequencias) {
        Json sj = Json::object();
        sj["id"] = s.id;
        sj["name"] = s.nome;
        Json passos = Json::array();
        for (const Passo& p : s.passos)
            passos.push_back(passoParaTexto(p));
        sj["steps"] = std::move(passos);
        if (s.temGerador)
            sj["gen"] = geradorParaJson(s.gerador);
        seqs.push_back(std::move(sj));
    }
    raiz["list"] = std::move(seqs);
    Json visoes = Json::array();
    for (const Visao& v : conjunto.visoes) {
        Json vj = Json::object();
        vj["id"] = v.id;
        vj["name"] = v.nome;
        vj["previewVisible"] = v.previaVisivel;
        vj["on"] = passoParaTexto(v.ligados);
        visoes.push_back(std::move(vj));
    }
    raiz["views"] = std::move(visoes);
    if (!conjunto.ancoras.empty()) {
        Json ancoras = Json::array();
        for (const AncoraSlice& a : conjunto.ancoras)
            ancoras.push_back(Json::array({a.uid, a.tela, a.nome, a.indice}));
        raiz["anchors"] = std::move(ancoras);
    }
    return raiz;
}

SequenceSet conjuntoDeJson(const Json& j, std::vector<std::string>* avisos)
{
    SequenceSet c;
    if (!j.is_object())
        return c;
    auto avisar = [&](const std::string& m) {
        if (avisos)
            avisos->push_back(m);
    };
    c.proximoId = inteiro(j, "nextId", 1, 1, 1 << 30);
    if (const auto it = j.find("list"); it != j.end() && it->is_array()) {
        for (const Json& sj : *it) {
            if (!sj.is_object())
                continue;
            if (static_cast<int>(c.sequencias.size()) >= kMaxSequencias) {
                avisar("Sequências em excesso foram ignoradas");
                break;
            }
            Sequencia s;
            s.id = inteiro(sj, "id", 0, 0, 1 << 30);
            s.nome = texto(sj, "name");
            if (const auto ps = sj.find("steps"); ps != sj.end() && ps->is_array()) {
                for (const Json& pj : *ps) {
                    if (static_cast<int>(s.passos.size()) >= kMaxPassos)
                        break;
                    Passo p;
                    if (pj.is_string() && !textoParaPasso(pj.get<std::string>(), p)) {
                        avisar("Passo inválido em \"" + s.nome + "\"");
                        p.clear();
                    }
                    s.passos.push_back(std::move(p));
                }
            }
            if (const auto g = sj.find("gen"); g != sj.end() && g->is_object()) {
                s.temGerador = true;
                s.gerador = geradorDeJson(*g);
            }
            c.sequencias.push_back(std::move(s));
        }
    }
    if (const auto it = j.find("views"); it != j.end() && it->is_array()) {
        for (const Json& vj : *it) {
            if (!vj.is_object())
                continue;
            if (static_cast<int>(c.visoes.size()) >= kMaxVisoes) {
                avisar("Visões em excesso foram ignoradas");
                break;
            }
            Visao v;
            v.id = inteiro(vj, "id", 0, 0, 1 << 30);
            v.nome = texto(vj, "name");
            v.previaVisivel = logico(vj, "previewVisible", true);
            if (!textoParaPasso(texto(vj, "on"), v.ligados)) {
                avisar("Slices inválidos na visão \"" + v.nome + "\"");
                v.ligados.clear();
            }
            c.visoes.push_back(std::move(v));
        }
    }
    if (const auto it = j.find("anchors"); it != j.end() && it->is_array()) {
        for (const Json& aj : *it) {
            if (!aj.is_array() || aj.size() != 4 || !aj[0].is_number_integer() || !aj[1].is_string()
                || !aj[2].is_string() || !aj[3].is_number_integer())
                continue;
            AncoraSlice a;
            a.uid = std::clamp(aj[0].get<int>(), 0, 1 << 30);
            a.tela = aj[1].get<std::string>();
            a.nome = aj[2].get<std::string>();
            a.indice = std::max(aj[3].get<int>(), 0);
            if (a.uid > 0)
                c.ancoras.push_back(std::move(a));
        }
    }
    normalizar(c);
    return c;
}

}
