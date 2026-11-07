#pragma once

#include "ui/PanelContext.h"
#include "update/Markdown.h"

#include <string>
#include <vector>

namespace bsl::ui {

void openUpdateWindow(bool checkNow);
void drawUpdateWindow(PanelContext& ctx);

bool updateNoticeVisible(PanelContext& ctx, std::string* text = nullptr);
float updateNoticeWidth(PanelContext& ctx);
void drawUpdateNotice(PanelContext& ctx);

std::string updateMenuText(PanelContext& ctx);

void drawMarkdown(const std::vector<update::MdBlock>& blocks);

struct UpdateUiInfo
{
    bool windowOpen = false;
    bool noticeShown = false;
    std::string noticeText;
};
UpdateUiInfo updateUiInfo();

}
