#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

enum class LegalTab { License = 0, ThirdParty = 1, Privacy = 2, Terms = 3 };

void openLegalWindow(LegalTab tab = LegalTab::License);
void drawLegalWindow(PanelContext& ctx);

std::string sourceCodeUrl();
void openSourceCode();

struct LegalUiInfo
{
    bool windowOpen = false;
    int tab = 0;
    int thirdPartyCount = 0;
    bool licenseFullOpen = false;
    int openedTexts = 0;
    bool docsFound = false;
    std::string lastUrl;
};
LegalUiInfo legalUiInfo();

}
