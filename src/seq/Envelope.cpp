#include "seq/Envelope.h"

#include <algorithm>
#include <cmath>

namespace bsl::seq {

namespace {

float limitar(float v, float lo, float hi)
{
    if (!std::isfinite(v))
        return lo;
    return std::clamp(v, lo, hi);
}

struct Ponto
{
    float x;
    float y;
};

struct Segmento
{
    Ponto p0, c1, c2, p1;
    bool reto;
};

Segmento segmento(const std::vector<Ancora>& a, size_t i)
{
    const Ancora& s = a[i];
    const Ancora& t = a[i + 1];
    return {{s.x, s.y}, {s.x + s.saidaX, s.y + s.saidaY}, {t.x + t.entradaX, t.y + t.entradaY}, {t.x, t.y}, s.reto};
}

Ponto bezier(const Segmento& g, float t)
{
    const float u = 1.0f - t;
    const float a = u * u * u;
    const float b = 3.0f * u * u * t;
    const float c = 3.0f * u * t * t;
    const float d = t * t * t;
    return {a * g.p0.x + b * g.c1.x + c * g.c2.x + d * g.p1.x, a * g.p0.y + b * g.c1.y + c * g.c2.y + d * g.p1.y};
}

float resolverT(const Segmento& g, float x)
{
    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 40; ++i) {
        const float meio = 0.5f * (lo + hi);
        if (bezier(g, meio).x < x)
            lo = meio;
        else
            hi = meio;
    }
    return 0.5f * (lo + hi);
}

Ponto meio(Ponto a, Ponto b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

}

float Envelope::avaliar(float x) const
{
    if (ancoras.empty())
        return limitar(x, 0.0f, 1.0f);
    if (ancoras.size() == 1)
        return ancoras[0].y;
    x = limitar(x, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < ancoras.size() && x > ancoras[i + 1].x)
        ++i;
    const Segmento g = segmento(ancoras, i);
    if (g.reto) {
        const float largura = g.p1.x - g.p0.x;
        if (largura <= 0.0f)
            return g.p1.y;
        return g.p0.y + (g.p1.y - g.p0.y) * ((x - g.p0.x) / largura);
    }
    const float t = resolverT(g, x);
    return limitar(bezier(g, t).y, 0.0f, 1.0f);
}

float Envelope::amostrar(float x) const
{
    if (tabela.size() != static_cast<size_t>(kTamanhoTabela))
        return avaliar(x);
    x = limitar(x, 0.0f, 1.0f) * static_cast<float>(kTamanhoTabela - 1);
    const int i = std::min(static_cast<int>(x), kTamanhoTabela - 2);
    const float f = x - static_cast<float>(i);
    return tabela[static_cast<size_t>(i)] * (1.0f - f) + tabela[static_cast<size_t>(i) + 1] * f;
}

Envelope envelopeCurvaS(int id, const std::string& nome)
{
    Envelope e;
    e.id = id;
    e.nome = nome;
    Ancora a;
    a.saidaX = 0.42f;
    Ancora b;
    b.x = 1.0f;
    b.y = 1.0f;
    b.entradaX = -0.42f;
    e.ancoras = {a, b};
    reconstruirTabela(e);
    return e;
}

Envelope envelopeLinear(int id, const std::string& nome)
{
    Envelope e;
    e.id = id;
    e.nome = nome;
    Ancora a;
    a.reto = true;
    Ancora b;
    b.x = 1.0f;
    b.y = 1.0f;
    e.ancoras = {a, b};
    reconstruirTabela(e);
    return e;
}

void normalizarEnvelope(Envelope& e)
{
    for (Ancora& a : e.ancoras) {
        a.x = limitar(a.x, 0.0f, 1.0f);
        a.y = limitar(a.y, 0.0f, 1.0f);
    }
    std::stable_sort(e.ancoras.begin(), e.ancoras.end(), [](const Ancora& a, const Ancora& b) { return a.x < b.x; });
    if (static_cast<int>(e.ancoras.size()) > kMaxAncoras)
        e.ancoras.resize(static_cast<size_t>(kMaxAncoras));
    if (e.ancoras.size() < 2) {
        const Envelope padrao = envelopeCurvaS();
        if (e.ancoras.empty()) {
            e.ancoras = padrao.ancoras;
        } else {
            Ancora u = e.ancoras[0];
            u.x = 0.0f;
            Ancora v = padrao.ancoras[1];
            e.ancoras = {u, v};
        }
    }
    const size_t n = e.ancoras.size();
    e.ancoras.front().x = 0.0f;
    e.ancoras.back().x = 1.0f;
    for (size_t i = 1; i + 1 < n; ++i) {
        const float minimo = e.ancoras[i - 1].x + kEspacoMinimo;
        const float maximo = 1.0f - static_cast<float>(n - 1 - i) * kEspacoMinimo;
        e.ancoras[i].x = std::clamp(e.ancoras[i].x, minimo, std::max(minimo, maximo));
    }
    e.ancoras.front().entradaX = 0.0f;
    e.ancoras.front().entradaY = 0.0f;
    e.ancoras.back().saidaX = 0.0f;
    e.ancoras.back().saidaY = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        Ancora& a = e.ancoras[i];
        const float esquerda = i == 0 ? 0.0f : a.x - e.ancoras[i - 1].x;
        const float direita = i + 1 == n ? 0.0f : e.ancoras[i + 1].x - a.x;
        a.entradaX = limitar(a.entradaX, -esquerda, 0.0f);
        a.saidaX = limitar(a.saidaX, 0.0f, direita);
        a.entradaY = limitar(a.entradaY, -a.y, 1.0f - a.y);
        a.saidaY = limitar(a.saidaY, -a.y, 1.0f - a.y);
    }
    reconstruirTabela(e);
}

