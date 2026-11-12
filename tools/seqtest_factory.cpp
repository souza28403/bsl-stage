#include "seqtest.h"

#include "output/OutputSetup.h"
#include "seq/SeqFactory.h"

#include <algorithm>
#include <set>

namespace seqtest {

using namespace bsl;
using namespace bsl::seq;

namespace {

std::vector<GeoSlice> grade4x3(float passo = 100.0f, float deslocamentoX = 0.0f)
{
    std::vector<GeoSlice> v;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) {
            GeoSlice g;
            g.uid = r * 4 + c + 1;
            g.cx = 50.0f + passo * static_cast<float>(c) + deslocamentoX * static_cast<float>(r);
            g.cy = 50.0f + passo * static_cast<float>(r);
            g.largura = 100.0f;
            g.altura = 100.0f;
            v.push_back(g);
        }
    return v;
}

std::set<int> uniao(const std::vector<Passo>& passos)
{
    std::set<int> u;
    for (const Passo& p : passos)
        u.insert(p.begin(), p.end());
    return u;
}

bool disjuntos(const std::vector<Passo>& passos)
{
    std::set<int> vistos;
    for (const Passo& p : passos)
        for (int u : p)
            if (!vistos.insert(u).second)
                return false;
    return true;
}

}

void testarFabrica()
{
    secao("fabrica");

    const std::vector<GeoSlice> geo = grade4x3();
    Grade grade;
    ParametrosGerador p;
    std::vector<Passo> horizontal = gerarPassos(geo, p, &grade);
    VERIFICAR(grade.colunas() == 4 && grade.linhas() == 3);
    VERIFICAR(horizontal.size() == 4);
    VERIFICAR((horizontal[0] == Passo{1, 5, 9}) && (horizontal[3] == Passo{4, 8, 12}));

    p.inverterX = true;
    horizontal = gerarPassos(geo, p);
    VERIFICAR((horizontal[0] == Passo{4, 8, 12}) && (horizontal[3] == Passo{1, 5, 9}));
    p.inverterX = false;

    p.direcao = DirecaoGerador::Vertical;
    std::vector<Passo> vertical = gerarPassos(geo, p);
    VERIFICAR(vertical.size() == 3 && (vertical[0] == Passo{1, 2, 3, 4}) && (vertical[2] == Passo{9, 10, 11, 12}));
    p.inverterY = true;
    vertical = gerarPassos(geo, p);
    VERIFICAR((vertical[0] == Passo{9, 10, 11, 12}));
    p.inverterY = false;

    p.direcao = DirecaoGerador::Linha;
    std::vector<Passo> linha = gerarPassos(geo, p);
    VERIFICAR(linha.size() == 12 && (linha[0] == Passo{1}) && (linha[4] == Passo{5}) && (linha[11] == Passo{12}));
    p.inverterX = true;
    p.inverterY = true;
    linha = gerarPassos(geo, p);
    VERIFICAR((linha[0] == Passo{12}) && (linha[11] == Passo{1}));
    p.inverterX = p.inverterY = false;

    p.direcao = DirecaoGerador::Coluna;
    std::vector<Passo> coluna = gerarPassos(geo, p);
    VERIFICAR(coluna.size() == 12 && (coluna[0] == Passo{1}) && (coluna[1] == Passo{5}) && (coluna[3] == Passo{2}));

    p.direcao = DirecaoGerador::Horizontal;
    p.bloco = 2;
    std::vector<Passo> bloco = gerarPassos(geo, p);
    VERIFICAR(bloco.size() == 2 && (bloco[0] == Passo{1, 2, 5, 6, 9, 10}));
    p.bloco = 3;
    bloco = gerarPassos(geo, p);
    VERIFICAR(bloco.size() == 2 && (bloco[1] == Passo{4, 8, 12}));
    p.bloco = 1;

    p.direcao = DirecaoGerador::Linha;
    p.modulo = 3;
    std::vector<Passo> modulo = gerarPassos(geo, p);
    VERIFICAR(modulo.size() == 3 && (modulo[0] == Passo{1, 4, 7, 10}) && uniao(modulo).size() == 12);
    p.modulo = 20;
    modulo = gerarPassos(geo, p);
    VERIFICAR(modulo.size() == 20 && modulo[12].empty() && uniao(modulo).size() == 12);
    p.modulo = 0;

    p.direcao = DirecaoGerador::Horizontal;
    p.espelhoX = true;
    std::vector<Passo> espelho = gerarPassos(geo, p);
    VERIFICAR(espelho.size() == 2 && (espelho[0] == Passo{1, 4, 5, 8, 9, 12}) && (espelho[1] == Passo{2, 3, 6, 7, 10, 11}));
    p.espelhoY = true;
    p.direcao = DirecaoGerador::Linha;
    espelho = gerarPassos(geo, p);
    VERIFICAR(espelho.size() == 4 && (espelho[0] == Passo{1, 4, 9, 12}) && uniao(espelho).size() == 12);
    p.espelhoX = p.espelhoY = false;

    p.direcao = DirecaoGerador::Aleatorio;
    p.semente = 7;
    const std::vector<Passo> a1 = gerarPassos(geo, p);
    const std::vector<Passo> a2 = gerarPassos(geo, p);
    VERIFICAR(a1 == a2);
    VERIFICAR(a1.size() == 12 && disjuntos(a1) && uniao(a1).size() == 12);
    bool unicos = true;
    for (const Passo& passo : a1)
        unicos = unicos && passo.size() == 1;
    VERIFICAR(unicos);
    bool algumaDiferente = false;
    for (int semente = 8; semente < 20; ++semente) {
        p.semente = semente;
        const std::vector<Passo> outra = gerarPassos(geo, p);
        VERIFICAR(disjuntos(outra) && uniao(outra).size() == 12);
        algumaDiferente = algumaDiferente || outra != a1;
    }
    VERIFICAR(algumaDiferente);

    ParametrosGerador e;
    e.embaralhar = true;
    e.semente = 3;
    const std::vector<Passo> e1 = gerarPassos(geo, e);
    VERIFICAR(e1.size() == 4 && uniao(e1).size() == 12 && gerarPassos(geo, e) == e1);

    const DirecaoGerador direcoes[] = {DirecaoGerador::Horizontal, DirecaoGerador::Vertical, DirecaoGerador::Linha,
                                       DirecaoGerador::Coluna, DirecaoGerador::Aleatorio};
    int combinacoes = 0;
    bool todasCompletas = true;
    bool todasDentroDoTeto = true;
    for (DirecaoGerador d : direcoes)
        for (int mascara = 0; mascara < 64; ++mascara)
            for (int bl : {1, 2, 5})
                for (int mod : {0, 3, 7}) {
                    ParametrosGerador q;
                    q.direcao = d;
                    q.inverterX = mascara & 1;
                    q.inverterY = mascara & 2;
                    q.espelhoX = mascara & 4;
                    q.espelhoY = mascara & 8;
                    q.embaralhar = mascara & 16;
                    q.bloco = bl;
                    q.modulo = mod;
                    q.semente = (mascara & 32) ? 99 : 1;
                    const std::vector<Passo> r = gerarPassos(geo, q);
                    ++combinacoes;
                    todasCompletas = todasCompletas && uniao(r).size() == 12 && !r.empty();
                    todasDentroDoTeto = todasDentroDoTeto && static_cast<int>(r.size()) <= kMaxPassos;
                    if (mod > 0)
                        todasCompletas = todasCompletas && static_cast<int>(r.size()) == mod;
                    for (const Passo& passo : r)
                        todasCompletas = todasCompletas && std::is_sorted(passo.begin(), passo.end());
                }
    VERIFICAR(combinacoes == 5 * 64 * 3 * 3);
    VERIFICAR(todasCompletas && todasDentroDoTeto);

    const std::vector<GeoSlice> torta = grade4x3(100.0f, 20.0f);
    ParametrosGerador t;
    Grade gradeTorta;
    const std::vector<Passo> tp = gerarPassos(torta, t, &gradeTorta);
    VERIFICAR(gradeTorta.colunas() == 4 && gradeTorta.linhas() == 3 && uniao(tp).size() == 12);

    std::vector<GeoSlice> lacuna = geo;
    for (GeoSlice& g : lacuna)
        if (g.cx > 250.0f)
            g.cx += 300.0f;
    ParametrosGerador m;
    Grade gradeLacuna;
    gerarPassos(lacuna, m, &gradeLacuna);
    VERIFICAR(gradeLacuna.colunas() == 4);
    ParametrosGerador manual;
    manual.bordasManuais = true;
    manual.bordasX = {200.0f};
    manual.bordasY = {};
    const std::vector<Passo> manuais = gerarPassos(geo, manual);
    VERIFICAR(manuais.size() == 2 && (manuais[0] == Passo{1, 2, 5, 6, 9, 10}));
    manual.bordasX = {300.0f, 100.0f, 100.0f};
    VERIFICAR(gerarPassos(geo, manual).size() == 3);

    VERIFICAR(gerarPassos({}, ParametrosGerador{}).size() == 1);
    VERIFICAR(gerarPassos({}, ParametrosGerador{})[0].empty());
    std::vector<GeoSlice> unico{geo[0]};
    VERIFICAR(gerarPassos(unico, ParametrosGerador{}).size() == 1);

    std::vector<GeoSlice> enorme;
    for (int i = 0; i < 2000; ++i) {
        GeoSlice g;
        g.uid = i + 1;
        g.cx = 50.0f + 100.0f * static_cast<float>(i);
        g.cy = 50.0f;
        g.largura = g.altura = 100.0f;
        enorme.push_back(g);
    }
    const std::vector<Passo> grandes = gerarPassos(enorme, ParametrosGerador{});
    VERIFICAR(static_cast<int>(grandes.size()) == kMaxPassos && uniao(grandes).size() == 2000);

    Sequencia s;
    s.nome = "Gerada";
    s.temGerador = true;
    s.gerador.direcao = DirecaoGerador::Vertical;
    aplicarGerador(s, geo);
    VERIFICAR(s.passos.size() == 3);
    Sequencia sem;
    sem.passos.resize(5);
    aplicarGerador(sem, geo);
    VERIFICAR(sem.passos.size() == 5);

    OutputSetup saida;
    saida.addScreen(1920, 1080);
    saida.screens[0].slices.clear();
    Slice a = makeFullSlice("A", 1920, 1080, 1920, 1080);
    a.input = {0, 0, 960, 1080};
    Slice b = makeFullSlice("B", 1920, 1080, 1920, 1080);
    b.input = {960, 0, 960, 1080};
    saida.screens[0].slices = {a, b};
    ensureSliceUids(saida);
    const std::vector<GeoSlice> real = coletarGeometria(saida);
    VERIFICAR(real.size() == 2 && real[0].cx == 480.0f && real[1].cx == 1440.0f && real[0].cy == 540.0f);
    VERIFICAR(gerarPassos(real, ParametrosGerador{}).size() == 2);
}

}
