#pragma once

#include "media/SourceParams.h"
#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

void drawZoneRoutingButton(PanelContext& ctx, const media::ParamSet& params, int zoneSource, const std::string& id);

std::string zoneSourceLabel(const Composition& comp, int zoneSource);

}
