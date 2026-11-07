#pragma once

#include "ui/PanelContext.h"
#include "ui/Theme.h"

namespace bsl::ui {

void drawTransportBar(PanelContext& ctx);

inline float transportBarHeight() { return dp(28.0f); }

inline int transportRows(float availWidth) { return availWidth < dp(700.0f) ? 2 : 1; }

}
