#pragma once

#include <algorithm>
#include <cmath>

namespace bsl {

struct OrientedBox
{
    float cx = 0.0f;
    float cy = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    float rot = 0.0f;
};

struct TransformGpu
{
    bool on = false;
    float box[4] = {0, 0, 1, 1};
    float pivot[2] = {0, 0};
    float cs[2] = {1, 0};
    float crop[4] = {0, 0, 0, 0};
    float canvas[2] = {1, 1};
};

struct LayerTransform
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float rotation = 0.0f;
    float anchorX = 0.5f;
    float anchorY = 0.5f;
    float cropL = 0.0f;
    float cropT = 0.0f;
    float cropR = 0.0f;
    float cropB = 0.0f;

    bool operator==(const LayerTransform&) const = default;

    float width(int canvasW) const { return w > 0.0f ? w : static_cast<float>(canvasW); }
    float height(int canvasH) const { return h > 0.0f ? h : static_cast<float>(canvasH); }

    bool isDefault() const { return *this == LayerTransform{}; }

    LayerTransform settled(int canvasW, int canvasH) const
    {
        LayerTransform t = *this;
        auto snap0 = [](float& v) {
            if (std::abs(v) < 1.0e-4f)
                v = 0.0f;
        };
        for (float* v : {&t.x, &t.y, &t.rotation, &t.cropL, &t.cropT, &t.cropR, &t.cropB})
            snap0(*v);
        if (std::abs(t.w - static_cast<float>(canvasW)) < 1.0e-3f)
            t.w = 0.0f;
        if (std::abs(t.h - static_cast<float>(canvasH)) < 1.0e-3f)
            t.h = 0.0f;
        t.w = t.w < 0.0f ? 0.0f : t.w;
        t.h = t.h < 0.0f ? 0.0f : t.h;
        return t;
    }

    bool isIdentity(int canvasW, int canvasH) const
    {
        const LayerTransform t = settled(canvasW, canvasH);
        return t.x == 0.0f && t.y == 0.0f && t.w == 0.0f && t.h == 0.0f && t.rotation == 0.0f && t.cropL == 0.0f
               && t.cropT == 0.0f && t.cropR == 0.0f && t.cropB == 0.0f;
    }

    void clampCrop(int canvasW, int canvasH)
    {
        const float bw = width(canvasW), bh = height(canvasH);
        cropL = std::clamp(cropL, 0.0f, std::max(0.0f, bw - 1.0f));
        cropR = std::clamp(cropR, 0.0f, std::max(0.0f, bw - 1.0f - cropL));
        cropT = std::clamp(cropT, 0.0f, std::max(0.0f, bh - 1.0f));
        cropB = std::clamp(cropB, 0.0f, std::max(0.0f, bh - 1.0f - cropT));
    }

    static LayerTransform rect(float x, float y, float w, float h)
    {
        LayerTransform t;
        t.x = x;
        t.y = y;
        t.w = w;
        t.h = h;
        return t;
    }

    OrientedBox box(int canvasW, int canvasH) const
    {
        const float bw = width(canvasW), bh = height(canvasH);
        const float rad = rotation * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(rad), s = std::sin(rad);
        const float px = x + anchorX * bw, py = y + anchorY * bh;
        const float dx = (0.5f - anchorX) * bw, dy = (0.5f - anchorY) * bh;
        OrientedBox b;
        b.w = bw;
        b.h = bh;
        b.rot = rotation == 0.0f ? 0.0f : rad;
        b.cx = rotation == 0.0f ? px + dx : px + dx * c - dy * s;
        b.cy = rotation == 0.0f ? py + dy : py + dx * s + dy * c;
        return b;
    }

