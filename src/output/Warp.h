#pragma once

#include "output/OutputSetup.h"

namespace bsl {

// Matriz 3x3 em ordem de linhas
struct Mat3
{
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// Homografia que leva o quadrado unitário (u,v) aos 4 cantos TL,TR,BR,BL:
//   (0,0)->c[0]  (1,0)->c[1]  (1,1)->c[2]  (0,1)->c[3]
// false se o quad é degenerado (área zero / colinear).
bool squareToQuad(const Point2 c[4], Mat3& out);

bool invert(const Mat3& a, Mat3& out);

// Aplica a homografia a (x,y)
Point2 apply(const Mat3& h, double x, double y);

// Ponto dentro do quad (qualquer orientação; válido para quads convexos)
bool pointInQuad(const Point2 c[4], Point2 p);

// Área com sinal (positiva no sentido horário com y para baixo)
double quadArea(const Point2 c[4]);

} // namespace bsl
