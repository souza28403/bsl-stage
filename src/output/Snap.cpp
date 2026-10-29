#include "output/Snap.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>

namespace bsl::snap {

namespace {

constexpr float kEps = 0.01f;

struct Pick
{
    bool found = false;
    float d = 0.0f;
};

// Menor deslocamento que leva algum valor de 'vals' a uma linha (ou a grade), dentro do limiar
Pick pickAxis(const std::vector<float>& vals, const std::vector<Line>& lines, const Options& o, float thr)
{
    Pick best;
    float bestAbs = FLT_MAX;
    for (const float v : vals)
        for (const Line& l : lines) {
            const float d = l.pos - v;
            const float ad = std::abs(d);
            if (ad <= thr && ad < bestAbs) {
                best = {true, d};
                bestAbs = ad;
            }
        }
    if (o.gridOn && o.gridStep > 0.0f)
        for (const float v : vals) {
            const float g = std::round(v / o.gridStep) * o.gridStep;
            const float d = g - v;
            const float ad = std::abs(d);
            if (ad <= thr && ad < bestAbs - 1.0e-4f) { // empate: o alvo (outro slice) ganha da grade
                best = {true, d};
                bestAbs = ad;
            }
        }
    return best;
}

void addGuide(std::vector<Guide>& guides, bool vertical, float pos, float lo, float hi)
{
    for (Guide& g : guides)
        if (g.vertical == vertical && std::abs(g.pos - pos) < kEps) {
            g.lo = std::min(g.lo, lo);
            g.hi = std::max(g.hi, hi);
            return;
        }
    guides.push_back({vertical, pos, lo, hi});
}

// Linhas-guia nas bordas que ficaram alinhadas e marcas nos cantos que caem sobre elas
void finalize(Result& r, const Moving& m, const Targets& t)
{
    const RectF ext{m.extent.x0 + r.dx, m.extent.y0 + r.dy, m.extent.x1 + r.dx, m.extent.y1 + r.dy};
    if (r.snappedX)
        for (const Line& l : t.vx)
            for (const float v : m.xs)
                if (std::abs(l.pos - (v + r.dx)) < kEps) {
                    addGuide(r.guides, true, l.pos, std::min(l.lo, ext.y0), std::max(l.hi, ext.y1));
                    break;
                }
    if (r.snappedY)
        for (const Line& l : t.hy)
            for (const float v : m.ys)
                if (std::abs(l.pos - (v + r.dy)) < kEps) {
                    addGuide(r.guides, false, l.pos, std::min(l.lo, ext.x0), std::max(l.hi, ext.x1));
                    break;
                }
    for (const Point2& p : m.points) {
        const Point2 q{p.x + r.dx, p.y + r.dy};
        bool on = false;
        for (const Guide& g : r.guides)
            if ((g.vertical && std::abs(q.x - g.pos) < kEps) || (!g.vertical && std::abs(q.y - g.pos) < kEps))
                on = true;
        if (!on)
            continue;
        bool dup = false;
        for (const Point2& e : r.marks)
            if (std::abs(e.x - q.x) < kEps && std::abs(e.y - q.y) < kEps)
                dup = true;
        if (!dup)
            r.marks.push_back(q);
    }
}

Point2 closestOnSegment(const Segment& s, Point2 p)
{
    const float vx = s.b.x - s.a.x, vy = s.b.y - s.a.y;
    const float len2 = vx * vx + vy * vy;
    if (len2 < 1.0e-9f)
        return s.a;
    const float u = std::clamp(((p.x - s.a.x) * vx + (p.y - s.a.y) * vy) / len2, 0.0f, 1.0f);
    return {s.a.x + vx * u, s.a.y + vy * u};
}

} // namespace

RectF boundsOf(const Point2 c[4])
{
    RectF r{c[0].x, c[0].y, c[0].x, c[0].y};
    for (int i = 1; i < 4; ++i) {
        r.x0 = std::min(r.x0, c[i].x);
        r.y0 = std::min(r.y0, c[i].y);
        r.x1 = std::max(r.x1, c[i].x);
        r.y1 = std::max(r.y1, c[i].y);
    }
    return r;
}

RectF boundsOf(const RectI& r)
{
    return {static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h)};
}

