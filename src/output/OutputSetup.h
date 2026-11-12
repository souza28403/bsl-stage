#pragma once

#include <string>
#include <vector>

namespace bsl {

struct Point2
{
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Point2&) const = default;
};

struct RectI
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const RectI&) const = default;
};

enum Corner { kCornerTL = 0, kCornerTR = 1, kCornerBR = 2, kCornerBL = 3 };

enum EdgeSide { kEdgeLeft = 0, kEdgeTop = 1, kEdgeRight = 2, kEdgeBottom = 3 };

struct SoftEdge
{
    float width = 0.0f;
    float gamma = 1.0f;

    bool operator==(const SoftEdge&) const = default;
};

enum ZoneFilter { kZoneAll = 0, kZoneFront = 1, kZoneMiddle = 2, kZoneBack = 3, kZone4 = 4 };
inline constexpr int kZoneFilterCount = 5;
inline constexpr int kZoneSourceMaster = -1;

inline const char* zoneFilterName(int zone)
{
    switch (zone) {
    case kZoneFront: return "Frente";
    case kZoneMiddle: return "Meio";
    case kZoneBack: return "Fundo";
    case kZone4: return "Zona 4";
    default: return "Todas";
    }
}

enum SliceSource { kSliceFromComposition = 0, kSliceFromLayer = 1 };

struct Slice
{
    std::string name;
    bool enabled = true;

    int source = kSliceFromComposition;
    int layerId = 0;
    bool includeMaster = false;
    bool applyLayerTransform = false;

    RectI input;
    Point2 corners[4];
    bool flipH = false;
    bool flipV = false;
    float opacity = 1.0f;
    float gain[3] = {1.0f, 1.0f, 1.0f};
    float brightness = 0.0f;
    SoftEdge edges[4];
    int zFilter = kZoneAll;
    int zSource = kZoneSourceMaster;
    int uid = 0;

    bool operator==(const Slice&) const = default;
};

enum class OutputKind { Disabled, Monitor, Window, Spout, Ndi };

struct Screen
{
    int id = 0;
    std::string name;
    bool enabled = true;

    OutputKind kind = OutputKind::Disabled;
    int monitorIndex = -1;
    std::string monitorName;
    std::string spoutName;
    std::string ndiName;
    bool ndiBgra = false;

    int width = 1920;
    int height = 1080;

    std::vector<Slice> slices;

    bool live() const { return enabled && kind != OutputKind::Disabled; }

    bool operator==(const Screen&) const = default;
};

struct MonitorInfo
{
    int index = 0;
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool primary = false;
};

struct OutputSetup
{
    std::vector<Screen> screens;
    int nextScreenId = 1;
    int nextSliceUid = 1;

    bool operator==(const OutputSetup&) const = default;

    static OutputSetup createDefault(int canvasW, int canvasH, const std::vector<MonitorInfo>& monitors);

    int addScreen(int canvasW, int canvasH);
    int addSlice(int screenIndex, int canvasW, int canvasH);
    int duplicateScreen(int screenIndex);
    int duplicateSlice(int screenIndex, int sliceIndex);
    void removeScreen(int screenIndex);
    void removeSlice(int screenIndex, int sliceIndex);

    int findScreenIndex(int id) const;

    void rescaleInputs(int oldW, int oldH, int newW, int newH);
};

struct LayerViewRequest
{
    int layerId = 0;
    bool includeMaster = false;
    bool applyTransform = false;

    bool operator==(const LayerViewRequest&) const = default;
};

std::vector<LayerViewRequest> collectLayerViews(const OutputSetup& setup, int previewScreenId);

std::vector<std::string> slicesLinkedToLayer(const OutputSetup& setup, int layerId);

bool ensureSliceUids(OutputSetup& setup);

Slice makeFullSlice(const std::string& name, int canvasW, int canvasH, int screenW, int screenH);

void fitOutputToInput(Slice& slice);
void resetWarp(Slice& slice);

const char* outputKindName(OutputKind kind);

}
