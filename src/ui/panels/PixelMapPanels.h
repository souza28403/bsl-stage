#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

void pixelMapPrepare(PanelContext& ctx);

void drawPixelMapConfigPanel(PanelContext& ctx);
void drawPixelMapOverviewPanel(PanelContext& ctx);
void drawPixelMapTablePanel(PanelContext& ctx);

void pixelMapReset();
void pixelMapRelease();

struct PixelMapUiInfo
{
    int selected = -1;
    bool snap = true;
    bool dragging = false;
    int guides = 0;
    float zoom = 1.0f;
    std::string status;
};
PixelMapUiInfo pixelMapUiInfo();

}
