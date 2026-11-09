#pragma once

#include "media/NetStream.h"
#include "media/SourceParams.h"
#include "ui/PanelContext.h"

namespace bsl::ui {

void requestNetStreamWindow(int deck, int layer, int column);

void drawNetStreamWindow(PanelContext& ctx);

bool drawNetUrlEditor(const media::Json& current, bool& reveal, media::Json& out);

void drawNetAddressRows(const media::net::Config& cfg, int maxRows);

}
