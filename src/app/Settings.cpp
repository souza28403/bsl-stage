#include "app/Settings.h"

#include "app/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace bsl {

namespace {

std::filesystem::path settingsPath()
{
    return paths::appDataDir() / L"settings.json";
}

} // namespace

const char* uiLayoutKey(UiLayout layout)
{
    switch (layout) {
    case UiLayout::BigGrid: return "bigGrid";
    case UiLayout::BigOutput: return "bigOutput";
    default: return "default";
    }
}

const char* uiLayoutLabel(UiLayout layout)
{
    switch (layout) {
    case UiLayout::BigGrid: return "Grade grande";
    case UiLayout::BigOutput: return "Saída grande";
    default: return "Padrão (media server)";
    }
}

Settings Settings::load()
{
    Settings s;
    std::ifstream file(settingsPath());
    if (!file)
        return s;

    try {
        const nlohmann::json j = nlohmann::json::parse(file);
        s.profile = profileFromKey(j.value("performanceProfile", "").c_str(), s.profile);
        s.lastBrowserFolder = j.value("browserFolder", "");
        const float savedScale = j.value("uiScale", 0.0f);
        s.uiScale = savedScale > 0.0f ? std::clamp(savedScale, 0.75f, 3.0f) : 1.0f;
        if (j.contains("uiScaleMode") && j["uiScaleMode"].is_string()) {
            const std::string mode = j["uiScaleMode"].get<std::string>();
            s.uiScaleMode = mode == "system" ? UiScaleMode::System
                            : mode == "fixed" ? UiScaleMode::Fixed
                                              : UiScaleMode::Window;
        } else {
            // arquivo antigo (so uiScale): valor fixo se o usuario escolheu um; senao o padrao novo
            s.uiScaleMode = savedScale > 0.0f ? UiScaleMode::Fixed : UiScaleMode::Window;
        }
        const std::string layoutKey = j.value("layout", std::string("default"));
        for (int i = 0; i < kUiLayoutCount; ++i)
            if (layoutKey == uiLayoutKey(static_cast<UiLayout>(i)))
                s.layout = static_cast<UiLayout>(i);
        if (j.contains("lastLayoutPreset") && j["lastLayoutPreset"].is_string())
            s.lastLayoutPreset = j["lastLayoutPreset"].get<std::string>();
        if (j.contains("isfFolders") && j["isfFolders"].is_array()) {
            for (const auto& item : j["isfFolders"])
                if (item.is_string() && !item.get<std::string>().empty() && s.isfFolders.size() < 32)
                    s.isfFolders.push_back(item.get<std::string>());
        }
        s.ffglUseResolume = j.value("ffglUseResolume", false);
        if (j.contains("ffglFolders") && j["ffglFolders"].is_array()) {
            for (const auto& item : j["ffglFolders"])
                if (item.is_string() && !item.get<std::string>().empty() && s.ffglFolders.size() < 32)
                    s.ffglFolders.push_back(item.get<std::string>());
        }
        if (j.contains("audio") && j["audio"].is_object()) {
            const auto& a = j["audio"];
            s.audio.enabled = a.value("enabled", true);
            s.audio.source = a.value("source", std::string("loopback")) == "input" ? audio::AudioSource::Input
                                                                                   : audio::AudioSource::Loopback;
            s.audio.deviceId = a.value("deviceId", std::string());
            s.audio.gain = std::clamp(a.value("gain", 1.0f), 0.1f, 4.0f);
            s.audio.gate = std::clamp(a.value("gate", 0.0f), 0.0f, 0.15f);
            s.audio.attackMs = std::clamp(a.value("attackMs", 0.0f), 0.0f, 200.0f);
            s.audio.releaseScale = std::clamp(a.value("releaseScale", 1.0f), 0.25f, 4.0f);
        }
        s.aiFps = std::clamp(j.value("aiFps", 0), 0, 60);
        s.aiDevice = j.value("aiDevice", std::string("auto"));
        if (s.aiDevice != "gpu" && s.aiDevice != "cpu")
            s.aiDevice = "auto";
        if (j.contains("recentProjects") && j["recentProjects"].is_array()) {
            for (const auto& item : j["recentProjects"]) {
                if (!item.is_string() || s.recentProjects.size() >= kMaxRecentProjects)
                    continue;
                const std::string utf8 = item.get<std::string>();
                std::error_code ec; // some o que foi apagado/movido
                if (std::filesystem::exists(std::filesystem::path(std::u8string(utf8.begin(), utf8.end())), ec))
                    s.recentProjects.push_back(utf8);
            }
        }
        if (j.contains("window")) {
            const auto& w = j["window"];
            s.windowWidth = std::clamp(w.value("width", s.windowWidth), 640, 16384);
            s.windowHeight = std::clamp(w.value("height", s.windowHeight), 360, 16384);
        }
    } catch (const nlohmann::json::exception&) {
        return Settings{};
    }
    return s;
}

void Settings::addRecentProject(const std::string& pathUtf8)
{
    removeRecentProject(pathUtf8);
    recentProjects.insert(recentProjects.begin(), pathUtf8);
    if (recentProjects.size() > kMaxRecentProjects)
        recentProjects.resize(kMaxRecentProjects);
}

void Settings::removeRecentProject(const std::string& pathUtf8)
{
    std::erase(recentProjects, pathUtf8);
}

void Settings::save() const
{
    nlohmann::json j;
    j["performanceProfile"] = settingsFor(profile).key;
    j["browserFolder"] = lastBrowserFolder;
    j["uiScaleMode"] = uiScaleMode == UiScaleMode::System  ? "system"
                       : uiScaleMode == UiScaleMode::Fixed ? "fixed"
                                                           : "window";
    j["uiScale"] = uiScale;
    j["layout"] = uiLayoutKey(layout);
    j["lastLayoutPreset"] = lastLayoutPreset;
    j["isfFolders"] = isfFolders;
    j["ffglUseResolume"] = ffglUseResolume;
    j["ffglFolders"] = ffglFolders;
    j["audio"] = {{"enabled", audio.enabled},
                  {"source", audio.source == audio::AudioSource::Input ? "input" : "loopback"},
                  {"deviceId", audio.deviceId},
                  {"gain", audio.gain},
                  {"gate", audio.gate},
                  {"attackMs", audio.attackMs},
                  {"releaseScale", audio.releaseScale}};
    j["aiFps"] = aiFps;
    j["aiDevice"] = aiDevice;
    j["recentProjects"] = recentProjects;
    j["window"] = {{"width", windowWidth}, {"height", windowHeight}};

    std::ofstream file(settingsPath());
    if (file)
        file << j.dump(2) << '\n';
}

} // namespace bsl