void reconstruirTabela(Envelope& e)
{
    e.tabela.resize(static_cast<size_t>(kTamanhoTabela));
    for (int i = 0; i < kTamanhoTabela; ++i)
        e.tabela[static_cast<size_t>(i)] = e.avaliar(static_cast<float>(i) / static_cast<float>(kTamanhoTabela - 1));
}

int inserirAncora(Envelope& e, float x)
{
    normalizarEnvelope(e);
    if (static_cast<int>(e.ancoras.size()) >= kMaxAncoras)
        return -1;
    x = limitar(x, 0.0f, 1.0f);
    size_t i = 0;
    while (i + 2 < e.ancoras.size() && x > e.ancoras[i + 1].x)
        ++i;
    const Ancora esq = e.ancoras[i];
    const Ancora dir = e.ancoras[i + 1];
    if (x < esq.x + kEspacoMinimo || x > dir.x - kEspacoMinimo)
        return -1;
    Ancora nova;
    nova.x = x;
    if (esq.reto) {
        nova.y = e.avaliar(x);
        nova.reto = true;
        nova.entradaX = nova.entradaY = nova.saidaX = nova.saidaY = 0.0f;
        e.ancoras.insert(e.ancoras.begin() + static_cast<std::ptrdiff_t>(i) + 1, nova);
    } else {
        const Segmento g = segmento(e.ancoras, i);
        const float t = resolverT(g, x);
        const Ponto ab = meio(g.p0, g.c1, t);
        const Ponto bc = meio(g.c1, g.c2, t);
        const Ponto cd = meio(g.c2, g.p1, t);
        const Ponto abc = meio(ab, bc, t);
        const Ponto bcd = meio(bc, cd, t);
        const Ponto m = meio(abc, bcd, t);
        nova.x = m.x;
        nova.y = m.y;
        nova.entradaX = abc.x - m.x;
        nova.entradaY = abc.y - m.y;
        nova.saidaX = bcd.x - m.x;
        nova.saidaY = bcd.y - m.y;
        e.ancoras[i].saidaX = ab.x - esq.x;
        e.ancoras[i].saidaY = ab.y - esq.y;
        e.ancoras[i + 1].entradaX = cd.x - dir.x;
        e.ancoras[i + 1].entradaY = cd.y - dir.y;
        e.ancoras.insert(e.ancoras.begin() + static_cast<std::ptrdiff_t>(i) + 1, nova);
    }
    normalizarEnvelope(e);
    return static_cast<int>(i) + 1;
}

bool removerAncora(Envelope& e, int indice)
{
    if (indice <= 0 || indice + 1 >= static_cast<int>(e.ancoras.size()))
        return false;
    e.ancoras.erase(e.ancoras.begin() + indice);
    normalizarEnvelope(e);
    return true;
}

void moverAncora(Envelope& e, int indice, float x, float y)
{
    if (indice < 0 || indice >= static_cast<int>(e.ancoras.size()))
        return;
    Ancora& a = e.ancoras[static_cast<size_t>(indice)];
    if (indice != 0 && indice + 1 != static_cast<int>(e.ancoras.size()))
        a.x = x;
    a.y = y;
    normalizarEnvelope(e);
}

