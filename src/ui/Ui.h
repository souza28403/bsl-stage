#pragma once

#include "ui/PanelContext.h"
#include "ui/PanelNames.h"

#include <string>

namespace bsl::ui {

void drawUi(PanelContext& ctx);

void requestResetLayout();

void requestFocusTab(const std::string& key);

}
