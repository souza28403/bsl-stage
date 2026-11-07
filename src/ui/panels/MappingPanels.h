#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

void validateMappingSelection(PanelContext& ctx);

std::string sliceSourceLabel(const Composition& comp, const Slice& slice);
std::string sliceLayerName(const Composition& comp, const Slice& slice);
bool sliceLayerMissing(const Composition& comp, const Slice& slice);
void setSliceSource(const Composition& comp, Slice& slice, int layerIndex);

void drawMappingListPanel(PanelContext& ctx);
void drawMappingInputPanel(PanelContext& ctx);
void drawMappingOutputPanel(PanelContext& ctx);
void drawMappingPropertiesPanel(PanelContext& ctx);

}