void definirAlca(Envelope& e, int indice, bool saida, float dx, float dy)
{
    if (indice < 0 || indice >= static_cast<int>(e.ancoras.size()))
        return;
    Ancora& a = e.ancoras[static_cast<size_t>(indice)];
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    if (saida) {
        a.saidaX = std::max(dx, 0.0f);
        a.saidaY = dy;
    } else {
        a.entradaX = std::min(dx, 0.0f);
        a.entradaY = dy;
    }
    if (a.alcasLigadas) {
        const float propriaX = saida ? a.saidaX : a.entradaX;
        const float propriaY = saida ? a.saidaY : a.entradaY;
        const float comprimento = std::hypot(propriaX, propriaY);
        float& ox = saida ? a.entradaX : a.saidaX;
        float& oy = saida ? a.entradaY : a.saidaY;
        const float outro = std::hypot(ox, oy);
        if (comprimento > 1e-6f && outro > 1e-6f) {
            ox = -propriaX / comprimento * outro;
            oy = -propriaY / comprimento * outro;
        }
    }
    normalizarEnvelope(e);
}

void definirReto(Envelope& e, int indice, bool reto)
{
    if (indice < 0 || indice + 1 >= static_cast<int>(e.ancoras.size()))
        return;
    e.ancoras[static_cast<size_t>(indice)].reto = reto;
    reconstruirTabela(e);
}

void definirAlcasLigadas(Envelope& e, int indice, bool ligadas)
{
    if (indice < 0 || indice >= static_cast<int>(e.ancoras.size()))
        return;
    e.ancoras[static_cast<size_t>(indice)].alcasLigadas = ligadas;
}

void garantirBiblioteca(BibliotecaEnvelopes& b)
{
    std::vector<Envelope> validos;
    for (Envelope& e : b.lista) {
        if (static_cast<int>(validos.size()) >= kMaxEnvelopes)
            break;
        const bool repetido = std::any_of(validos.begin(), validos.end(), [&](const Envelope& v) { return v.id == e.id; });
        if (e.id < 1 || repetido)
            e.id = 0;
        validos.push_back(std::move(e));
    }
    b.lista = std::move(validos);
    int maior = 0;
    for (const Envelope& e : b.lista)
        maior = std::max(maior, e.id);
    b.proximoId = std::max({b.proximoId, maior + 1, 1});
    for (Envelope& e : b.lista) {
        if (e.id == 0)
            e.id = b.proximoId++;
        normalizarEnvelope(e);
    }
    if (b.lista.empty())
        b.lista.push_back(envelopeCurvaS(b.proximoId++));
}

int criarEnvelope(BibliotecaEnvelopes& b, const std::string& nome)
{
    if (static_cast<int>(b.lista.size()) >= kMaxEnvelopes)
        return 0;
    b.lista.push_back(envelopeCurvaS(b.proximoId++, nome));
    return b.lista.back().id;
}

int duplicarEnvelope(BibliotecaEnvelopes& b, int id)
{
    const Envelope* origem = acharEnvelope(b, id);
    if (!origem || static_cast<int>(b.lista.size()) >= kMaxEnvelopes)
        return 0;
    Envelope copia = *origem;
    copia.id = b.proximoId++;
    copia.nome += " (cópia)";
    b.lista.push_back(std::move(copia));
    return b.lista.back().id;
}

bool excluirEnvelope(BibliotecaEnvelopes& b, int id)
{
    const auto it = std::find_if(b.lista.begin(), b.lista.end(), [id](const Envelope& e) { return e.id == id; });
    if (it == b.lista.end())
        return false;
    b.lista.erase(it);
    if (b.lista.empty())
        b.lista.push_back(envelopeCurvaS(b.proximoId++));
    return true;
}

bool renomearEnvelope(BibliotecaEnvelopes& b, int id, const std::string& nome)
{
    Envelope* e = acharEnvelope(b, id);
    if (!e)
        return false;
    e->nome = nome;
    return true;
}

Envelope* acharEnvelope(BibliotecaEnvelopes& b, int id)
{
    for (Envelope& e : b.lista)
        if (e.id == id)
            return &e;
    return nullptr;
}

const Envelope* acharEnvelope(const BibliotecaEnvelopes& b, int id)
{
    for (const Envelope& e : b.lista)
        if (e.id == id)
            return &e;
    return nullptr;
}

}
