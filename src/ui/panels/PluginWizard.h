#pragma once

#include "ui/PanelContext.h"

#include <filesystem>
#include <string>

namespace bsl::ui {

void openNewPluginWizard();
void openImportPluginWindow(const std::filesystem::path& file = {});
void openExportPluginWindow(const std::string& typeId, const std::filesystem::path& shaderFile);
void drawPluginWizard(PanelContext& ctx);

void openPluginInEditor(const std::filesystem::path& file);
void showPluginInFolder(const std::filesystem::path& file);
std::string duplicateUserPlugin(const std::filesystem::path& file);

struct PluginUiInfo
{
    bool wizardOpen = false;
    bool created = false;
    std::string createdFile;
    std::string createdId;
    std::string error;
    int rows = 0;
    bool importOpen = false;
    bool importValid = false;
    std::string importName;
    std::string importLicense;
    std::string importAuthor;
    std::string importError;
    std::string installedFolder;
    bool exportOpen = false;
    std::string exportedFile;
    std::string lastAction;
    std::string lastPath;
    std::string message;
};
PluginUiInfo pluginUiInfo();

}
