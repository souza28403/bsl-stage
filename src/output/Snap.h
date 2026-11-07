#pragma once

#include "output/OutputSetup.h"

#include <vector>

namespace bsl::snap {

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
    bool enabled = true;
    bool altHeld = false;
    float thresholdPx = 8.0f;
    float zoom = 1.0f;
    bool gridOn = false;
    float gridStep = 16.0f;

    bool active() const { return enabled && !altHeld; }
    float threshold() const { return thresholdPx / (zoom > 1.0e-6f ? zoom : 1.0e-6f); }
};

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
    std::vector<Line> vx;
    std::vector<Line> hy;
    std::vector<Point2> points;
    std::vector<Segment> segments;
};

void addRect(Targets& t, const RectF& r);
void addQuad(Targets& t, const Point2 c[4]);
void addBounds(Targets& t, const RectF& r);

struct Guide
{
    bool vertical = true;
    float pos = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;
};

struct Result
{
    float dx = 0.0f;
    float dy = 0.0f;
    bool snappedX = false;
    bool snappedY = false;
    std::vector<Guide> guides;
    std::vector<Point2> marks;
};

struct Moving
{
    std::vector<float> xs;
    std::vector<float> ys;
    std::vector<Point2> points;
    RectF extent;
};

Result snapMoving(const Moving& m, const Targets& t, const Options& o);

Result snapPoint(Point2 p, const Targets& t, const Options& o);

enum class Align { Left, CenterH, Right, Top, MiddleV, Bottom };
enum class Distribute { Horizontal, Vertical };
enum class Dir { Left, Right, Up, Down };

std::vector<Point2> alignOffsets(const std::vector<RectF>& boxes, Align mode, const RectF* reference = nullptr);
std::vector<Point2> distributeOffsets(const std::vector<RectF>& boxes, Distribute axis);
bool abutOffset(const RectF& group, const std::vector<RectF>& others, Dir dir, Point2& out);

}
