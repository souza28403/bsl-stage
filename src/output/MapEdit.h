#pragma once

#include "output/OutputSetup.h"

namespace bsl::mapedit {

// Geometria pura do editor de mapeamento (sem ImGui/GL): o quad tratado como retangulo com posicao, tamanho e rotacao.

// Retangulo orientado: centro, tamanho e rotacao em radianos (sentido horario na tela, y para baixo), em volta do centro.
struct Xform
{
    float cx = 0.0f;
    float cy = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    float rot = 0.0f;

    // Posicao "X, Y" mostrada nas propriedades: canto superior esquerdo do retangulo ANTES de girar
    float x() const { return cx - w * 0.5f; }
    float y() const { return cy - h * 0.5f; }
    void setPosition(float nx, float ny) { cx = nx + w * 0.5f; cy = ny + h * 0.5f; }
};

// Cantos TL, TR, BR, BL
void toCorners(const Xform& x, Point2 out[4]);
// true se os 4 cantos formam um retangulo (talvez girado) dentro de 'tol' px. false: 'out' vira a caixa que envolve
// os cantos (rot = 0), que e o que o modo Transformar usa para um quad deformado.
bool fromCorners(const Point2 c[4], Xform& out, float tol = 0.5f);

// Alcas: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L (sentido horario)
bool handleLeft(int h);
bool handleRight(int h);
bool handleTop(int h);
bool handleBottom(int h);
// Posicao de uma alca em coordenadas do conteudo
Point2 handlePoint(const Xform& x, int handle);
// Alca de rotacao: acima do meio do topo, 'distance' px de conteudo para fora (no sentido "para cima" do retangulo)
Point2 rotateHandlePoint(const Xform& x, float distance);

struct ResizeOptions
{
    bool keepAspect = false; // Shift
    bool fromCenter = false; // Alt
    float minSize = 1.0f;
};

// 'lx, ly' = deslocamento do mouse no referencial do retangulo (ja desfeita a rotacao)
Xform resizeXform(const Xform& start, int handle, float lx, float ly, const ResizeOptions& opt);
// Gira o retangulo: angulo inicial + variacao do angulo do mouse em volta do centro. 'step' > 0 arredonda o angulo
// final a multiplos dele (rad; 15 graus com Shift).
float rotateAngle(Point2 center, Point2 startMouse, Point2 mouse, float startRot, float step);
// Desfaz a rotacao de um deslocamento (dx, dy) do mouse
Point2 toLocal(const Xform& x, float dx, float dy);
// Sem rotacao, deixa posicao e tamanho em pixels inteiros
void roundToPixels(Xform& x);
float normalizeAngle(float rad); // (-pi, pi]

constexpr float kPi = 3.14159265358979323846f;
inline float degToRad(float d) { return d * kPi / 180.0f; }
inline float radToDeg(float r) { return r * 180.0f / kPi; }

} // namespace bsl::mapedit
