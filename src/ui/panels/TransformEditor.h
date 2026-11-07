#pragma once

#include "ui/PanelContext.h"

#include <imgui.h>

#include <string>

namespace bsl::ui {

enum class TransformPanel { Output = 0, Preview = 1 };

bool& transformEditOn(TransformPanel panel);

bool transformToggleButton(TransformPanel panel, ImVec2 size);

void drawTransformTools(PanelContext& ctx, TransformPanel panel);

void drawTransformOverlay(PanelContext& ctx, TransformPanel panel, ImVec2 a, ImVec2 b);

std::string transformCaption(const PanelContext& ctx);

void drawTransformPresetItems(PanelContext& ctx, int layer);

void drawTransformFields(PanelContext& ctx, int layerIndex);

void drawClipTransformToggle(PanelContext& ctx, Clip& clip, int layerIndex);

void transformHistoryObserve(PanelContext& ctx);
void transformEditorReset(const Composition& comp);

struct TransformEditorInfo
{
    bool onOutput = false;
    bool onPreview = false;
    bool crop = false;
    bool snap = true;
    int undo = 0;
    int redo = 0;
    bool dragging = false;
    std::string drag;
    int guides = 0;
};
TransformEditorInfo transformEditorInfo();

}
