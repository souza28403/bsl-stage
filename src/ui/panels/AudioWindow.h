#pragma once

#include "ui/PanelContext.h"

#include <imgui.h>

namespace bsl::ui {

void openAudioWindow();
void drawAudioWindow(PanelContext& ctx);

bool audioMeter(const char* id, ImVec2 size);

}