RectF unite(const RectF& a, const RectF& b)
{
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

Point2 centroid(const Point2 c[4])
{
    return {(c[0].x + c[1].x + c[2].x + c[3].x) * 0.25f, (c[0].y + c[1].y + c[2].y + c[3].y) * 0.25f};
}

void addRect(Targets& t, const RectF& r)
{
    const Point2 c[4] = {{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}};
    addQuad(t, c);
}

void addQuad(Targets& t, const Point2 c[4])
{
    const RectF b = boundsOf(c);
    const Point2 ctr = centroid(c);
    for (int i = 0; i < 4; ++i) {
        t.vx.push_back({c[i].x, b.y0, b.y1});
        t.hy.push_back({c[i].y, b.x0, b.x1});
        t.points.push_back(c[i]);
        t.segments.push_back({c[i], c[(i + 1) % 4]});
    }
    t.vx.push_back({ctr.x, b.y0, b.y1});
    t.hy.push_back({ctr.y, b.x0, b.x1});
    t.points.push_back(ctr);
}

void addBounds(Targets& t, const RectF& r)
{
    for (const float x : {r.x0, r.cx(), r.x1})
        t.vx.push_back({x, r.y0, r.y1});
    for (const float y : {r.y0, r.cy(), r.y1})
        t.hy.push_back({y, r.x0, r.x1});
    for (const float x : {r.x0, r.x1})
        for (const float y : {r.y0, r.y1})
            t.points.push_back({x, y});
    t.points.push_back({r.cx(), r.cy()});
}

Result snapMoving(const Moving& m, const Targets& t, const Options& o)
{
    Result r;
    if (!o.active())
        return r;
    const float thr = o.threshold();
    const Pick px = pickAxis(m.xs, t.vx, o, thr);
    const Pick py = pickAxis(m.ys, t.hy, o, thr);
    if (px.found) {
        r.dx = px.d;
        r.snappedX = true;
    }
    if (py.found) {
        r.dy = py.d;
        r.snappedY = true;
    }
    finalize(r, m, t);
    return r;
}

Result snapPoint(Point2 p, const Targets& t, const Options& o)
{
    Result r;
    if (!o.active())
        return r;
    const float thr = o.threshold();

    // 1) outro canto/centro: encaixa exatamente (e o que "conecta" um slice no outro)
    int best = -1;
    float bestD = thr;
    for (size_t i = 0; i < t.points.size(); ++i) {
        const float d = std::hypot(t.points[i].x - p.x, t.points[i].y - p.y);
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    Moving m;
    m.xs = {p.x};
    m.ys = {p.y};
    m.points = {p};
    m.extent = {p.x, p.y, p.x, p.y};
    if (best >= 0) {
        r.dx = t.points[static_cast<size_t>(best)].x - p.x;
        r.dy = t.points[static_cast<size_t>(best)].y - p.y;
        r.snappedX = r.snappedY = true;
        finalize(r, m, t);
        if (r.marks.empty())
            r.marks.push_back({p.x + r.dx, p.y + r.dy});
        return r;
    }

    // 2) linhas x/y (bordas e centros)
    r = snapMoving(m, t, o);
    if (r.snappedX || r.snappedY)
        return r;

    // 3) aresta mais proxima de outro quad
    float bestEdge = thr;
    Point2 hit{};
    bool found = false;
    for (const Segment& s : t.segments) {
        const Point2 q = closestOnSegment(s, p);
        const float d = std::hypot(q.x - p.x, q.y - p.y);
        if (d <= bestEdge) {
            bestEdge = d;
            hit = q;
            found = true;
        }
    }
    if (found) {
        r.dx = hit.x - p.x;
        r.dy = hit.y - p.y;
        r.snappedX = r.snappedY = true;
        r.marks.push_back(hit);
    }
    return r;
}

std::vector<Point2> alignOffsets(const std::vector<RectF>& boxes, Align mode, const RectF* reference)
{
    std::vector<Point2> out(boxes.size());
    if (boxes.empty())
        return out;
    RectF ref = reference ? *reference : boxes.front();
    if (!reference)
        for (const RectF& b : boxes)
            ref = unite(ref, b);
    for (size_t i = 0; i < boxes.size(); ++i) {
        const RectF& b = boxes[i];
        switch (mode) {
        case Align::Left: out[i].x = ref.x0 - b.x0; break;
        case Align::CenterH: out[i].x = ref.cx() - b.cx(); break;
        case Align::Right: out[i].x = ref.x1 - b.x1; break;
        case Align::Top: out[i].y = ref.y0 - b.y0; break;
        case Align::MiddleV: out[i].y = ref.cy() - b.cy(); break;
        case Align::Bottom: out[i].y = ref.y1 - b.y1; break;
        }
    }
    return out;
}

std::vector<Point2> distributeOffsets(const std::vector<RectF>& boxes, Distribute axis)
{
    std::vector<Point2> out(boxes.size());
    const size_t n = boxes.size();
    if (n < 3)
        return out;
    const bool horiz = axis == Distribute::Horizontal;
    auto lo = [&](size_t i) { return horiz ? boxes[i].x0 : boxes[i].y0; };
    auto hi = [&](size_t i) { return horiz ? boxes[i].x1 : boxes[i].y1; };

    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return lo(a) + hi(a) < lo(b) + hi(b); });

    float total = 0.0f;
    for (size_t i = 0; i < n; ++i)
        total += hi(i) - lo(i);
    const float first = lo(order.front());
    float last = hi(order.back());
    for (size_t i = 0; i < n; ++i)
        last = std::max(last, hi(i));
    const float gap = (last - first - total) / static_cast<float>(n - 1);

    float cursor = first;
    for (size_t k = 0; k < n; ++k) {
        const size_t i = order[k];
        const float shift = cursor - lo(i);
        (horiz ? out[i].x : out[i].y) = shift;
        cursor += (hi(i) - lo(i)) + gap;
    }
    return out;
}