    void setBox(const OrientedBox& b, int canvasW, int canvasH)
    {
        w = b.w;
        h = b.h;
        const float rad = b.rot;
        rotation = rad * 180.0f / 3.14159265358979323846f;
        const float c = std::cos(rad), s = std::sin(rad);
        const float dx = (0.5f - anchorX) * b.w, dy = (0.5f - anchorY) * b.h;
        const float rx = rad == 0.0f ? dx : dx * c - dy * s;
        const float ry = rad == 0.0f ? dy : dx * s + dy * c;
        x = b.cx - rx - anchorX * b.w;
        y = b.cy - ry - anchorY * b.h;
        *this = settled(canvasW, canvasH);
    }

    TransformGpu gpu(int canvasW, int canvasH) const
    {
        TransformGpu g;
        g.canvas[0] = static_cast<float>(canvasW);
        g.canvas[1] = static_cast<float>(canvasH);
        if (isIdentity(canvasW, canvasH))
            return g;
        const LayerTransform t = settled(canvasW, canvasH);
        const float bw = t.width(canvasW), bh = t.height(canvasH);
        const float rad = t.rotation * 3.14159265358979323846f / 180.0f;
        g.on = true;
        g.box[0] = t.x;
        g.box[1] = t.y;
        g.box[2] = bw;
        g.box[3] = bh;
        g.pivot[0] = t.x + t.anchorX * bw;
        g.pivot[1] = t.y + t.anchorY * bh;
        g.cs[0] = t.rotation == 0.0f ? 1.0f : std::cos(rad);
        g.cs[1] = t.rotation == 0.0f ? 0.0f : std::sin(rad);
        g.crop[0] = t.cropL;
        g.crop[1] = t.cropT;
        g.crop[2] = t.cropR;
        g.crop[3] = t.cropB;
        return g;
    }

    void toLocal(float px, float py, int canvasW, int canvasH, float& lx, float& ly) const
    {
        const TransformGpu g = gpu(canvasW, canvasH);
        if (!g.on) {
            lx = px;
            ly = py;
            return;
        }
        const float dx = px - g.pivot[0], dy = py - g.pivot[1];
        lx = g.pivot[0] + g.cs[0] * dx + g.cs[1] * dy - g.box[0];
        ly = g.pivot[1] - g.cs[1] * dx + g.cs[0] * dy - g.box[1];
    }

    bool contains(float px, float py, int canvasW, int canvasH) const
    {
        float lx = 0.0f, ly = 0.0f;
        toLocal(px, py, canvasW, canvasH, lx, ly);
        const LayerTransform t = settled(canvasW, canvasH);
        const float bw = t.width(canvasW), bh = t.height(canvasH);
        return lx >= t.cropL && lx <= bw - t.cropR && ly >= t.cropT && ly <= bh - t.cropB;
    }
};

enum class TransformPreset {
    Full = 0,
    Fit,
    Fill,
    LeftHalf,
    RightHalf,
    Quad1,
    Quad2,
    Quad3,
    Quad4,
    Grid2x2,
    Grid3x3,
};
inline constexpr int kTransformPresetCount = 11;

inline const char* transformPresetName(int p)
{
    static const char* const names[kTransformPresetCount] = {
        "Tela cheia",   "Ajustar",       "Preencher",    "Metade esquerda",         "Metade direita",
        "Quadrante 1",  "Quadrante 2",   "Quadrante 3",  "Quadrante 4",             "Grade 2×2 com as camadas 1–4",
        "Grade 3×3 com as camadas 1–9"};
    return (p >= 0 && p < kTransformPresetCount) ? names[p] : "";
}

inline LayerTransform gridCellTransform(int cols, int rows, int col, int row, int canvasW, int canvasH)
{
    const int x0 = static_cast<int>(std::lround(static_cast<double>(col) * canvasW / cols));
    const int x1 = static_cast<int>(std::lround(static_cast<double>(col + 1) * canvasW / cols));
    const int y0 = static_cast<int>(std::lround(static_cast<double>(row) * canvasH / rows));
    const int y1 = static_cast<int>(std::lround(static_cast<double>(row + 1) * canvasH / rows));
    return LayerTransform::rect(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0),
                                static_cast<float>(y1 - y0))
        .settled(canvasW, canvasH);
}

}
