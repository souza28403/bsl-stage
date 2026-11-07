#pragma once

#include "ui/PanelContext.h"

#include <vector>

namespace bsl::ui {

enum class MapViewKind { Input, Output };

void drawMapView(PanelContext& ctx, MapViewKind kind);

void mapHistoryObserve(PanelContext& ctx);
void mapHistoryCommit(PanelContext& ctx);
void mapEditorReset(const OutputSetup& output);

void mapDuplicateSelection(PanelContext& ctx);
void mapRemoveSelection(PanelContext& ctx);

bool isSliceSelected(const MappingSelection& sel, int slice);
void selectOnly(MappingSelection& sel, int slice);
void toggleSliceSelection(MappingSelection& sel, int slice);
void setSliceSelection(MappingSelection& sel, std::vector<int> slices, int primary);
void normalizeSelection(MappingSelection& sel, int sliceCount);

struct MapEditorInfo
{
    int undoCount = 0;
    int redoCount = 0;
    bool snap = true;
    bool grid = false;
    int gridStep = 16;
    float inputZoom = 1.0f;
    float outputZoom = 1.0f;
    bool dragging = false;
    int guides = 0;
};
MapEditorInfo mapEditorInfo();

}
