#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

void drawEnvelopePanel(PanelContext& ctx);

void openEnvelopeEditor(int envelopeId);
bool envelopeEditorTakeOpenRequest();
void envelopeEditorReset();

struct EnvelopeUiInfo
{
    int envelopeId = 0;
    int anchors = 0;
    int selected = 0;
    bool canUndo = false;
    bool canRedo = false;
    bool dragging = false;
    bool snap = true;
    std::string status;
};
EnvelopeUiInfo envelopeUiInfo(const seq::BibliotecaEnvelopes& biblioteca);

}
