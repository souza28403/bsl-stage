#pragma once

#include "core/Clip.h"
#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

enum class PreviewSource {
    Clip,
    Layer,
};

struct PreviewOptions
{
    PreviewSource source = PreviewSource::Clip;
    bool checker = true;
    FitMode fit = FitMode::Fit;
};
PreviewOptions& previewOptions();

struct PreviewInfo
{
    bool layerMode = false;
    int layer = -1;
    int column = -1;
    bool hasClip = false;
    bool live = false;
    bool ownSource = false;
    bool playing = false;
    double position = 0.0;
    double duration = 0.0;
};
PreviewInfo previewInfo();

void drawOutputPanel(PanelContext& ctx);

void drawPreviewPanel(PanelContext& ctx);

int previewWantedLayerId();
void previewPanelHidden();

void releasePreview();

}
