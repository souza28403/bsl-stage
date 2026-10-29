#pragma once

#include "output/OutputSetup.h"

#include <vector>

namespace bsl::snap {

// Logica pura (sem ImGui, sem GL) do editor de mapeamento: ima (snap), alinhar, distribuir e "Encostar".
// Todas as coordenadas sao do CONTEUDO (px do canvas na Entrada, px da tela na Saida); o limiar do ima vem em px de
// TELA e e convertido pelo zoom, entao encaixa a mesma distancia visual em qualquer zoom.

struct RectF
{
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;

    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
    bool overlaps(const RectF& o) const { return x0 <= o.x1 && o.x0 <= x1 && y0 <= o.y1 && o.y0 <= y1; }
    bool contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

RectF boundsOf(const Point2 c[4]);
RectF boundsOf(const RectI& r);
RectF unite(const RectF& a, const RectF& b);
Point2 centroid(const Point2 c[4]);

struct Options
{
    bool enabled = true;      // o toggle do ima na barra
    bool altHeld = false;     // Alt desliga temporariamente
    float thresholdPx = 8.0f; // limiar em px de TELA
    float zoom = 1.0f;        // px de tela por px de conteudo
    bool gridOn = false;
    float gridStep = 16.0f;   // em px de conteudo

    bool active() const { return enabled && !altHeld; }
    float threshold() const { return thresholdPx / (zoom > 1.0e-6f ? zoom : 1.0e-6f); } // em px de conteudo
};

// Linha de alinhamento: x constante (vx) ou y constante (hy), com a extensao do objeto de origem (para a linha-guia)
struct Line
{
    float pos = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;
};

struct Segment
{
    Point2 a, b;
};

struct Targets
{
    std::vector<Line> vx;       // x constante
    std::vector<Line> hy;       // y constante
    std::vector<Point2> points; // cantos e centros
    std::vector<Segment> segments; // arestas dos quads (um canto encaixa na aresta)
};

void addRect(Targets& t, const RectF& r);        // bordas, centro e cantos
void addQuad(Targets& t, const Point2 c[4]);     // x/y de cada canto, centro, cantos e arestas
void addBounds(Targets& t, const RectF& r);      // bordas e centro do canvas/tela (sem arestas)

struct Guide
{
    bool vertical = true; // true: linha x = pos, de lo a hi (em y)
    float pos = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;
};

struct Result
{
    float dx = 0.0f; // deslocamento a somar a posicao proposta
    float dy = 0.0f;
    bool snappedX = false;
    bool snappedY = false;
    std::vector<Guide> guides;  // linhas-guia (vermelhas finas)
    std::vector<Point2> marks;  // cantos encaixados (ja na posicao final)
};

// O que esta sendo movido: valores x/y candidatos (bordas, centro, cantos), pontos (cantos) e a caixa que o envolve.
// Qualquer lista pode ficar vazia (ao redimensionar so a(s) borda(s) arrastada(s) entram).
struct Moving
{
    std::vector<float> xs;
    std::vector<float> ys;
    std::vector<Point2> points;
    RectF extent;
};

Result snapMoving(const Moving& m, const Targets& t, const Options& o);

// Um canto sozinho: encaixa primeiro em outro canto/centro (distancia), depois nas linhas x/y e, por fim, na aresta mais
// proxima de outro quad.
Result snapPoint(Point2 p, const Targets& t, const Options& o);

// --- Alinhar / distribuir / encostar (devolvem o deslocamento de cada caixa) ---
enum class Align { Left, CenterH, Right, Top, MiddleV, Bottom };
enum class Distribute { Horizontal, Vertical };
enum class Dir { Left, Right, Up, Down };

// 'reference' = caixa de referencia (canvas/tela) quando ha um unico item; nulo = a caixa que envolve todos
std::vector<Point2> alignOffsets(const std::vector<RectF>& boxes, Align mode, const RectF* reference = nullptr);
// Precisa de 3 ou mais; o primeiro e o ultimo (pela posicao) ficam onde estao e os de dentro ganham vaos iguais
std::vector<Point2> distributeOffsets(const std::vector<RectF>& boxes, Distribute axis);
// Cola 'group' no vizinho mais proximo na direcao dada: depois do deslocamento as bordas ficam iguais. false = sem vizinho.
bool abutOffset(const RectF& group, const std::vector<RectF>& others, Dir dir, Point2& out);

} // namespace bsl::snap
