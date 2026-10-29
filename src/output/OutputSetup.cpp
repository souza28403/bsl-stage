#include "output/OutputSetup.h"

#include <algorithm>
#include <cmath>

namespace bsl {

namespace {

bool validSlice(const OutputSetup& setup, int s, int i)
{
    if (s < 0 || s >= static_cast<int>(setup.screens.size()))
        return false;
    return i >= 0 && i < static_cast<int>(setup.screens[static_cast<size_t>(s)].slices.size());
}

template <typename T, typename GetName>
std::string uniqueName(const std::vector<T>& items, const std::string& base, GetName getName)
{
    auto taken = [&](const std::string& n) {
        return std::any_of(items.begin(), items.end(), [&](const T& it) { return getName(it) == n; });
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + " " + std::to_string(n);
        if (!taken(candidate))
            return candidate;
    }
}

std::string uniqueScreenName(const OutputSetup& setup, const std::string& base)
{
    return uniqueName(setup.screens, base, [](const Screen& s) -> const std::string& { return s.name; });
}

std::string uniqueSliceName(const Screen& screen, const std::string& base)
{
    return uniqueName(screen.slices, base, [](const Slice& s) -> const std::string& { return s.name; });
}

} // namespace

Slice makeFullSlice(const std::string& name, int canvasW, int canvasH, int screenW, int screenH)
{
    Slice s;
    s.name = name;
    s.input = {0, 0, canvasW, canvasH};
    s.corners[kCornerTL] = {0.0f, 0.0f};
    s.corners[kCornerTR] = {static_cast<float>(screenW), 0.0f};
    s.corners[kCornerBR] = {static_cast<float>(screenW), static_cast<float>(screenH)};
    s.corners[kCornerBL] = {0.0f, static_cast<float>(screenH)};
    return s;
}

void fitOutputToInput(Slice& slice)
{
    const float x = static_cast<float>(slice.input.x);
    const float y = static_cast<float>(slice.input.y);
    const float w = static_cast<float>(slice.input.w);
    const float h = static_cast<float>(slice.input.h);
    slice.corners[kCornerTL] = {x, y};
    slice.corners[kCornerTR] = {x + w, y};
    slice.corners[kCornerBR] = {x + w, y + h};
    slice.corners[kCornerBL] = {x, y + h};
}

void resetWarp(Slice& slice)
{
    float minX = slice.corners[0].x, maxX = minX;
    float minY = slice.corners[0].y, maxY = minY;
    for (const Point2& c : slice.corners) {
        minX = std::min(minX, c.x);
        maxX = std::max(maxX, c.x);
        minY = std::min(minY, c.y);
        maxY = std::max(maxY, c.y);
    }
    slice.corners[kCornerTL] = {minX, minY};
    slice.corners[kCornerTR] = {maxX, minY};
    slice.corners[kCornerBR] = {maxX, maxY};
    slice.corners[kCornerBL] = {minX, maxY};
}

const char* outputKindName(OutputKind kind)
{
    switch (kind) {
    case OutputKind::Monitor: return "Monitor";
    case OutputKind::Window: return "Janela";
    case OutputKind::Spout: return "Spout";
    case OutputKind::Ndi: return "NDI";
    default: return "Desativado";
    }
}

OutputSetup OutputSetup::createDefault(int canvasW, int canvasH, const std::vector<MonitorInfo>& monitors)
{
    OutputSetup setup;
    const int idx = setup.addScreen(canvasW, canvasH);
    Screen& screen = setup.screens[static_cast<size_t>(idx)];

    for (const MonitorInfo& m : monitors) {
        if (m.primary)
            continue;
        screen.kind = OutputKind::Monitor;
        screen.monitorIndex = m.index;
        screen.monitorName = m.name;
        screen.width = m.width;
        screen.height = m.height;
        screen.slices.front() = makeFullSlice(screen.slices.front().name, canvasW, canvasH, m.width, m.height);
        break;
    }
    return setup;
}

int OutputSetup::addScreen(int canvasW, int canvasH)
{
    Screen s;
    s.id = nextScreenId++;
    s.name = uniqueScreenName(*this, "Tela " + std::to_string(screens.size() + 1));
    s.spoutName = "BSL Stage - " + s.name;
    s.ndiName = s.spoutName;
    s.width = canvasW;
    s.height = canvasH;
    s.slices.push_back(makeFullSlice("Slice 1", canvasW, canvasH, s.width, s.height));
    screens.push_back(std::move(s));
    return static_cast<int>(screens.size()) - 1;
}

int OutputSetup::addSlice(int screenIndex, int canvasW, int canvasH)
{
    if (screenIndex < 0 || screenIndex >= static_cast<int>(screens.size()))
        return -1;
    Screen& s = screens[static_cast<size_t>(screenIndex)];
    s.slices.push_back(makeFullSlice(uniqueSliceName(s, "Slice " + std::to_string(s.slices.size() + 1)),
                                     canvasW, canvasH, s.width, s.height));
    return static_cast<int>(s.slices.size()) - 1;
}

int OutputSetup::duplicateScreen(int screenIndex)
{
    if (screenIndex < 0 || screenIndex >= static_cast<int>(screens.size()))
        return -1;
    Screen copy = screens[static_cast<size_t>(screenIndex)];
    copy.id = nextScreenId++;
    copy.name = uniqueScreenName(*this, copy.name + " cópia");
    copy.spoutName = "BSL Stage - " + copy.name;
    copy.ndiName = copy.spoutName;
    copy.kind = OutputKind::Disabled;
    screens.insert(screens.begin() + screenIndex + 1, std::move(copy));
    return screenIndex + 1;
}

int OutputSetup::duplicateSlice(int screenIndex, int sliceIndex)
{
    if (!validSlice(*this, screenIndex, sliceIndex))
        return -1;
    Screen& s = screens[static_cast<size_t>(screenIndex)];
    Slice copy = s.slices[static_cast<size_t>(sliceIndex)];
    copy.name = uniqueSliceName(s, copy.name + " cópia");
    s.slices.insert(s.slices.begin() + sliceIndex + 1, std::move(copy));
    return sliceIndex + 1;
}

void OutputSetup::removeScreen(int screenIndex)
{
    if (screenIndex >= 0 && screenIndex < static_cast<int>(screens.size()))
        screens.erase(screens.begin() + screenIndex);
}

void OutputSetup::removeSlice(int screenIndex, int sliceIndex)
{
    if (!validSlice(*this, screenIndex, sliceIndex))
        return;
    auto& slices = screens[static_cast<size_t>(screenIndex)].slices;
    slices.erase(slices.begin() + sliceIndex);
}

int OutputSetup::findScreenIndex(int id) const
{
    for (size_t i = 0; i < screens.size(); ++i)
        if (screens[i].id == id)
            return static_cast<int>(i);
    return -1;
}

void OutputSetup::rescaleInputs(int oldW, int oldH, int newW, int newH)
{
    if (oldW <= 0 || oldH <= 0 || (oldW == newW && oldH == newH))
        return;
    const double kx = static_cast<double>(newW) / oldW;
    const double ky = static_cast<double>(newH) / oldH;
    for (Screen& screen : screens) {
        for (Slice& s : screen.slices) {
            const RectI r = s.input;
            const int x0 = std::clamp(static_cast<int>(std::lround(r.x * kx)), 0, newW - 1);
            const int y0 = std::clamp(static_cast<int>(std::lround(r.y * ky)), 0, newH - 1);
            const int x1 = std::clamp(static_cast<int>(std::lround((r.x + r.w) * kx)), x0 + 1, newW);
            const int y1 = std::clamp(static_cast<int>(std::lround((r.y + r.h) * ky)), y0 + 1, newH);
            s.input = {x0, y0, x1 - x0, y1 - y0};
        }
    }
}

} // namespace bsl
