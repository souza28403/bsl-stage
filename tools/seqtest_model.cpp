#include "seqtest.h"

#include "output/OutputSetup.h"
#include "seq/SeqJson.h"
#include "seq/SeqModel.h"

namespace seqtest {

using namespace bsl;
using namespace bsl::seq;

namespace {

OutputSetup montarSaida()
{
    OutputSetup s;
    s.addScreen(1920, 1080);
    s.addScreen(1920, 1080);
    s.screens[0].name = "Palco";
    s.screens[1].name = "Fundo";
    s.screens[0].slices.clear();
    s.screens[1].slices.clear();
    for (int i = 0; i < 4; ++i) {
        s.screens[0].slices.push_back(makeFullSlice("S" + std::to_string(i), 1920, 1080, 1920, 1080));
        s.screens[1].slices.push_back(makeFullSlice("F" + std::to_string(i), 1920, 1080, 1920, 1080));
    }
    ensureSliceUids(s);
    return s;
}

}

void testarModelo()
{
    secao("modelo");

    Passo p;
    VERIFICAR(textoParaPasso("1-5,9,12-14", p));
    VERIFICAR((p == Passo{1, 2, 3, 4, 5, 9, 12, 13, 14}));
    VERIFICAR(passoParaTexto(p) == "1-5,9,12-14");
    VERIFICAR(passoParaTexto({3, 1, 2, 2}) == "1-3");
    VERIFICAR(passoParaTexto({4, 5}) == "4,5");
    VERIFICAR(passoParaTexto({}).empty());
    VERIFICAR(textoParaPasso("", p) && p.empty());
    VERIFICAR(textoParaPasso(" 7 , 2-3 ", p) && (p == Passo{2, 3, 7}));
    Passo intacto{9};
    VERIFICAR(!textoParaPasso("abc", intacto) && intacto == Passo{9});
    VERIFICAR(!textoParaPasso("5-2", intacto));
    VERIFICAR(!textoParaPasso("1,,2", intacto));
    VERIFICAR(!textoParaPasso("0", intacto));
    VERIFICAR(!textoParaPasso("1-", intacto));
    VERIFICAR(!textoParaPasso("1-999999999", intacto));

    SequenceSet c;
    VERIFICAR(c.vazio());
    const int s1 = criarSequencia(c, "Onda", 4);
    const int s2 = criarSequencia(c, "Pisca", 2000);
    const int v1 = criarVisao(c, "Palco");
    VERIFICAR(s1 == 1 && s2 == 2 && v1 == 3 && c.proximoId == 4);
    VERIFICAR(static_cast<int>(acharSequencia(c, s2)->passos.size()) == kMaxPassos);
    VERIFICAR(acharSequencia(c, 99) == nullptr && acharVisao(c, s1) == nullptr);
    acharSequencia(c, s1)->passos[0] = {1, 2, 3};
    acharSequencia(c, s1)->temGerador = true;
    acharSequencia(c, s1)->gerador.direcao = DirecaoGerador::Coluna;
    acharSequencia(c, s1)->gerador.semente = 77;
    acharSequencia(c, s1)->gerador.bordasManuais = true;
    acharSequencia(c, s1)->gerador.bordasX = {0.25f, 0.5f};
    acharVisao(c, v1)->ligados = {2, 3};
    acharVisao(c, v1)->previaVisivel = false;
    VERIFICAR(visaoContem(c, kVisaoTudo, 12345));
    VERIFICAR(visaoContem(c, v1, 2) && !visaoContem(c, v1, 4) && !visaoContem(c, 99, 2));
    VERIFICAR(!excluirVisao(c, kVisaoTudo));

    const nlohmann::json j = conjuntoParaJson(c);
    std::vector<std::string> avisos;
    const SequenceSet lido = conjuntoDeJson(j, &avisos);
    VERIFICAR(avisos.empty());
    VERIFICAR(lido == c);
    VERIFICAR(conjuntoParaJson(lido) == j);
    VERIFICAR(j["list"][0]["steps"][0] == "1-3");

    const int copia = duplicarSequencia(c, s1);
    VERIFICAR(copia == 4 && acharSequencia(c, copia)->passos == acharSequencia(c, s1)->passos);
    const int copiaV = duplicarVisao(c, v1);
    VERIFICAR(copiaV == 5 && acharVisao(c, copiaV)->ligados == acharVisao(c, v1)->ligados);
    VERIFICAR(duplicarSequencia(c, 99) == 0);
    VERIFICAR(excluirSequencia(c, copia) && !excluirSequencia(c, copia));
    VERIFICAR(excluirVisao(c, copiaV) && !excluirVisao(c, copiaV));

    definirTamanho(*acharSequencia(c, s1), 0);
    VERIFICAR(acharSequencia(c, s1)->passos.size() == 1);
    definirTamanho(*acharSequencia(c, s1), 6);
    VERIFICAR(acharSequencia(c, s1)->passos.size() == 6);

    SequenceSet vazio = conjuntoDeJson(nlohmann::json::object(), nullptr);
    VERIFICAR(vazio.vazio() && vazio.proximoId == 1);
    VERIFICAR(conjuntoDeJson(nlohmann::json("texto"), nullptr).vazio());

    nlohmann::json sujo = {{"nextId", 2},
                           {"list", nlohmann::json::array({{{"id", 5}, {"name", "A"}, {"steps", nlohmann::json::array()}},
                                                           {{"id", 5}, {"name", "B"}, {"steps", {"1,x"}}},
                                                           {{"id", -3}, {"name", "C"}}})},
                           {"views", nlohmann::json::array({{{"id", 5}, {"name", "V"}, {"on", "9-7"}}})}};
    std::vector<std::string> av2;
    SequenceSet s = conjuntoDeJson(sujo, &av2);
    VERIFICAR(s.sequencias.size() == 3 && s.visoes.size() == 1);
    VERIFICAR(av2.size() == 2);
    VERIFICAR(s.sequencias[0].id == 5 && s.sequencias[1].id != 5 && s.sequencias[2].id > 0);
    VERIFICAR(s.visoes[0].id != 5 && s.visoes[0].ligados.empty());
    VERIFICAR(s.proximoId > 5);
    for (const Sequencia& q : s.sequencias)
        VERIFICAR(!q.passos.empty());
    int ids = 0;
    for (const Sequencia& q : s.sequencias)
        ids += q.id;
    for (const Visao& q : s.visoes)
        ids += q.id;
    VERIFICAR(ids > 0);

    nlohmann::json enorme = {{"list", nlohmann::json::array()}};
    for (int i = 0; i < kMaxSequencias + 10; ++i)
        enorme["list"].push_back({{"name", "x"}});
    VERIFICAR(static_cast<int>(conjuntoDeJson(enorme, nullptr).sequencias.size()) == kMaxSequencias);

    OutputSetup saida = montarSaida();
    SequenceSet r;
    const int sr = criarSequencia(r, "R", 2);
    const int vr = criarVisao(r, "V");
    const int uidA = saida.screens[0].slices[1].uid;
    const int uidB = saida.screens[1].slices[2].uid;
    acharSequencia(r, sr)->passos[0] = {uidA, uidB};
    acharVisao(r, vr)->ligados = {uidA};
    atualizarAncoras(r, saida);
    VERIFICAR(r.ancoras.size() == 2);
    VERIFICAR(uidsOrfaos(r, saida).empty());
    VERIFICAR(religarSlices(r, saida) == 0);

    saida.screens[0].slices.erase(saida.screens[0].slices.begin() + 1);
    saida.screens[0].slices.push_back(makeFullSlice("S1", 1920, 1080, 1920, 1080));
    saida.screens[1].slices.erase(saida.screens[1].slices.begin() + 2);
    saida.screens[1].slices.insert(saida.screens[1].slices.begin() + 2,
                                   makeFullSlice("Outro", 1920, 1080, 1920, 1080));
    ensureSliceUids(saida);
    VERIFICAR(uidsOrfaos(r, saida).size() == 2);
    const int novoA = saida.screens[0].slices.back().uid;
    const int novoB = saida.screens[1].slices[2].uid;
    VERIFICAR(religarSlices(r, saida) == 2);
    VERIFICAR((acharSequencia(r, sr)->passos[0] == Passo{std::min(novoA, novoB), std::max(novoA, novoB)}));
    VERIFICAR(acharVisao(r, vr)->ligados == Passo{novoA});
    VERIFICAR(uidsOrfaos(r, saida).empty());
    VERIFICAR(r.ancoras.size() == 2);
}

}
