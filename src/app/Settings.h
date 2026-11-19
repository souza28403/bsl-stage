#pragma once

#include "app/PerformanceProfile.h"
#include "audio/AudioTypes.h"
#include "control/KeyBinding.h"
#include "dmx/DmxNet.h"
#include "ui/ViewState.h"
#include "update/Policy.h"

#include <string>
#include <vector>

namespace bsl {

enum class UiScaleMode { Window, System, Fixed };

enum class UiLayout { Default, BigGrid, BigOutput };
inline constexpr int kUiLayoutCount = 3;
const char* uiLayoutKey(UiLayout layout);
const char* uiLayoutLabel(UiLayout layout);

bool outputCompatFor(const std::string& setting, const std::string& vendor, const std::string& renderer);

struct Settings
{
    PerformanceProfile profile = PerformanceProfile::Balanced;
    int windowWidth = 1600;
    int windowHeight = 900;
    bool hasWindowPos = false;
    int windowX = 0;
    int windowY = 0;
    bool windowMaximized = false;
    ui::view::State view;
    std::string lastBrowserFolder;
    UiScaleMode uiScaleMode = UiScaleMode::Window;
    float uiScale = 1.0f;
    UiLayout layout = UiLayout::Default;
    std::string lastLayoutPreset;
    std::vector<std::string> isfFolders;
    std::vector<std::string> ffglFolders;
    audio::AudioConfig audio;
    int aiFps = 0;
    std::string aiDevice = "auto";
    std::string outputCompat = "auto";
    std::string controlPanelMode = "browser";
    control::KeyBindingSet keys = control::defaultAppKeys();

    dmx::NetConfig dmxNet;

    update::Prefs update;

    static constexpr size_t kMaxRecentProjects = 8;
    std::vector<std::string> recentProjects;
    void addRecentProject(const std::string& pathUtf8);
    void removeRecentProject(const std::string& pathUtf8);

    static Settings load();
    void save() const;
};

}
