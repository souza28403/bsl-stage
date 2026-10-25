#pragma once

#include "app/PerformanceProfile.h"

#include <string>
#include <vector>

namespace bsl {

// Como a escala da interface e escolhida
//  Window: acompanha o tamanho da janela (min(largura/1600, altura/900) em pixels fisicos) - padrao
//  System: escala do Windows (DPI do monitor)
//  Fixed:  valor fixo em Settings::uiScale
enum class UiScaleMode { Window, System, Fixed };

// Layouts prontos do modo Composicao (Ver > Layout)
enum class UiLayout { Default, BigGrid, BigOutput };
inline constexpr int kUiLayoutCount = 3;
const char* uiLayoutKey(UiLayout layout);
const char* uiLayoutLabel(UiLayout layout);

struct Settings
{
    PerformanceProfile profile = PerformanceProfile::Balanced;
    int windowWidth = 1600;
    int windowHeight = 900;
    std::string lastBrowserFolder; // UTF-8; vazio = Desktop
    UiScaleMode uiScaleMode = UiScaleMode::Window;
    float uiScale = 1.0f;          // valor do modo Fixed (0.75 a 3.0)
    UiLayout layout = UiLayout::Default;

    // Projetos abertos recentemente (UTF-8, o mais novo primeiro, no maximo kMaxRecentProjects)
    static constexpr size_t kMaxRecentProjects = 8;
    std::vector<std::string> recentProjects;
    void addRecentProject(const std::string& pathUtf8); // move para o topo
    void removeRecentProject(const std::string& pathUtf8);

    // Le %APPDATA%\BSL Stage\settings.json; valores padrao se faltar ou estiver invalido
    static Settings load();
    void save() const;
};

} // namespace bsl
