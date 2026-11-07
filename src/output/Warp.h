#pragma once

#include "output/OutputSetup.h"

namespace bsl {

struct Mat3
{
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

bool squareToQuad(const Point2 c[4], Mat3& out);

bool invert(const Mat3& a, Mat3& out);

Point2 apply(const Mat3& h, double x, double y);

bool pointInQuad(const Point2 c[4], Point2 p);

double quadArea(const Point2 c[4]);

}
