#include "output/Warp.h"

#include <cmath>

namespace bsl {

bool squareToQuad(const Point2 c[4], Mat3& out)
{
    const double x0 = c[0].x, y0 = c[0].y;
    const double x1 = c[1].x, y1 = c[1].y;
    const double x2 = c[2].x, y2 = c[2].y;
    const double x3 = c[3].x, y3 = c[3].y;

    const double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
    const double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;

    double g = 0.0, h = 0.0;
    if (std::abs(dx3) > 1e-9 || std::abs(dy3) > 1e-9) {
        const double den = dx1 * dy2 - dy1 * dx2;
        if (std::abs(den) < 1e-9)
            return false;
        g = (dx3 * dy2 - dy3 * dx2) / den;
        h = (dx1 * dy3 - dy1 * dx3) / den;
    }
    const double a = x1 - x0 + g * x1;
    const double b = x3 - x0 + h * x3;
    const double d = y1 - y0 + g * y1;
    const double e = y3 - y0 + h * y3;

    const double m[9] = {a, b, x0, d, e, y0, g, h, 1.0};
    for (int i = 0; i < 9; ++i)
        out.m[i] = m[i];

    Mat3 inv;
    return invert(out, inv);
}

bool invert(const Mat3& a, Mat3& out)
{
    const double* m = a.m;
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = m[5] * m[6] - m[3] * m[8];
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (std::abs(det) < 1e-12)
        return false;
    const double id = 1.0 / det;
    out.m[0] = c00 * id;
    out.m[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    out.m[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    out.m[3] = c01 * id;
    out.m[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    out.m[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    out.m[6] = c02 * id;
    out.m[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    out.m[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    return true;
}

Point2 apply(const Mat3& h, double x, double y)
{
    const double* m = h.m;
    const double w = m[6] * x + m[7] * y + m[8];
    const double inv = std::abs(w) < 1e-12 ? 0.0 : 1.0 / w;
    return {static_cast<float>((m[0] * x + m[1] * y + m[2]) * inv),
            static_cast<float>((m[3] * x + m[4] * y + m[5]) * inv)};
}

double quadArea(const Point2 c[4])
{
    double a = 0.0;
    for (int i = 0; i < 4; ++i) {
        const Point2& p = c[i];
        const Point2& q = c[(i + 1) % 4];
        a += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    return a * 0.5;
}

bool pointInQuad(const Point2 c[4], Point2 p)
{
    bool pos = false, neg = false;
    for (int i = 0; i < 4; ++i) {
        const Point2& a = c[i];
        const Point2& b = c[(i + 1) % 4];
        const double cross = static_cast<double>(b.x - a.x) * (p.y - a.y) - static_cast<double>(b.y - a.y) * (p.x - a.x);
        pos = pos || cross > 0.0;
        neg = neg || cross < 0.0;
    }
    return !(pos && neg);
}

}