bool abutOffset(const RectF& g, const std::vector<RectF>& others, Dir dir, Point2& out)
{
    bool found = false;
    int bestOverlap = 2;
    float bestGap = FLT_MAX;
    float bestShift = 0.0f;
    for (const RectF& o : others) {
        bool inDir = false;
        float shift = 0.0f;
        bool overlap = false;
        switch (dir) {
        case Dir::Right:
            inDir = o.cx() > g.cx();
            shift = o.x0 - g.x1;
            overlap = std::min(g.y1, o.y1) - std::max(g.y0, o.y0) > 0.0f;
            break;
        case Dir::Left:
            inDir = o.cx() < g.cx();
            shift = o.x1 - g.x0;
            overlap = std::min(g.y1, o.y1) - std::max(g.y0, o.y0) > 0.0f;
            break;
        case Dir::Down:
            inDir = o.cy() > g.cy();
            shift = o.y0 - g.y1;
            overlap = std::min(g.x1, o.x1) - std::max(g.x0, o.x0) > 0.0f;
            break;
        case Dir::Up:
            inDir = o.cy() < g.cy();
            shift = o.y1 - g.y0;
            overlap = std::min(g.x1, o.x1) - std::max(g.x0, o.x0) > 0.0f;
            break;
        }
        if (!inDir)
            continue;
        const int ov = overlap ? 0 : 1; // quem se sobrepoe na outra direcao ganha de quem esta "de lado"
        const float gap = std::abs(shift);
        if (!found || ov < bestOverlap || (ov == bestOverlap && gap < bestGap)) {
            found = true;
            bestOverlap = ov;
            bestGap = gap;
            bestShift = shift;
        }
    }
    if (!found)
        return false;
    out = (dir == Dir::Left || dir == Dir::Right) ? Point2{bestShift, 0.0f} : Point2{0.0f, bestShift};
    return true;
}

} // namespace bsl::snap
