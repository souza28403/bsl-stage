#include "seq/SeqFactory.h"

#include "output/OutputSetup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <set>

namespace bsl::seq {

namespace {

struct Celula
{
    int coluna;
    int linha;
};

using Unidade = std::vector<Celula>;

float mediana(std::vector<float> valores)
{
    if (valores.empty())
        return 0.0f;
    std::sort(valores.begin(), valores.end());
    return valores[valores.size() / 2];
}

std::vector<float> agrupar(std::vector<float> centros, float tolerancia)
{
    std::sort(centros.begin(), centros.end());
    std::vector<float> bordas;
    for (size_t i = 1; i < centros.size(); ++i)
        if (centros[i] - centros[i - 1] > tolerancia)
            bordas.push_back(0.5f * (centros[i] + centros[i - 1]));
    return bordas;
}

std::vector<float> bordasOrdenadas(std::vector<float> bordas)
{
    bordas.erase(std::remove_if(bordas.begin(), bordas.end(), [](float b) { return !std::isfinite(b); }), bordas.end());
    std::sort(bordas.begin(), bordas.end());
    bordas.erase(std::unique(bordas.begin(), bordas.end()), bordas.end());
    return bordas;
}

void embaralharUnidades(std::vector<Unidade>& unidades, int semente)
{
    std::mt19937 gerador(static_cast<std::uint32_t>(semente));
    for (size_t i = unidades.size(); i > 1; --i) {
        const size_t j = gerador() % i;
        std::swap(unidades[i - 1], unidades[j]);
    }
}

std::uint64_t chaveCelulas(const Unidade& unidade)
{
    std::vector<std::uint64_t> chaves;
    for (const Celula& c : unidade)
        chaves.push_back((static_cast<std::uint64_t>(c.linha) << 32) | static_cast<std::uint32_t>(c.coluna));
    std::sort(chaves.begin(), chaves.end());
    std::uint64_t h = 1469598103934665603ull;
    for (std::uint64_t k : chaves) {
        h ^= k;
        h *= 1099511628211ull;
    }
    return h;
}

Unidade conjuntoCelulas(const Unidade& unidade)
{
    Unidade copia = unidade;
    std::sort(copia.begin(), copia.end(), [](const Celula& a, const Celula& b) {
        return a.linha != b.linha ? a.linha < b.linha : a.coluna < b.coluna;
    });
    copia.erase(std::unique(copia.begin(), copia.end(),
                            [](const Celula& a, const Celula& b) { return a.linha == b.linha && a.coluna == b.coluna; }),
                copia.end());
    return copia;
}

}

int Grade::colunaDe(float cx) const
{
    return static_cast<int>(std::upper_bound(bordasX.begin(), bordasX.end(), cx) - bordasX.begin());
}

int Grade::linhaDe(float cy) const
{
    return static_cast<int>(std::upper_bound(bordasY.begin(), bordasY.end(), cy) - bordasY.begin());
}

std::vector<GeoSlice> coletarGeometria(const OutputSetup& saida)
{
    std::vector<GeoSlice> geometria;
    for (const Screen& tela : saida.screens)
        for (const Slice& s : tela.slices) {
            if (s.uid <= 0)
                continue;
            GeoSlice g;
            g.uid = s.uid;
            g.largura = static_cast<float>(s.input.w);
            g.altura = static_cast<float>(s.input.h);
            g.cx = static_cast<float>(s.input.x) + 0.5f * g.largura;
            g.cy = static_cast<float>(s.input.y) + 0.5f * g.altura;
            geometria.push_back(g);
        }
    return geometria;
}

Grade detectarGrade(const std::vector<GeoSlice>& slices, float limiar)
{
    Grade grade;
    if (slices.empty())
        return grade;
    std::vector<float> xs, ys, ws, hs;
    for (const GeoSlice& g : slices) {
        xs.push_back(g.cx);
        ys.push_back(g.cy);
        ws.push_back(g.largura);
        hs.push_back(g.altura);
    }
    const float l = std::clamp(std::isfinite(limiar) ? limiar : 0.5f, 0.01f, 4.0f);
    grade.bordasX = agrupar(xs, l * std::max(mediana(ws), 1.0f));
    grade.bordasY = agrupar(ys, l * std::max(mediana(hs), 1.0f));
    return grade;
}

Grade gradeDoGerador(const std::vector<GeoSlice>& slices, const ParametrosGerador& p)
{
    if (!p.bordasManuais)
        return detectarGrade(slices, p.limiar);
    Grade grade;
    grade.bordasX = bordasOrdenadas(p.bordasX);
    grade.bordasY = bordasOrdenadas(p.bordasY);
    return grade;
}

std::vector<Passo> gerarPassos(const std::vector<GeoSlice>& slices, const ParametrosGerador& p, Grade* saidaGrade)
{
    const Grade grade = gradeDoGerador(slices, p);
    if (saidaGrade)
        *saidaGrade = grade;
    const int colunas = grade.colunas();
    const int linhas = grade.linhas();
    std::vector<std::vector<std::vector<int>>> celulas(static_cast<size_t>(linhas),
                                                       std::vector<std::vector<int>>(static_cast<size_t>(colunas)));
    for (const GeoSlice& g : slices)
        celulas[static_cast<size_t>(grade.linhaDe(g.cy))][static_cast<size_t>(grade.colunaDe(g.cx))].push_back(g.uid);
    auto ocupada = [&](int c, int l) { return !celulas[static_cast<size_t>(l)][static_cast<size_t>(c)].empty(); };

    std::vector<int> ordemColunas, ordemLinhas;
    for (int c = 0; c < colunas; ++c)
        ordemColunas.push_back(p.inverterX ? colunas - 1 - c : c);
    for (int l = 0; l < linhas; ++l)
        ordemLinhas.push_back(p.inverterY ? linhas - 1 - l : l);

    std::vector<Unidade> unidades;
    switch (p.direcao) {
    case DirecaoGerador::Horizontal:
        for (int c : ordemColunas) {
            Unidade u;
            for (int l = 0; l < linhas; ++l)
                if (ocupada(c, l))
                    u.push_back({c, l});
            if (!u.empty())
                unidades.push_back(std::move(u));
        }
        break;
    case DirecaoGerador::Vertical:
        for (int l : ordemLinhas) {
            Unidade u;
            for (int c = 0; c < colunas; ++c)
                if (ocupada(c, l))
                    u.push_back({c, l});
            if (!u.empty())
                unidades.push_back(std::move(u));
        }
        break;
    case DirecaoGerador::Linha:
    case DirecaoGerador::Aleatorio:
        for (int l : ordemLinhas)
            for (int c : ordemColunas)
                if (ocupada(c, l))
                    unidades.push_back({{c, l}});
        break;
    case DirecaoGerador::Coluna:
        for (int c : ordemColunas)
            for (int l : ordemLinhas)
                if (ocupada(c, l))
                    unidades.push_back({{c, l}});
        break;
    }

    if (p.direcao == DirecaoGerador::Aleatorio)
        embaralharUnidades(unidades, p.semente);

    if (p.espelhoX || p.espelhoY) {
        std::vector<Unidade> espelhadas;
        std::set<std::uint64_t> vistas;
        for (const Unidade& u : unidades) {
            Unidade nova;
            for (const Celula& c : u) {
                nova.push_back(c);
                const int mc = p.espelhoX ? colunas - 1 - c.coluna : c.coluna;
                const int ml = p.espelhoY ? linhas - 1 - c.linha : c.linha;
                if (p.espelhoX)
                    nova.push_back({mc, c.linha});
                if (p.espelhoY)
                    nova.push_back({c.coluna, ml});
                if (p.espelhoX && p.espelhoY)
                    nova.push_back({mc, ml});
            }
            nova = conjuntoCelulas(nova);
            if (vistas.insert(chaveCelulas(nova)).second)
                espelhadas.push_back(std::move(nova));
        }
        unidades = std::move(espelhadas);
    }

    if (p.embaralhar)
        embaralharUnidades(unidades, p.semente);

    const size_t bloco = static_cast<size_t>(std::clamp(p.bloco, 1, kMaxPassos));
    std::vector<Unidade> blocos;
    for (size_t i = 0; i < unidades.size(); ++i) {
        if (i % bloco == 0)
            blocos.emplace_back();
        blocos.back().insert(blocos.back().end(), unidades[i].begin(), unidades[i].end());
    }

    size_t total = blocos.size();
    if (p.modulo > 0)
        total = static_cast<size_t>(std::min(p.modulo, kMaxPassos));
    else if (total > static_cast<size_t>(kMaxPassos))
        total = static_cast<size_t>(kMaxPassos);

    std::vector<Passo> passos(std::max<size_t>(total, 1));
    for (size_t i = 0; i < blocos.size(); ++i)
        for (const Celula& c : blocos[i]) {
            const std::vector<int>& uids = celulas[static_cast<size_t>(c.linha)][static_cast<size_t>(c.coluna)];
            Passo& destino = passos[i % passos.size()];
            destino.insert(destino.end(), uids.begin(), uids.end());
        }
    for (Passo& passo : passos)
        normalizarPasso(passo);
    return passos;
}

void aplicarGerador(Sequencia& sequencia, const std::vector<GeoSlice>& slices)
{
    if (!sequencia.temGerador)
        return;
    sequencia.passos = gerarPassos(slices, sequencia.gerador);
}

}
