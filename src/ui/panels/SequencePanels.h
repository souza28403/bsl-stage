#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

void sequencesPrepare(PanelContext& ctx);
void drawSeqListPanel(PanelContext& ctx);
void drawSeqStepsPanel(PanelContext& ctx);
void seqUiReset();

struct SeqUiInfo
{
    int sequenceId = 0;
    int viewId = 0;
    int step = 0;
    int stepCount = 0;
    int litInStep = 0;
    int tool = 0;
    bool canUndo = false;
    bool canRedo = false;
    bool gestureOpen = false;
    bool gridEdit = false;
    float zoom = 1.0f;
    bool fit = true;
    bool ghost = true;
    bool backdrop = true;
    std::string status;
};
SeqUiInfo seqUiInfo();

}
