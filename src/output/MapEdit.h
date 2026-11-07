#pragma once

#include "output/OutputSetup.h"

namespace bsl::mapedit {

struct Xform
{
    float cx = 0.0f;
    float cy = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    float rot = 0.0f;

    float x() const { return cx - w * 0.5f; }
    float y() const { return cy - h * 0.5f; }
    void setPosition(float nx, float ny) { cx = nx + w * 0.5f; cy = ny + h * 0.5f; }
};

void toCorners(const Xform& x, Point2 out[4]);
bool fromCorners(const Point2 c[4], Xform& out, float tol = 0.5f);

bool handleLeft(int h);
bool handleRight(int h);
bool handleTop(int h);
bool handleBottom(int h);
Point2 handlePoint(const Xform& x, int handle);
Point2 rotateHandlePoint(const Xform& x, float distance);

struct ResizeOptions
{
    bool keepAspect = false;
    bool fromCenter = false;
    float minSize = 1.0f;
};

Xform resizeXform(const Xform& start, int handle, float lx, float ly, const ResizeOptions& opt);
float rotateAngle(Point2 center, Point2 startMouse, Point2 mouse, float startRot, float step);
Point2 toLocal(const Xform& x, float dx, float dy);
void roundToPixels(Xform& x);
float normalizeAngle(float rad);

constexpr float kPi = 3.14159265358979323846f;
inline float degToRad(float d) { return d * kPi / 180.0f; }
inline float radToDeg(float r) { return r * 180.0f / kPi; }

}
