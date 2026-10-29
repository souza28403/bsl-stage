#pragma once

#include "app/PerformanceProfile.h"
#include "audio/AudioTypes.h"

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
    // Ultimo preset de layout do usuario aplicado (Ver > Layout > Salvar layout atual); vazio = nenhum (vale o layout
    // pronto acima). Restaurado ao abrir o app.
    std::string lastLayoutPreset;
    std::vector<std::string> isfFolders; // pastas ISF extras (UTF-8), alem de Documents\BSL Stage\ISF
    // Plugins FFGL: Documents\BSL Stage\Plugins sempre; o Extra Effects do Resolume so se existir e estiver ligado
    bool ffglUseResolume = false; // opcional: o cliente normalmente nao tem Resolume
    std::vector<std::string> ffglFolders; // pastas de plugins extras (UTF-8)
    // Analise de audio (Ver > Audio): loopback do PC ou entrada, ganho, gate, ataque/release
    audio::AudioConfig audio;
    // IA local opcional (Ver > IA): taxa de inferencia por fonte (0 = a do perfil: Eco desligada, Balanceado 15, Maximo 30)
    // e onde rodar os modelos ("auto" = GPU com fallback de CPU, "gpu", "cpu"). A IA fica sempre desligada no perfil Eco.
    int aiFps = 0;
    std::string aiDevice = "auto";

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
