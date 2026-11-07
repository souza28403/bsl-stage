#pragma once

#include "ui/PanelContext.h"

namespace bsl::ui {

void requestCaptureTarget(int deck, int layer, int column);

void drawCaptureTargetChooser(PanelContext& ctx);

void afterClipPlaced(const Composition& comp, int layer, int column);

}
