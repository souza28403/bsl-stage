#include "seq/EnvelopeStore.h"

#include "app/Paths.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace bsl::seq {

namespace {

using Json = nlohmann::json;

float numero(const Json& e, float padrao)
{
    if (!e.is_number())
        return padrao;
    const double v = e.get<double>();
    return std::isfinite(v) ? static_cast<float>(v) : padrao;
}

}

Json envelopeParaJson(const Envelope& e)
{
    Json j = Json::object();
    j["id"] = e.id;
    j["name"] = e.nome;
    Json pontos = Json::array();
    for (const Ancora& a : e.ancoras)
        pontos.push_back(Json::array({a.x, a.y, a.entradaX, a.entradaY, a.saidaX, a.saidaY,
                                      a.alcasLigadas ? 1 : 0, a.reto ? 1 : 0}));
    j["points"] = std::move(pontos);
    return j;
}

Envelope envelopeDeJson(const Json& j)
{
    Envelope e;
    if (j.is_object()) {
        if (const auto it = j.find("id"); it != j.end() && it->is_number_integer())
            e.id = std::max(it->get<int>(), 0);
        if (const auto it = j.find("name"); it != j.end() && it->is_string())
            e.nome = it->get<std::string>();
        if (const auto it = j.find("points"); it != j.end() && it->is_array()) {
            for (const Json& p : *it) {
                if (!p.is_array() || p.size() < 8 || static_cast<int>(e.ancoras.size()) >= kMaxAncoras)
                    continue;
                Ancora a;
                a.x = numero(p[0], 0.0f);
                a.y = numero(p[1], 0.0f);
                a.entradaX = numero(p[2], 0.0f);
                a.entradaY = numero(p[3], 0.0f);
                a.saidaX = numero(p[4], 0.0f);
                a.saidaY = numero(p[5], 0.0f);
                a.alcasLigadas = numero(p[6], 1.0f) != 0.0f;
                a.reto = numero(p[7], 0.0f) != 0.0f;
                e.ancoras.push_back(a);
            }
        }
    }
    normalizarEnvelope(e);
    return e;
}

Json bibliotecaParaJson(const BibliotecaEnvelopes& b)
{
    Json j = Json::object();
    j["nextId"] = b.proximoId;
    Json lista = Json::array();
    for (const Envelope& e : b.lista)
        lista.push_back(envelopeParaJson(e));
    j["envelopes"] = std::move(lista);
    return j;
}

BibliotecaEnvelopes bibliotecaDeJson(const Json& j)
{
    BibliotecaEnvelopes b;
    if (j.is_object()) {
        if (const auto it = j.find("nextId"); it != j.end() && it->is_number_integer())
            b.proximoId = std::max(it->get<int>(), 1);
        if (const auto it = j.find("envelopes"); it != j.end() && it->is_array()) {
            for (const Json& e : *it) {
                if (static_cast<int>(b.lista.size()) >= kMaxEnvelopes)
                    break;
                b.lista.push_back(envelopeDeJson(e));
            }
        }
    }
    garantirBiblioteca(b);
    return b;
}

std::filesystem::path caminhoBiblioteca()
{
    return paths::appDataDir() / L"envelopes.json";
}

bool salvarBiblioteca(const BibliotecaEnvelopes& b, const std::filesystem::path& arquivo)
{
    std::error_code ec;
    if (arquivo.has_parent_path())
        std::filesystem::create_directories(arquivo.parent_path(), ec);
    std::filesystem::path temporario = arquivo;
    temporario += L".tmp";
    {
        std::ofstream saida(temporario, std::ios::binary | std::ios::trunc);
        if (!saida)
            return false;
        saida << bibliotecaParaJson(b).dump(1, ' ', false, Json::error_handler_t::replace) << "\n";
        saida.flush();
        if (!saida) {
            saida.close();
            std::filesystem::remove(temporario, ec);
            return false;
        }
    }
    std::filesystem::rename(temporario, arquivo, ec);
    if (ec) {
        std::filesystem::remove(temporario, ec);
        return false;
    }
    return true;
}

BibliotecaEnvelopes carregarBiblioteca(const std::filesystem::path& arquivo)
{
    std::ifstream entrada(arquivo, std::ios::binary);
    if (!entrada) {
        BibliotecaEnvelopes b;
        garantirBiblioteca(b);
        return b;
    }
    const std::string texto((std::istreambuf_iterator<char>(entrada)), std::istreambuf_iterator<char>());
    return bibliotecaDeJson(Json::parse(texto, nullptr, false));
}

std::map<int, int> mesclarNaBiblioteca(BibliotecaEnvelopes& b, const std::vector<Envelope>& doProjeto)
{
    std::map<int, int> mapa;
    for (const Envelope& original : doProjeto) {
        Envelope e = original;
        normalizarEnvelope(e);
        const Envelope* existente = acharEnvelope(b, e.id);
        if (existente && existente->mesmoConteudo(e)) {
            mapa[original.id] = e.id;
            continue;
        }
        if (static_cast<int>(b.lista.size()) >= kMaxEnvelopes) {
            mapa[original.id] = b.lista.front().id;
            continue;
        }
        if (existente || e.id < 1)
            e.id = b.proximoId++;
        else
            b.proximoId = std::max(b.proximoId, e.id + 1);
        mapa[original.id] = e.id;
        b.lista.push_back(std::move(e));
    }
    return mapa;
}

}
