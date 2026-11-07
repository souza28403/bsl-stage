#pragma once

#include "ui/PanelContext.h"
#include "update/IssueReport.h"

#include <string>

namespace bsl::ui {

void openReportWindow(update::ReportKind kind);
void drawReportWindow(PanelContext& ctx);

struct ReportUiInfo
{
    bool open = false;
    int kind = 0;
    bool includeSystem = false;
    bool includeLog = false;
    size_t bodyLength = 0;
    size_t urlLength = 0;
    bool truncated = false;
    std::string lastUrl;
    std::string lastClipboard;
};
ReportUiInfo reportUiInfo();

}
