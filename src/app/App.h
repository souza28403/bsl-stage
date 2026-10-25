#pragma once

#include "app/Settings.h"
#include "core/Composition.h"
#include "ui/PanelContext.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace bsl {

class Compositor;
class LiveThumbnailer;
class OutputRenderer;
class ProjectSession;
class ThumbnailCache;

// Argumentos de linha de comando
struct AppOptions
{
    std::filesystem::path demoFolder;       // --demo <pasta>
    std::filesystem::path screenshotPath;   // --screenshot <arquivo.png> <segundos>
    double screenshotSeconds = 0.0;

    // Fase 2A (testes automaticos)
    bool outputWindow = false;                    // --output-window
    bool startInMapping = false;                  // --mode mapping
    std::filesystem::path screenshotOutputPath;   // --screenshot-output <arquivo.png> <segundos>
    double screenshotOutputSeconds = 0.0;
    int canvasWidth = 0;                          // --canvas <w> <h>: muda o canvas depois de montar a saida
    int canvasHeight = 0;
    bool identify = false;                        // --identify: liga o modo Identificar
    bool testMissingMonitor = false;              // --test-missing-monitor: Screen em monitor inexistente
    int windowWidth = 0;                          // --window-size <w> <h>: tamanho da janela (nao e salvo)
    int windowHeight = 0;

    // Fase 2B: primeiro argumento posicional <projeto>.bslstage
    std::filesystem::path projectPath;

    // Fase 3A (testes): --trigger <camada> <coluna> (1 = primeira) dispara o clip depois do --demo; repetivel
    std::vector<std::pair<int, int>> triggers;
    // Escala da interface (testes): --ui-scale <f> fixa o valor (modo Fixed); --ui-mode window|system|fixed
    float uiScale = 0.0f;
    std::string uiMode;
    // --qa <pasta>: modo de teste de interface (ver app/Qa.h). Isola settings/imgui.ini/log em <pasta>/appdata.
    std::filesystem::path qaDir;
    bool openCaptureChooser = false; // --choose-target: abre a janela de alvo da captura (celula 3,2) para o screenshot
};

class App
{
public:
    explicit App(AppOptions options = {});
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();

private:
    using Clock = std::chrono::steady_clock;

    bool init();
    void shutdown();
    void frame();
    void updateStats(Clock::time_point frameStart);
    void limitFrameRate(Clock::time_point frameStart) const;
    void applyProfile();
    void applyDecoderOptions();
    void setWindowIcon();
    void setupDemo();
    void saveScreenshot();
    void saveOutputScreenshot();
    void setupOutputTest();
    void syncCanvasSize();
    void afterProjectLoaded();
    void applySwapInterval();
    void updateLiveThumbs();
    void updateUiScale();
    void runQaCommands();
    std::string qaStateJson() const;
    void saveScreenshotTo(const std::filesystem::path& file);
    static void onDrop(GLFWwindow* window, int count, const char** paths);
    static void onClose(GLFWwindow* window);

    AppOptions options_;
    GLFWwindow* window_ = nullptr;
    bool imguiReady_ = false;

    Settings settings_;
    Composition composition_;
    Selection selection_;
    GlInfo glInfo_;
    FrameStats stats_;
    DropRequest drop_;
    bool quitRequested_ = false;
    OutputSetup output_;
    MappingSelection mapping_;
    AppMode mode_ = AppMode::Composition;
    std::unique_ptr<Compositor> compositor_;
    std::unique_ptr<OutputRenderer> outputs_;
    int swapInterval_ = -1;
    bool screenshotDone_ = false;
    bool outputShotDone_ = false;
    std::filesystem::path qaShotPath_;
    bool qaOn_ = false;
    std::unique_ptr<ThumbnailCache> thumbs_;
    std::unique_ptr<LiveThumbnailer> liveThumbs_;
    std::string iniPath_;
    std::unique_ptr<ProjectSession> project_; // por ultimo: referencia os membros acima

    Clock::time_point lastFrameStart_{};
    Clock::time_point startTime_{};
    Clock::time_point lastUpdate_{};
    float smoothedInterval_ = 1.0f / 60.0f;
    float smoothedWork_ = 0.0f;
    int frameCount_ = 0;
};

} // namespace bsl
