#pragma once

#include "ui/PanelContext.h"

#include <string>
#include <vector>

namespace bsl::ui {

void openCompatWindow();
void drawCompatWindow(PanelContext& ctx);

void drawNdiRuntimeNotice();

bool drawNdiSourcePicker(const std::string& current, std::string& out);

bool drawSpoutSenderPicker(const std::string& current, std::string& out);

}
