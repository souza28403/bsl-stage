#include "output/PixelMap.h"

#include <algorithm>
#include <cmath>

namespace bsl::pixelmap {

namespace {

std::string uniqueScreenName(const OutputSetup& out, const std::string& base)
{
    auto taken = [&](const std::string& n) {
        return std::any_of(out.screens.begin(), out.screens.end(), [&](const Screen& s) { return s.name == n; });
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + " " + std::to_string(n);
        if (!taken(candidate))
            return candidate;
    }
}

Slice sliceFor(const Panel& p, double canvasPerPhysX, double canvasPerPhysY, int canvasW, int canvasH)
{
    Slice s;
    s.name = p.name;
    // Entrada: o retangulo escalado no canvas real (Math.round como a tabela), dentro do canvas
    int x = static_cast<int>(jsRound(p.x * canvasPerPhysX));
    int y = static_cast<int>(jsRound(p.y * canvasPerPhysY));
    int w = static_cast<int>(jsRound(p.w * canvasPerPhysX));
    int h = static_cast<int>(jsRound(p.h * canvasPerPhysY));
    x = std::clamp(x, 0, std::max(0, canvasW - 1));
    y = std::clamp(y, 0, std::max(0, canvasH - 1));
    w = std::clamp(w, 1, std::max(1, canvasW - x));
    h = std::clamp(h, 1, std::max(1, canvasH - y));
    s.input = {x, y, w, h};
    // Saida: a posicao fisica
    const float fx = static_cast<float>(p.x), fy = static_cast<float>(p.y);
    const float fw = static_cast<float>(p.w), fh = static_cast<float>(p.h);
    s.corners[kCornerTL] = {fx, fy};
    s.corners[kCornerTR] = {fx + fw, fy};
    s.corners[kCornerBR] = {fx + fw, fy + fh};
    s.corners[kCornerBL] = {fx, fy + fh};
    return s;
}

} // namespace

int createMapping(const Config& c, OutputSetup& out, MapMode mode, int targetScreen, int canvasW, int canvasH)
{
    canvasW = std::max(canvasW, 1);
    canvasH = std::max(canvasH, 1);
    int index = targetScreen;
    if (mode == MapMode::Add || index < 0 || index >= static_cast<int>(out.screens.size())) {
        index = out.addScreen(canvasW, canvasH);
        out.screens[static_cast<size_t>(index)].name = uniqueScreenName(out, "Pixel Map");
        out.screens[static_cast<size_t>(index)].spoutName = "BSL Stage - " + out.screens[static_cast<size_t>(index)].name;
        out.screens[static_cast<size_t>(index)].ndiName = out.screens[static_cast<size_t>(index)].spoutName;
    }
    Screen& screen = out.screens[static_cast<size_t>(index)];
    screen.width = std::clamp(c.physW, 16, 8192);
    screen.height = std::clamp(c.physH, 16, 8192);

    const double kx = static_cast<double>(canvasW) / static_cast<double>(std::max(c.physW, 1));
    const double ky = static_cast<double>(canvasH) / static_cast<double>(std::max(c.physH, 1));
    screen.slices.clear();
    for (const Panel& p : c.panels)
        screen.slices.push_back(sliceFor(p, kx, ky, canvasW, canvasH));
    if (screen.slices.empty()) // uma Screen sem slices nao mostra nada: deixa o slice cheio de sempre
        screen.slices.push_back(makeFullSlice("Slice 1", canvasW, canvasH, screen.width, screen.height));
    return index;
}

bool importFromScreen(Config& c, const Screen& screen)
{
    if (screen.slices.empty())
        return false;
    std::vector<Panel> panels;
    for (const Slice& s : screen.slices) {
        float x0 = s.corners[0].x, x1 = x0, y0 = s.corners[0].y, y1 = y0;
        for (const Point2& p : s.corners) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        Panel p;
        p.name = s.name;
        p.x = static_cast<int>(jsRound(x0));
        p.y = static_cast<int>(jsRound(y0));
        p.w = std::max(1, static_cast<int>(jsRound(x1)) - p.x);
        p.h = std::max(1, static_cast<int>(jsRound(y1)) - p.y);
        panels.push_back(std::move(p));
        if (panels.size() >= static_cast<size_t>(kMaxPanels))
            break;
    }
    c.physW = screen.width;
    c.physH = screen.height;
    c.panels = std::move(panels);
    normalize(c);
    return true;
}

} // namespace bsl::pixelmap
