#include "output/MapEdit.h"

#include "output/Snap.h"

#include <algorithm>
#include <cmath>

namespace bsl::mapedit {

namespace {

Point2 rotated(float lx, float ly, float rot)
{
    const float c = std::cos(rot), s = std::sin(rot);
    return {lx * c - ly * s, lx * s + ly * c};
}

} // namespace

float normalizeAngle(float rad)
{
    while (rad > kPi)
        rad -= 2.0f * kPi;
    while (rad <= -kPi)
        rad += 2.0f * kPi;
    return rad;
}

void toCorners(const Xform& x, Point2 out[4])
{
    const float hw = x.w * 0.5f, hh = x.h * 0.5f;
    const float lx[4] = {-hw, hw, hw, -hw};
    const float ly[4] = {-hh, -hh, hh, hh};
    for (int i = 0; i < 4; ++i) {
        if (x.rot == 0.0f) { // sem rotacao: sem ruido de ponto flutuante (as bordas ficam exatas)
            out[i] = {x.cx + lx[i], x.cy + ly[i]};
        } else {
            const Point2 r = rotated(lx[i], ly[i], x.rot);
            out[i] = {x.cx + r.x, x.cy + r.y};
        }
    }
}

bool fromCorners(const Point2 c[4], Xform& out, float tol)
{
    const float v1x = c[1].x - c[0].x, v1y = c[1].y - c[0].y;
    const float v2x = c[3].x - c[0].x, v2y = c[3].y - c[0].y;
    const float len1 = std::hypot(v1x, v1y), len2 = std::hypot(v2x, v2y);
    bool rect = len1 > 1.0e-3f && len2 > 1.0e-3f;
    if (rect) {
        const float cross = v1x * v2y - v1y * v2x;
        rect = cross > 0.0f;                                                   // horario: sem espelhar
        rect = rect && std::abs(v1x * v2x + v1y * v2y) / len1 <= tol;           // lados perpendiculares
        rect = rect && std::hypot(c[2].x - (c[1].x + v2x), c[2].y - (c[1].y + v2y)) <= tol; // paralelogramo
    }
    if (rect) {
        out.cx = (c[0].x + c[2].x) * 0.5f;
        out.cy = (c[0].y + c[2].y) * 0.5f;
        out.w = len1;
        out.h = len2;
        out.rot = std::abs(v1y) < 1.0e-3f && v1x > 0.0f ? 0.0f : std::atan2(v1y, v1x);
        if (out.rot == 0.0f) { // quase sem rotacao: usa a caixa exata (nao a media de cantos ligeiramente diferentes)
            const snap::RectF b = snap::boundsOf(c);
            out.cx = b.cx();
            out.cy = b.cy();
            out.w = b.width();
            out.h = b.height();
        }
        return true;
    }
    const snap::RectF b = snap::boundsOf(c);
    out.cx = b.cx();
    out.cy = b.cy();
    out.w = std::max(b.width(), 1.0f);
    out.h = std::max(b.height(), 1.0f);
    out.rot = 0.0f;
    return false;
}

bool handleLeft(int h) { return h == 0 || h == 6 || h == 7; }
bool handleRight(int h) { return h == 2 || h == 3 || h == 4; }
bool handleTop(int h) { return h == 0 || h == 1 || h == 2; }
bool handleBottom(int h) { return h == 4 || h == 5 || h == 6; }

Point2 handlePoint(const Xform& x, int handle)
{
    const float lx = handleLeft(handle) ? -x.w * 0.5f : (handleRight(handle) ? x.w * 0.5f : 0.0f);
    const float ly = handleTop(handle) ? -x.h * 0.5f : (handleBottom(handle) ? x.h * 0.5f : 0.0f);
    const Point2 r = rotated(lx, ly, x.rot);
    return {x.cx + r.x, x.cy + r.y};
}

Point2 rotateHandlePoint(const Xform& x, float distance)
{
    const Point2 r = rotated(0.0f, -x.h * 0.5f - distance, x.rot);
    return {x.cx + r.x, x.cy + r.y};
}

Point2 toLocal(const Xform& x, float dx, float dy)
{
    return rotated(dx, dy, -x.rot);
}

Xform resizeXform(const Xform& s, int handle, float lx, float ly, const ResizeOptions& opt)
{
    const bool hl = handleLeft(handle), hr = handleRight(handle), ht = handleTop(handle), hb = handleBottom(handle);
    const float minS = std::max(opt.minSize, 0.0f);

    float nl = -s.w * 0.5f, nr = s.w * 0.5f, nt = -s.h * 0.5f, nb = s.h * 0.5f;
    if (hl) nl += lx;
    if (hr) nr += lx;
    if (ht) nt += ly;
    if (hb) nb += ly;
    if (opt.fromCenter) { // a borda oposta anda para o lado contrario
        if (hl) nr = -nl;
        if (hr) nl = -nr;
        if (ht) nb = -nt;
        if (hb) nt = -nb;
    }
    // tamanho minimo (sem inverter o retangulo)
    if (nr - nl < minS) {
        if (opt.fromCenter) { nl = -minS * 0.5f; nr = minS * 0.5f; }
        else if (hl) nl = nr - minS;
        else nr = nl + minS;
    }
    if (nb - nt < minS) {
        if (opt.fromCenter) { nt = -minS * 0.5f; nb = minS * 0.5f; }
        else if (ht) nt = nb - minS;
        else nb = nt + minS;
    }

    if (opt.keepAspect && s.w > 0.0f && s.h > 0.0f) {
        float nw = nr - nl, nh = nb - nt;
        const bool horiz = hl || hr, vert = ht || hb;
        float k;
        if (horiz && vert)
            k = std::max(nw / s.w, nh / s.h);
        else if (horiz)
            k = nw / s.w;
        else
            k = nh / s.h;
        k = std::max(k, minS / std::min(s.w, s.h));
        nw = s.w * k;
        nh = s.h * k;
        if (opt.fromCenter) {
            nl = -nw * 0.5f; nr = nw * 0.5f; nt = -nh * 0.5f; nb = nh * 0.5f;
        } else {
            if (hl) nl = nr - nw; else if (hr) nr = nl + nw; else { nl = -nw * 0.5f; nr = nw * 0.5f; }
            if (ht) nt = nb - nh; else if (hb) nb = nt + nh; else { nt = -nh * 0.5f; nb = nh * 0.5f; }
        }
    }

    Xform out = s;
    const Point2 off = rotated((nl + nr) * 0.5f, (nt + nb) * 0.5f, s.rot);
    out.cx = s.cx + off.x;
    out.cy = s.cy + off.y;
    out.w = nr - nl;
    out.h = nb - nt;
    return out;
}

float rotateAngle(Point2 center, Point2 startMouse, Point2 mouse, float startRot, float step)
{
    const float a0 = std::atan2(startMouse.y - center.y, startMouse.x - center.x);
    const float a1 = std::atan2(mouse.y - center.y, mouse.x - center.x);
    float r = startRot + (a1 - a0);
    if (step > 0.0f)
        r = std::round(r / step) * step;
    return normalizeAngle(r);
}

void roundToPixels(Xform& x)
{
    if (x.rot != 0.0f)
        return;
    x.w = std::max(1.0f, std::round(x.w));
    x.h = std::max(1.0f, std::round(x.h));
    x.setPosition(std::round(x.x()), std::round(x.y()));
}

} // namespace bsl::mapedit
