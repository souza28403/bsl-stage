#pragma once

#include "app/FrameWatch.h"
#include "app/Settings.h"
#include "control/KeyService.h"
#include "core/Composition.h"
#include "dmx/DmxService.h"
#include "seq/Envelope.h"
#include "ui/PanelContext.h"
#include "update/Service.h"

#include <chrono>
#include <cstdint>
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

struct AppOptions
{
    std::filesystem::path demoFolder;
    std::filesystem::path screenshotPath;
    double screenshotSeconds = 0.0;

    bool outputWindow = false;
    bool startInMapping = false;
    bool startInPixelMap = false;
    bool startInSequences = false;
    std::filesystem::path screenshotOutputPath;
    double screenshotOutputSeconds = 0.0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    bool identify = false;
    bool testMissingMonitor = false;
    int windowWidth = 0;
    int windowHeight = 0;

    std::filesystem::path projectPath;

    std::vector<std::pair<int, int>> triggers;
    float uiScale = 0.0f;
    std::string uiMode;
    std::filesystem::path qaDir;
    bool openCaptureChooser = false;
    std::vector<std::filesystem::path> pluginFolders;
    std::string fxOpen;
    std::string focusTab;
    std::string dmxDemo;
    std::filesystem::path fakeRelease;
    std::filesystem::path ndiLib;
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
    void applyFullscreen(bool on);
    void runQaCommands();
    void setupKeyHooks();
    void setupDmxDemo();
    void setupUpdates();
    void absorverEnvelopesDoProjeto();
    void publicarSequencias();
    void updateTick();
    void pollSystemHotkeys();
    std::string qaStateJson() const;
    void saveScreenshotTo(const std::filesystem::path& file);
    static void onDrop(GLFWwindow* window, int count, const char** paths);
    static void onClose(GLFWwindow* window);

    AppOptions options_;
    GLFWwindow* window_ = nullptr;
    bool imguiReady_ = false;

    Settings settings_;
    Composition composition_;
    seq::BibliotecaEnvelopes envelopes_;
    std::uint64_t versaoSlicesAncoras_ = 0;
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
    FrameWatch frameWatch_;
    bool screenshotDone_ = false;
    bool outputShotDone_ = false;
    std::filesystem::path qaShotPath_;
    bool qaOn_ = false;
    std::unique_ptr<ThumbnailCache> thumbs_;
    std::unique_ptr<LiveThumbnailer> liveThumbs_;
    std::string iniPath_;
    bool fullscreen_ = false;
    bool fsMaximized_ = false;
    int fsX_ = 0, fsY_ = 0, fsW_ = 0, fsH_ = 0;
    control::KeyService keyService_;
    dmx::Service dmx_;
    update::Service updates_;
    double nextAutoCheck_ = 0.0;
    bool restartSawModal_ = false;
    bool updaterLaunched_ = false;
    std::unique_ptr<ProjectSession> project_;

    Clock::time_point lastFrameStart_{};
    Clock::time_point startTime_{};
    Clock::time_point lastUpdate_{};
    float smoothedInterval_ = 1.0f / 60.0f;
    float smoothedWork_ = 0.0f;
    int frameCount_ = 0;
};

}
