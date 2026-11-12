#include "app/App.h"

#include "app/FfglSetup.h"
#include "app/IsfSetup.h"
#include "app/Log.h"
#include "app/Paths.h"
#include "app/ProjectSession.h"
#include "app/Qa.h"
#include "app/Version.h"
#include "legal/Notices.h"
#include "ai/AiEngine.h"
#include "audio/AudioAnalyzer.h"
#include "ffgl/FfglCatalog.h"
#include "fx/IsfLibrary.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslSources.h"
#include "fx/bsl/PixelMapFeed.h"
#include "fx/bsl/SequenceFeed.h"
#include "fx/bsl/SliceFeed.h"
#include "media/DocThumb.h"
#include "media/SpoutSource.h"
#include "media/WorkerHost.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiRuntime.h"
#include "output/OutputRenderer.h"
#include "seq/EnvelopeStore.h"
#include "seq/SeqModel.h"
#include "render/Compositor.h"
#include "render/LiveThumb.h"
#include "render/TextureLoader.h"
#include "render/ThumbnailCache.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/LayoutUi.h"
#include "ui/KeysUi.h"
#include "ui/Ui.h"
#include "ui/ViewUi.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/LegalWindow.h"
#include "ui/panels/PluginWizard.h"
#include "ui/panels/MapView.h"
#include "ui/panels/PixelMapPanels.h"
#include "ui/panels/EnvelopeEditor.h"
#include "ui/panels/SequencePanels.h"
#include "ui/panels/PreviewPanel.h"
#include "ui/panels/ReportWindow.h"
#include "ui/panels/UpdateUi.h"
#include "update/Http.h"
#include "update/Policy.h"
#include "ui/panels/TabPanels.h"
#include "ui/panels/TransformEditor.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <stb_image_write.h>

#include <windows.h>
#include <mmsystem.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <exception>
#include <fstream>
#include <thread>
#include <vector>

namespace bsl {

namespace {

constexpr const char* kWindowTitle = "BSL Stage";
constexpr int kOutputWindowUiFps = 60;

void glfwErrorCallback(int code, const char* description)
{
    std::fprintf(stderr, "GLFW erro %d: %s\n", code, description);
}

struct DropFilesHeader
{
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
};

const char* glString(GLenum name)
{
    const auto* s = reinterpret_cast<const char*>(glGetString(name));
    return s ? s : "?";
}

RgbaImage squareIcon(const RgbaImage& src, int size)
{
    const double s = std::min(static_cast<double>(size) / src.width, static_cast<double>(size) / src.height);
    const int w = std::max(1, static_cast<int>(src.width * s));
    const int h = std::max(1, static_cast<int>(src.height * s));
    const RgbaImage scaled = resizeImage(src, w, h);

    RgbaImage out;
    out.width = out.height = size;
    out.pixels.assign(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 0);
    const int x0 = (size - w) / 2;
    const int y0 = (size - h) / 2;
    for (int y = 0; y < h; ++y)
        std::copy_n(&scaled.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) * 4],
                    static_cast<size_t>(w) * 4,
                    &out.pixels[(static_cast<size_t>(y + y0) * static_cast<size_t>(size) + static_cast<size_t>(x0)) * 4]);
    return out;
}

bool writePng(const std::filesystem::path& path, int w, int h, std::vector<unsigned char>& rgba)
{
    for (size_t i = 3; i < rgba.size(); i += 4)
        rgba[i] = 255;

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        log::warn("Não foi possível salvar a captura: " + paths::toUtf8(path));
        return false;
    }
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size);
        },
        &file, w, h, 4, rgba.data(), w * 4);
    log::info("Captura salva em " + paths::toUtf8(path));
    return true;
}

GLFWmonitor* monitorOfWindow(GLFWwindow* window)
{
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long long bestArea = -1;
    for (int i = 0; monitors && i < count; ++i) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode)
            continue;
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const long long ox = std::max(0, std::min(wx + ww, mx + mode->width) - std::max(wx, mx));
        const long long oy = std::max(0, std::min(wy + wh, my + mode->height) - std::max(wy, my));
        if (ox * oy > bestArea) {
            bestArea = ox * oy;
            best = monitors[i];
        }
    }
    return best;
}

bool windowPosVisible(int x, int y, int w)
{
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    const int px = x + std::min(w / 2, 200);
    const int py = y + 12;
    for (int i = 0; monitors && i < count; ++i) {
        int ax = 0, ay = 0, aw = 0, ah = 0;
        glfwGetMonitorWorkarea(monitors[i], &ax, &ay, &aw, &ah);
        if (px >= ax && px < ax + aw && py >= ay && py < ay + ah)
            return true;
    }
    return false;
}

}

App::App(AppOptions options) : options_(std::move(options))
{
    if (!options_.ndiLib.empty())
        ndi::Runtime::setLibraryOverride(options_.ndiLib);
}

App::~App()
{
    shutdown();
}

int App::run()
{
    if (!init())
        return 1;

    startTime_ = lastFrameStart_ = lastUpdate_ = Clock::now();
    frameWatch_.start();
    while (!glfwWindowShouldClose(window_)) {
        const auto frameStart = Clock::now();
        frameWatch_.enter(FrameWatch::Events);
        pollSystemHotkeys();
        glfwPollEvents();

        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            frameWatch_.enter(FrameWatch::Idle);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lastUpdate_ = Clock::now();
            continue;
        }

        updateStats(frameStart);
        frame();

        smoothedWork_ += 0.1f * (std::chrono::duration<float>(Clock::now() - frameStart).count()
                                 - smoothedWork_);
        frameWatch_.enter(FrameWatch::Limiter);
        limitFrameRate(frameStart);
    }

    frameWatch_.stop();
    shutdown();
    return 0;
}

bool App::init()
{
    if (!options_.qaDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options_.qaDir, ec);
        paths::setAppDataOverride(options_.qaDir / L"appdata");
        qaOn_ = true;
    }
    settings_ = Settings::load();
    envelopes_ = seq::carregarBiblioteca(seq::caminhoBiblioteca());
    if (options_.uiMode == "window")
        settings_.uiScaleMode = UiScaleMode::Window;
    else if (options_.uiMode == "system")
        settings_.uiScaleMode = UiScaleMode::System;
    else if (options_.uiMode == "fixed")
        settings_.uiScaleMode = UiScaleMode::Fixed;
    if (options_.uiScale > 0.0f) {
        settings_.uiScale = std::clamp(options_.uiScale, 0.75f, 3.0f);
        settings_.uiScaleMode = UiScaleMode::Fixed;
    }
    setupUpdates();
    ui::viewBind(settings_);
    composition_ = Composition::createDefault();
    {
        Settings ffglSettings = settings_;
        for (const std::filesystem::path& dir : options_.pluginFolders)
            ffglSettings.ffglFolders.push_back(paths::toUtf8(dir));
        configureFfglCatalog(ffglSettings);
    }
    configureIsfLibrary(settings_);
    audio::AudioAnalyzer::instance().setConfig(settings_.audio);
    audio::AudioAnalyzer::instance().start();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    const bool sizeOverride = options_.windowWidth > 0 && options_.windowHeight > 0;
    if (!sizeOverride) {
        if (settings_.windowMaximized)
            glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
        if (settings_.hasWindowPos && windowPosVisible(settings_.windowX, settings_.windowY, settings_.windowWidth)) {
            glfwWindowHint(GLFW_POSITION_X, settings_.windowX);
            glfwWindowHint(GLFW_POSITION_Y, settings_.windowY);
        }
    }
    window_ = glfwCreateWindow(sizeOverride ? options_.windowWidth : settings_.windowWidth,
                               sizeOverride ? options_.windowHeight : settings_.windowHeight, kWindowTitle,
                               nullptr, nullptr);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
    glfwWindowHint(GLFW_POSITION_X, GLFW_ANY_POSITION);
    glfwWindowHint(GLFW_POSITION_Y, GLFW_ANY_POSITION);
    if (!window_) {
        std::fprintf(stderr, "Nao foi possivel criar a janela (OpenGL 3.3 core).\n");
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, &App::onDrop);
    glfwSetWindowCloseCallback(window_, &App::onClose);

    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "Falha ao carregar as funcoes OpenGL (glad).\n");
        return false;
    }
    glInfo_.version = glString(GL_VERSION);
    glInfo_.gpu = glString(GL_RENDERER);

    timeBeginPeriod(1);

    try {
        compositor_ = std::make_unique<Compositor>(composition_.width, composition_.height);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erro ao criar o compositor: %s\n", e.what());
        return false;
    }
    thumbs_ = std::make_unique<ThumbnailCache>();
    liveThumbs_ = std::make_unique<LiveThumbnailer>();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoDecoration = false;
    io.ConfigViewportsNoTaskBarIcon = false;
    iniPath_ = paths::toUtf8(paths::appDataDir() / L"imgui.ini");
    io.IniFilename = iniPath_.c_str();
    io.ConfigDebugHighlightIdConflicts = false;
    if (qaOn_)
        qa::init(options_.qaDir);

    ui::loadFont(io);
    ui::applyTheme();
    ui::loadBrand();
    setWindowIcon();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    imguiReady_ = true;

    try {
        outputs_ = std::make_unique<OutputRenderer>(window_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erro ao criar o renderer de saída: %s\n", e.what());
        return false;
    }
    output_ = OutputSetup::createDefault(composition_.width, composition_.height, outputs_->monitors());
    mode_ = options_.startInMapping ? AppMode::Mapping : options_.startInPixelMap ? AppMode::PixelMap
            : options_.startInSequences ? AppMode::Sequences : AppMode::Composition;
    if (options_.outputWindow)
        setupOutputTest();
    if (options_.testMissingMonitor && !output_.screens.empty()) {
        Screen& s = output_.screens.front();
        s.kind = OutputKind::Monitor;
        s.monitorIndex = 7;
        s.monitorName = "Monitor Fantasma";
    }
    outputs_->setIdentify(options_.identify);
    if (options_.canvasWidth > 0 && options_.canvasHeight > 0) {
        composition_.width = options_.canvasWidth;
        composition_.height = options_.canvasHeight;
    }

    applyProfile();
    applyDecoderOptions();
    if (!options_.demoFolder.empty())
        setupDemo();
    if (!options_.focusTab.empty())
        ui::requestFocusTab(options_.focusTab);
    if (!options_.fxOpen.empty())
        ui::setEffectsPanelOnlyOpen(options_.fxOpen);
    if (!options_.dmxDemo.empty())
        setupDmxDemo();

    project_ = std::make_unique<ProjectSession>(
        ProjectSession::Targets{composition_, output_, selection_, mapping_, settings_},
        [this](int w, int h) { return OutputSetup::createDefault(w, h, outputs_->monitors()); });
    project_->setWindow(window_);
    setupKeyHooks();
    const bool captureRun = !options_.screenshotPath.empty() || !options_.screenshotOutputPath.empty();
    if (!captureRun)
        project_->enableCrashRecovery(options_.projectPath.empty());
    project_->markClean();
    if (!options_.projectPath.empty()) {
        project_->openNow(options_.projectPath);
        project_->takeLoaded();
        afterProjectLoaded();
        for (const auto& [layer, column] : options_.triggers)
            composition_.trigger(layer - 1, column - 1);
        if (!options_.triggers.empty())
            selection_ = {Selection::Kind::Clip, options_.triggers.back().first - 1, options_.triggers.back().second - 1};
    }
    return true;
}

void App::shutdown()
{
    const bool testRun = qaOn_ || !options_.screenshotPath.empty() || !options_.screenshotOutputPath.empty();
    updates_.shutdown();
    if (qaOn_) {
        qa::finish();
        qaOn_ = false;
    }
    ffgl::Catalog::instance().shutdown();
    audio::AudioAnalyzer::instance().stop();
    dmx_.shutdown();
    if (project_)
        project_->shutdown();

    if (window_)
        outputs_.reset();
    ndi::Finder::shutdownShared();

    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }

    if (window_) {
        if (!glfwGetWindowAttrib(window_, GLFW_ICONIFIED) && !(options_.windowWidth > 0 && options_.windowHeight > 0)) {
            if (fullscreen_) {
                settings_.windowWidth = fsW_;
                settings_.windowHeight = fsH_;
                settings_.windowX = fsX_;
                settings_.windowY = fsY_;
                settings_.hasWindowPos = true;
                settings_.windowMaximized = fsMaximized_;
            } else if (glfwGetWindowAttrib(window_, GLFW_MAXIMIZED)) {
                settings_.windowMaximized = true;
            } else {
                settings_.windowMaximized = false;
                glfwGetWindowSize(window_, &settings_.windowWidth, &settings_.windowHeight);
                glfwGetWindowPos(window_, &settings_.windowX, &settings_.windowY);
                settings_.hasWindowPos = true;
            }
        }
        settings_.save();
    }

    if (window_) {
        ui::releasePreview();
        ai::AiEngine::instance().shutdown();
        composition_ = Composition{};
        thumbs_.reset();
        liveThumbs_.reset();
        media::WorkerHost::waitAllStopped(8000);
        ui::pixelMapRelease();
        ui::unloadBrand();
        compositor_.reset();
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        timeEndPeriod(1);
    }

    if (!testRun && !updaterLaunched_ && updates_.readyToInstall()) {
        updaterLaunched_ = true;
        std::string error;
        if (updates_.launchInstallerOnExit(updates_.restartRequested(), &error))
            log::info("Atualização: instalador iniciado");
        else
            log::warn("Atualização: não foi possível iniciar a instalação (" + error + ")");
    }
}

void App::applyProfile()
{
    applySwapInterval();
}

void App::applySwapInterval()
{
    const bool outputWindows = outputs_ && outputs_->hasWindowOutput();
    const int want = (!outputWindows && settingsFor(settings_.profile).uiFps == 0) ? 1 : 0;
    if (want != swapInterval_) {
        glfwSwapInterval(want);
        swapInterval_ = want;
    }
}

void App::setupOutputTest()
{
    const int cw = composition_.width, ch = composition_.height;
    output_ = OutputSetup{};
    const int idx = output_.addScreen(cw, ch);
    Screen& screen = output_.screens[static_cast<size_t>(idx)];
    screen.kind = OutputKind::Window;
    screen.width = 1280;
    screen.height = 720;
    screen.slices.clear();

    Slice a = makeFullSlice("Slice A", cw, ch, screen.width, screen.height);
    a.input = {0, 0, cw / 2, ch};
    a.corners[kCornerTL] = {0.0f, 0.0f};
    a.corners[kCornerTR] = {640.0f, 0.0f};
    a.corners[kCornerBR] = {640.0f, 720.0f};
    a.corners[kCornerBL] = {0.0f, 720.0f};

    Slice b = makeFullSlice("Slice B", cw, ch, screen.width, screen.height);
    b.input = {cw / 2, 0, cw - cw / 2, ch};
    b.corners[kCornerTL] = {700.0f, 60.0f};
    b.corners[kCornerTR] = {1250.0f, 0.0f};
    b.corners[kCornerBR] = {1200.0f, 700.0f};
    b.corners[kCornerBL] = {660.0f, 720.0f};
    b.edges[kEdgeLeft].width = 24.0f;

    screen.slices.push_back(a);
    screen.slices.push_back(b);
    mapping_ = {0, 1, -1};
}

void App::syncCanvasSize()
{
    composition_.width = std::clamp(composition_.width, 16, 8192);
    composition_.height = std::clamp(composition_.height, 16, 8192);
    const int oldW = compositor_->width(), oldH = compositor_->height();
    if (composition_.width == oldW && composition_.height == oldH)
        return;
    try {
        compositor_->resize(composition_.width, composition_.height);
    } catch (const std::exception& e) {
        log::warn(std::string("Não foi possível redimensionar o canvas: ") + e.what());
        composition_.width = oldW;
        composition_.height = oldH;
        return;
    }
    output_.rescaleInputs(oldW, oldH, composition_.width, composition_.height);
    log::info("Canvas: " + std::to_string(composition_.width) + "x" + std::to_string(composition_.height));
}

void App::afterProjectLoaded()
{
    try {
        if (composition_.width != compositor_->width() || composition_.height != compositor_->height())
            compositor_->resize(composition_.width, composition_.height);
    } catch (const std::exception& e) {
        log::warn(std::string("Não foi possível ajustar o canvas do projeto: ") + e.what());
        composition_.width = compositor_->width();
        composition_.height = compositor_->height();
    }
    applyDecoderOptions();
    ui::mapEditorReset(output_);
    ui::transformEditorReset(composition_);
    ui::pixelMapReset();
    ui::seqUiReset();
    ui::envelopeEditorReset();
    dmx_.engine().reset();
    absorverEnvelopesDoProjeto();
    versaoSlicesAncoras_ = 0;
}

void App::absorverEnvelopesDoProjeto()
{
    if (composition_.envelopes.empty())
        return;
    const seq::BibliotecaEnvelopes antes = envelopes_;
    const std::map<int, int> mapa = seq::mesclarNaBiblioteca(envelopes_, composition_.envelopes);
    composition_.remapearEnvelopes(mapa);
    composition_.envelopes.clear();
    if (envelopes_.lista.size() != antes.lista.size() || envelopes_.proximoId != antes.proximoId)
        seq::salvarBiblioteca(envelopes_, seq::caminhoBiblioteca());
}

void App::publicarSequencias()
{
    std::vector<seq::Envelope> usados;
    for (int id : composition_.envelopesUsados())
        if (const seq::Envelope* e = seq::acharEnvelope(envelopes_, id))
            usados.push_back(*e);
    if (usados != composition_.envelopes)
        composition_.envelopes = std::move(usados);

    auto& feed = fx::bsllight::SequenceFeed::instance();
    const std::uint64_t versaoSlices = fx::bsllight::SliceFeed::instance().version();
    bool mudou = feed.set(composition_.sequences, envelopes_.lista);
    if (!composition_.sequences.vazio() && (mudou || versaoSlices != versaoSlicesAncoras_)) {
        seq::atualizarAncoras(composition_.sequences, output_);
        feed.set(composition_.sequences, envelopes_.lista);
    }
    versaoSlicesAncoras_ = versaoSlices;
}

void App::applyDecoderOptions()
{
    media::VideoDecoderOptions opt;
    if (settings_.profile == PerformanceProfile::Eco) {
        opt.lowPower = true;
        opt.targetWidth = composition_.width;
        opt.targetHeight = composition_.height;
    }
    composition_.decoderOptions = opt;
    composition_.maxBackgroundSources = settingsFor(settings_.profile).maxBackground;
}

void App::setWindowIcon()
{
    const std::filesystem::path dir = paths::assetsDir();
    if (dir.empty())
        return;
    const RgbaImage mark = loadImageFile(dir / L"brand" / L"bsl-mark.png");
    if (mark.empty())
        return;

    const RgbaImage small = squareIcon(mark, 32);
    const RgbaImage large = squareIcon(mark, 256);
    GLFWimage images[2];
    images[0] = {small.width, small.height, const_cast<unsigned char*>(small.pixels.data())};
    images[1] = {large.width, large.height, const_cast<unsigned char*>(large.pixels.data())};
    glfwSetWindowIcon(window_, 2, images);
}

void App::setupDemo()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(options_.demoFolder, ec)) {
        log::warn("--demo: pasta não encontrada: " + paths::toUtf8(options_.demoFolder));
        return;
    }

    const fs::path alphaFile = options_.demoFolder / L"vp9_alpha.webm";
    const bool hasAlphaFile = fs::exists(alphaFile, ec);

    std::vector<Clip> found;
    for (const auto& entry : fs::directory_iterator(options_.demoFolder, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        if (hasAlphaFile && entry.path().filename() == alphaFile.filename())
            continue;
        Clip clip = Clip::fromFile(entry.path());
        if (!clip.empty())
            found.push_back(std::move(clip));
    }
    std::sort(found.begin(), found.end(), [](const Clip& a, const Clip& b) {
        if (a.kind != b.kind)
            return a.kind == media::MediaKind::Video;
        return a.name < b.name;
    });

    const bool docsDemo = fs::exists(options_.demoFolder / L"docs" / L"sample.pdf", ec);
    constexpr int kDocColumn = 3;
    for (size_t i = 0; i < found.size(); ++i) {
        const int column = (docsDemo && static_cast<int>(i) >= kDocColumn) ? static_cast<int>(i) + 1 : static_cast<int>(i);
        while (column >= composition_.columnCount())
            composition_.addColumn();
        composition_.setClip(0, column, std::move(found[i]));
    }
    if (hasAlphaFile) {
        composition_.setClip(1, 0, Clip::fromFile(alphaFile));
        composition_.layers[1].blendMode = BlendMode::Normal;
    }

    while (composition_.columnCount() < 3)
        composition_.addColumn();
    if (composition_.layerCount() >= 3) {
        Clip text = Clip::generator(kTextId);
        text.setParam("text", "BSL Stage ao vivo: texto, letreiro, captura de tela e webcam. Ação, emoção e coração!");
        text.setParam("size", 120);
        text.setParam("vAlign", "bottom");
        text.setParam("margin", 60);
        text.setParam("outline", true);
        text.setParam("outlineWidth", media::param::floatJson(5.0f));
        text.setParam("shadow", true);
        text.setParam("bg", true);
        text.setParam("bgOpacity", media::param::floatJson(0.55f));
        text.setParam("bgPadding", 28);
        text.setParam("marquee", "horizontal");
        text.setParam("marqueeSpeed", media::param::floatJson(260.0f));
        composition_.setClip(2, 0, std::move(text));

        composition_.setClip(2, 1, Clip::generator(kScreenCaptureId));
        composition_.setClip(1, 2, Clip::generator(kCaptureDeviceId));
        Clip color = Clip::generator(kSolidColorId);
        color.setParam("color", media::param::colorJson({0.925f, 0.2f, 0.216f, 1.0f}));
        composition_.setClip(1, 1, std::move(color));
    }

    if (!composition_.clip(0, 0).empty()) {
        fx::EffectChain& chain = composition_.clip(0, 0).effects;
        chain.add("native:blur").setParam("radius", media::param::floatJson(6.0f));
        chain.add("native:vignette").setParam("amount", media::param::floatJson(0.7f));
    }
    if (composition_.layerCount() >= 3 && composition_.columnCount() >= 3)
        composition_.setClip(2, 2, Clip::generator("isf:Gradiente animado.fs"));

    if (!options_.pluginFolders.empty() || !ffgl::Catalog::instance().folders().empty()) {
        ffgl::Catalog& plugins = ffgl::Catalog::instance();
        plugins.waitIdle(60000);
        for (const ffgl::EntryPtr& e : plugins.entries()) {
            if (e->info.type != ffgl::kTypeEffect || e->map.descs.empty() || composition_.clip(0, 1).empty())
                continue;
            composition_.clip(0, 1).effects.add(e->typeId);
            log::info("--demo: efeito FFGL \"" + ffgl::pluginDisplayName(e->info) + "\" no clip (1, 2)");
            break;
        }
    }

    while (composition_.columnCount() < 6)
        composition_.addColumn();
    if (fx::bsllight::bslWebAvailable() && composition_.layerCount() >= 2) {
        if (composition_.clip(1, 5).empty())
            composition_.setClip(1, 5, Clip::generator(fx::bsllight::kWebBackgroundsId));
    }

    composition_.triggerColumn(0);

    const fs::path docs = options_.demoFolder / L"docs";
    const fs::path samplePdf = docs / L"sample.pdf", samplePptx = docs / L"sample.pptx", sampleHtml = docs / L"sample.html";
    if (composition_.layerCount() >= 3 && fs::exists(samplePdf, ec)) {
        const int col = kDocColumn;
        while (composition_.columnCount() <= col)
            composition_.addColumn();
        composition_.setClip(0, col, Clip::fromFile(samplePdf));
        if (fs::exists(samplePptx, ec))
            composition_.setClip(1, col, Clip::fromFile(samplePptx));
        if (fs::exists(sampleHtml, ec)) {
            Clip web = Clip::fromFile(sampleHtml);
            web.setParam("transparent", true);
            composition_.setClip(2, col, std::move(web));
        }
        composition_.layers[1].opacity = 0.55f;
        composition_.triggerColumn(col);
        selection_ = {Selection::Kind::Clip, 1, col};
    }
    for (const auto& [layer, column] : options_.triggers)
        composition_.trigger(layer - 1, column - 1);
    if (selection_.kind != Selection::Kind::Clip)
        selection_ = composition_.layerCount() >= 3 ? Selection{Selection::Kind::Clip, 2, 0}
                                                    : Selection{Selection::Kind::Clip, 0, 0};
    if (!options_.triggers.empty())
        selection_ = {Selection::Kind::Clip, options_.triggers.back().first - 1, options_.triggers.back().second - 1};
}

void App::updateLiveThumbs()
{
    const double now = glfwGetTime();
    for (int i = 0; i < composition_.layerCount(); ++i) {
        Clip* c = composition_.activeClip(i);
        if (!c || !(c->isGenerator() || c->isWeb()) || c->isTestPattern() || !c->isOpen())
            continue;
        const media::ISource& src = *c->source;
        if (src.texture() == 0 || src.width() <= 0 || src.height() <= 0)
            continue;
        if (now - c->liveThumbTime < 1.0)
            continue;
        liveThumbs_->blit(src.texture(), src.width(), src.height(), c->liveThumb);
        c->liveThumbTime = now;
    }
}

void App::setupDmxDemo()
{
    if (options_.qaDir.empty()) {
        log::warn("--dmx-demo só vale junto com --qa (não altera as configurações do usuário).");
        return;
    }
    settings_.dmxNet.enabled = true;
    settings_.dmxNet.interfaceIp = options_.dmxDemo;
    settings_.dmxNet.universesText = "1-3";
    dmx::Config& cfg = composition_.dmx;
    if (cfg.empty() && composition_.layerCount() >= 3) {
        const dmx::LayerMode modes[3] = {dmx::LayerMode::Full, dmx::LayerMode::Standard, dmx::LayerMode::Full16};
        int channel = 1;
        for (int i = 0; i < 3; ++i) {
            dmx::Block b;
            b.kind = dmx::BlockKind::Layer;
            b.layerId = composition_.layers[static_cast<size_t>(i)].id;
            b.mode = modes[i];
            b.universe = 1;
            b.channel = channel;
            channel += b.channelCount();
            cfg.addBlock(std::move(b));
        }
        dmx::Block master;
        master.kind = dmx::BlockKind::Master;
        master.universe = 1;
        master.channel = channel;
        cfg.addBlock(std::move(master));

        dmx::Map blackout;
        blackout.type = dmx::Map::Type::Action;
        blackout.universe = 2;
        blackout.channel = 1;
        blackout.binding.action = "master.blackout";
        blackout.binding.mode = control::Mode::Hold;
        cfg.addMap(std::move(blackout));
    }
    ui::openDmxWindowTab(0);
}

void App::setupUpdates()
{
    const std::string repo = BSLSTAGE_GITHUB_REPO;
    updates_.configure(BSLSTAGE_VERSION, repo, paths::localDataDir() / L"updates");
    updates_.setInstalledByMsi(update::installedByMsi());
    const bool offlineRun = qaOn_ || !options_.screenshotPath.empty() || !options_.screenshotOutputPath.empty();
    bool fake = false;
    if (offlineRun) {
        auto http = std::make_unique<update::FakeHttp>();
        if (!options_.fakeRelease.empty() && qaOn_) {
            std::ifstream in(options_.fakeRelease / L"release.json", std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            update::Release rel;
            if (!text.empty() && update::parseRelease(text, rel)) {
                const std::string base = "https://api.github.com/repos/" + repo + "/releases";
                http->addBody(base + "/latest", text);
                http->addBody(base + "?per_page=10", "[" + text + "]");
                for (const update::Asset& a : rel.assets) {
                    std::error_code ec;
                    const std::filesystem::path f = options_.fakeRelease / std::filesystem::path(std::u8string(a.name.begin(), a.name.end()));
                    if (std::filesystem::is_regular_file(f, ec))
                        http->addFile(a.url, f);
                }
                fake = true;
            } else {
                log::warn("--fake-release: release.json ausente ou inválido em " + paths::toUtf8(options_.fakeRelease));
            }
        }
        updates_.setHttp(std::move(http));
    }
    updates_.loadPersistedState(settings_.update);
    bool ok = true;
    const std::string last = updates_.takeLastUpdateMessage(&ok);
    if (!last.empty())
        (ok ? log::info(last) : log::warn(last));
    nextAutoCheck_ = fake ? 1.5 : update::kAutoCheckDelaySeconds;
}

void App::updateTick()
{
    update::CheckOutcome outcome;
    while (updates_.takeCheckOutcome(outcome)) {
        if (outcome.ok) {
            settings_.update.lastCheckUnix = outcome.checkedUnix;
            settings_.save();
            log::info("Atualizações: verificação concluída");
        } else {
            log::info("Atualizações: não foi possível verificar (" + outcome.error + ")");
        }
    }

    if (updates_.restartRequested()) {
        if (project_->busy())
            restartSawModal_ = true;
        else if (restartSawModal_ && !project_->quitApproved() && !quitRequested_) {
            updates_.requestRestart(false);
            restartSawModal_ = false;
        }
    } else {
        restartSawModal_ = false;
    }

    const double elapsed = std::chrono::duration<double>(Clock::now() - startTime_).count();
    if (elapsed < nextAutoCheck_)
        return;
    const bool offlineRun = qaOn_ || !options_.screenshotPath.empty() || !options_.screenshotOutputPath.empty();
    const bool fakeRun = !options_.fakeRelease.empty() && qaOn_;
    if (offlineRun && !fakeRun) {
        nextAutoCheck_ = 1.0e18;
        return;
    }
    if (fakeRun) {
        nextAutoCheck_ = 1.0e18;
        updates_.startCheck(settings_.update, false);
        return;
    }
    const auto now = static_cast<int64_t>(std::time(nullptr));
    if (!update::dueForAutoCheck(settings_.update, now)) {
        nextAutoCheck_ = elapsed + 3600.0;
        return;
    }
    update::ShowState show;
    show.outputLive = OutputRenderer::anyLive(output_);
    show.windowFullscreen = fullscreen_;
    show.layoutLocked = ui::layoutLocked();
    show.ecoProfile = settings_.profile == PerformanceProfile::Eco;
    if (update::showBlocksAutoCheck(show)) {
        nextAutoCheck_ = elapsed + 300.0;
        return;
    }
    updates_.startCheck(settings_.update, false);
    nextAutoCheck_ = elapsed + update::kAutoCheckRetrySeconds;
}

void App::setupKeyHooks()
{
    control::AppHooks& hooks = keyService_.hooks;
    hooks.project = [this](int what) {
        switch (what) {
        case 0: project_->requestNew(); break;
        case 1: project_->requestOpen(); break;
        case 2: project_->requestSave(); break;
        default: project_->requestSaveAs(); break;
        }
    };
    hooks.setMode = [this](int mode) {
        mode_ = mode == 1 ? AppMode::Mapping : mode == 2 ? AppMode::PixelMap : mode == 3 ? AppMode::Sequences : AppMode::Composition;
    };
    hooks.identify = [this] { return outputs_ && outputs_->identify(); };
    hooks.setIdentify = [this](bool on) {
        if (outputs_)
            outputs_->setIdentify(on);
    };
    hooks.layoutPreset = [](int index) { return ui::applyLayoutPresetIndex(index); };
    hooks.panelVisible = [](const char* key) { return ui::view::findPanel(key) ? (ui::panelVisibleKey(key) ? 1 : 0) : -1; };
    hooks.setPanelVisible = [](const char* key, bool on) { ui::setPanelVisible(key, on); };
    hooks.fullscreen = [] { return ui::isFullscreen(); };
    hooks.setFullscreen = [](bool on) { ui::requestFullscreen(on); };
    hooks.layoutLocked = [] { return ui::layoutLocked(); };
    hooks.setLayoutLocked = [](bool on) { ui::setLayoutLocked(on); };
    hooks.showAllPanels = [this] { ui::showAllPanels(mode_); };
    hooks.resetLayout = [] { ui::requestResetLayout(); };
    hooks.clearClipSelection = [this] {
        if (selection_.kind == Selection::Kind::Clip)
            selection_ = {};
    };
}

void App::pollSystemHotkeys()
{
    if (!window_ || !project_)
        return;
    bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0 && glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == 0;
    if (!focused && imguiReady_) {
        for (const ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports) {
            auto* w = static_cast<GLFWwindow*>(vp->PlatformHandle);
            if (w && w != window_ && glfwGetWindowAttrib(w, GLFW_FOCUSED))
                focused = true;
        }
    }
    keyService_.pollSystem(composition_, ui::selectionRef(selection_), settings_.keys, composition_.keys, focused,
                           project_->busy());
}

void App::saveScreenshot()
{
    saveScreenshotTo(options_.screenshotPath);
}

void App::saveScreenshotTo(const std::filesystem::path& file)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<unsigned char> flipped(pixels.size());
    const size_t stride = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        std::copy_n(&pixels[static_cast<size_t>(h - 1 - y) * stride], stride, &flipped[static_cast<size_t>(y) * stride]);
    }
    writePng(file, w, h, flipped);
}

void App::saveOutputScreenshot()
{
    if (output_.screens.empty()) {
        log::warn("--screenshot-output: nenhuma Screen");
        return;
    }
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!outputs_->readScreen(output_.screens.front().id, rgba, w, h)) {
        log::warn("--screenshot-output: a primeira Screen não foi renderizada");
        return;
    }
    writePng(options_.screenshotOutputPath, w, h, rgba);
}

void App::onClose(GLFWwindow* window)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->project_)
        return;
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        glfwRestoreWindow(window);
    app->project_->requestQuit();
}

void App::onDrop(GLFWwindow* window, int count, const char** droppedPaths)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app)
        return;
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    const ImVec2 origin = app->imguiReady_ ? ImGui::GetMainViewport()->Pos : ImVec2(0.0f, 0.0f);
    app->drop_.x = static_cast<float>(x) + origin.x;
    app->drop_.y = static_cast<float>(y) + origin.y;
    for (int i = 0; i < count; ++i) {
        const std::string utf8(droppedPaths[i]);
        std::filesystem::path file{std::u8string(utf8.begin(), utf8.end())};
        std::wstring ext = file.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
        if (ext == L".bslstage") {
            if (app->project_)
                app->project_->requestOpenPath(file);
            continue;
        }
        if (ext == L".bslplugin") {
            ui::openImportPluginWindow(file);
            continue;
        }
        app->drop_.files.push_back(std::move(file));
    }
    app->drop_.pending = !app->drop_.files.empty();
}

void App::updateStats(Clock::time_point frameStart)
{
    const float interval = std::chrono::duration<float>(frameStart - lastFrameStart_).count();
    lastFrameStart_ = frameStart;
    if (interval > 0.0f && interval < 1.0f)
        smoothedInterval_ += 0.1f * (interval - smoothedInterval_);

    stats_.fps = 1.0f / smoothedInterval_;
    stats_.frameMs = smoothedInterval_ * 1000.0f;
    stats_.workMs = smoothedWork_ * 1000.0f;
}

void App::frame()
{
    const auto now = Clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - lastUpdate_).count(), 0.0, 0.1);
    lastUpdate_ = now;
    const PerformanceProfile before = settings_.profile;
    frameWatch_.enter(FrameWatch::Update);

    if (const int fs = ui::takeFullscreenRequest(); fs >= 0)
        applyFullscreen(fs == 1);

    if (quitRequested_) {
        quitRequested_ = false;
        project_->requestQuit();
    }
    frameWatch_.enter(FrameWatch::Idle);
    project_->update();
    frameWatch_.enter(FrameWatch::Update);
    updateTick();
    if (project_->quitApproved())
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    if (project_->takeLoaded())
        afterProjectLoaded();

    applyDecoderOptions();
    thumbs_->pump();
    pixelmap::syncInput(composition_.pixelMap, composition_.width, composition_.height);
    fx::bsllight::PixelMapFeed::instance().set(composition_.pixelMap);
    dmx_.update(settings_.dmxNet, composition_, keyService_, ui::selectionRef(selection_));
    composition_.update(dt);
    updateLiveThumbs();
    syncCanvasSize();
    ensureSliceUids(output_);
    fx::bsllight::SliceFeed::instance().set(fx::bsllight::sliceFeedFromOutput(output_), composition_.width,
                                            composition_.height);
    publicarSequencias();

    {
        ai::AiEngine& aiEngine = ai::AiEngine::instance();
        aiEngine.setProfileRate(settingsFor(settings_.profile).aiFps);
        aiEngine.setRateOverride(settings_.aiFps);
        aiEngine.setDevicePref(settings_.aiDevice == "gpu"   ? ai::DevicePref::Gpu
                               : settings_.aiDevice == "cpu" ? ai::DevicePref::Cpu
                                                             : ai::DevicePref::Auto);
    }

    const bool wantOutputShot = !options_.screenshotOutputPath.empty() && !outputShotDone_;
    const bool outputLive = OutputRenderer::anyLive(output_) || wantOutputShot;

    int previewScreenId = -1;
    if (mode_ == AppMode::Mapping && mapping_.screen >= 0 && mapping_.screen < static_cast<int>(output_.screens.size()))
        previewScreenId = output_.screens[static_cast<size_t>(mapping_.screen)].id;
    else if (wantOutputShot && !output_.screens.empty())
        previewScreenId = output_.screens.front().id;

    std::vector<LayerViewRequest> layerViews = collectLayerViews(output_, previewScreenId);
    if (mode_ == AppMode::Composition) {
        if (const int wanted = ui::previewWantedLayerId(); wanted > 0) {
            const LayerViewRequest rq{wanted, false, true};
            if (std::find(layerViews.begin(), layerViews.end(), rq) == layerViews.end())
                layerViews.push_back(rq);
        }
    }
    frameWatch_.enter(FrameWatch::Compositor);
    compositor_->render(composition_, outputLive ? 1.0f : settingsFor(settings_.profile).previewScale,
                        settings_.profile == PerformanceProfile::Eco, layerViews);
    outputs_->setFrameRate(composition_.fps);
    frameWatch_.enter(FrameWatch::Outputs);
    outputs_->render(output_, *compositor_, previewScreenId);
    frameWatch_.enter(FrameWatch::Update);
    applySwapInterval();

    updateUiScale();
    frameWatch_.enter(FrameWatch::Idle);
    ui::runLayoutDialogs(glfwGetWin32Window(window_));
    ui::runKeysDialogs(glfwGetWin32Window(window_));
    ui::runDmxDialogs(glfwGetWin32Window(window_));
    frameWatch_.enter(FrameWatch::Update);
    if (qaOn_) {
        runQaCommands();
        qa::beginFrame();
    }

    frameWatch_.enter(FrameWatch::Ui);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    PanelContext ctx{composition_, selection_, *compositor_, settings_.profile, stats_, glInfo_,
                     compositor_->uvMax(), settings_, *thumbs_, drop_,
                     quitRequested_, output_, *outputs_, mapping_, mode_, *project_, keyService_, dmx_, updates_, envelopes_};
    ui::drawUi(ctx);
    if (qaOn_) {
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window_, &fw, &fh);
        glfwGetWindowSize(window_, &ww, &wh);
        qa::endFrame(frameCount_, qaStateJson(), ui::uiScale(), fw, fh, ww, wh);
    }

    if (drop_.pending) {
        log::warn("Solte os arquivos sobre uma célula da Composição.");
        drop_.pending = false;
        drop_.files.clear();
    }
    ImGui::Render();

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glClearColor(ui::theme::kBackground.x, ui::theme::kBackground.y, ui::theme::kBackground.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    frameWatch_.enter(FrameWatch::FloatingWindows);
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }

    if (!qaShotPath_.empty()) {
        saveScreenshotTo(qaShotPath_);
        qaShotPath_.clear();
    }
    ++frameCount_;
    if (options_.openCaptureChooser && frameCount_ == 4)
        ui::requestCaptureTarget(composition_.currentDeck, 2, 1);
    const double elapsed = std::chrono::duration<double>(now - startTime_).count();
    if (!options_.screenshotPath.empty() && !screenshotDone_ && frameCount_ > 5
        && elapsed >= options_.screenshotSeconds) {
        saveScreenshot();
        screenshotDone_ = true;
    }
    if (wantOutputShot && frameCount_ > 5 && elapsed >= options_.screenshotOutputSeconds) {
        saveOutputScreenshot();
        outputShotDone_ = true;
    }
    const bool anyShot = !options_.screenshotPath.empty() || !options_.screenshotOutputPath.empty();
    if (anyShot && (options_.screenshotPath.empty() || screenshotDone_)
        && (options_.screenshotOutputPath.empty() || outputShotDone_))
        glfwSetWindowShouldClose(window_, GLFW_TRUE);

    frameWatch_.enter(FrameWatch::Present);
    glfwSwapBuffers(window_);
    frameWatch_.enter(FrameWatch::Update);

    if (settings_.profile != before) {
        applyProfile();
        settings_.save();
    }
}

void App::applyFullscreen(bool on)
{
    if (!window_ || on == fullscreen_)
        return;
    if (on) {
        fsMaximized_ = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0;
        if (fsMaximized_)
            glfwRestoreWindow(window_);
        glfwGetWindowPos(window_, &fsX_, &fsY_);
        glfwGetWindowSize(window_, &fsW_, &fsH_);
        GLFWmonitor* monitor = monitorOfWindow(window_);
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode)
            return;
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitor, &mx, &my);
        glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowPos(window_, mx, my);
        glfwSetWindowSize(window_, mode->width, mode->height);
    } else {
        glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowSize(window_, fsW_, fsH_);
        glfwSetWindowPos(window_, fsX_, fsY_);
        if (fsMaximized_)
            glfwMaximizeWindow(window_);
    }
    fullscreen_ = on;
    ui::setFullscreenState(on);
    log::info(on ? "Tela cheia" : "Janela restaurada");
}

void App::updateUiScale()
{
    float scale = ui::uiScale();
    switch (settings_.uiScaleMode) {
    case UiScaleMode::Fixed:
        scale = std::clamp(settings_.uiScale, 0.75f, 3.0f);
        break;
    case UiScaleMode::System: {
        float sx = 1.0f;
        float sy = 1.0f;
        glfwGetWindowContentScale(window_, &sx, &sy);
        scale = sy > 1.0f ? sy : 1.0f;
        break;
    }
    case UiScaleMode::Window: {
        int fbW = 0;
        int fbH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0)
            return;
        const float raw = std::clamp(std::min(static_cast<float>(fbW) / 1600.0f, static_cast<float>(fbH) / 900.0f),
                                     0.75f, 3.0f);
        if (std::abs(raw - scale) >= 0.035f)
            scale = std::round(raw / 0.05f) * 0.05f;
        break;
    }
    }
    if (std::abs(scale - ui::uiScale()) > 1.0e-4f)
        ui::applyTheme(scale);
}

void App::runQaCommands()
{
    for (const std::string& line : qa::takeCommands()) {
        const size_t sp = line.find(' ');
        const std::string cmd = line.substr(0, sp);
        const std::string arg = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        if (cmd == "shot") {
            qaShotPath_ = std::filesystem::path{std::u8string(arg.begin(), arg.end())};
        } else if (cmd == "quit") {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        } else if (cmd == "drop") {
            std::wstring files;
            size_t pos = 0;
            while (pos < arg.size()) {
                const size_t bar = arg.find('|', pos);
                const std::string one = arg.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
                files += std::filesystem::path{std::u8string(one.begin(), one.end())}.wstring();
                files.push_back(L'\0');
                if (bar == std::string::npos)
                    break;
                pos = bar + 1;
            }
            files.push_back(L'\0');
            const SIZE_T bytes = sizeof(DropFilesHeader) + files.size() * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GHND, bytes);
            if (!mem)
                continue;
            auto* df = static_cast<DropFilesHeader*>(GlobalLock(mem));
            df->pFiles = sizeof(DropFilesHeader);
            df->fWide = TRUE;
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(glfwGetWin32Window(window_), &pt);
            df->pt = pt;
            std::memcpy(reinterpret_cast<char*>(df) + sizeof(DropFilesHeader), files.data(), files.size() * sizeof(wchar_t));
            GlobalUnlock(mem);
            SendMessageW(glfwGetWin32Window(window_), WM_DROPFILES, reinterpret_cast<WPARAM>(mem), 0);
        }
    }
}

std::string App::qaStateJson() const
{
    using nlohmann::json;
    json j;
    const Composition& c = composition_;
    j["mode"] = mode_ == AppMode::Mapping ? "mapping" : mode_ == AppMode::PixelMap ? "pixelmap" : mode_ == AppMode::Sequences ? "sequences" : "composition";
    j["layout"] = uiLayoutKey(settings_.layout);
    j["scaleMode"] = settings_.uiScaleMode == UiScaleMode::Window   ? "window"
                     : settings_.uiScaleMode == UiScaleMode::System ? "system"
                                                                    : "fixed";
    j["maximized"] = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0;
    j["deck"] = c.currentDeck;
    json decks = json::array();
    for (const Deck& d : c.decks)
        decks.push_back({{"name", d.name}, {"clips", d.clipCount()}});
    j["decks"] = decks;
    auto chainJson = [](const fx::EffectChain& chain) {
        json arr = json::array();
        for (const fx::EffectInstance& e : chain.items)
            arr.push_back({{"type", e.typeId}, {"bypass", e.bypass}});
        return arr;
    };
    json layers = json::array();
    for (int li = 0; li < c.layerCount(); ++li) {
        const Layer& l = c.layers[static_cast<size_t>(li)];
        const Clip* active = c.activeClip(li);
        layers.push_back({{"id", l.id}, {"sliceOnly", l.sliceOnly},
                          {"name", l.name}, {"activeDeck", l.activeDeck}, {"activeColumn", l.activeColumn},
                          {"opacity", l.opacity}, {"bypass", l.bypass}, {"solo", l.solo},
                          {"blend", static_cast<int>(l.blendMode)},
                          {"transform", {{"x", l.transform.x}, {"y", l.transform.y}, {"w", l.transform.width(c.width)},
                                         {"h", l.transform.height(c.height)}, {"rotation", l.transform.rotation},
                                         {"anchor", {l.transform.anchorX, l.transform.anchorY}},
                                         {"crop", {l.transform.cropL, l.transform.cropT, l.transform.cropR, l.transform.cropB}},
                                         {"identity", l.transform.isIdentity(c.width, c.height)}}},
                          {"ownTransform", active ? active->ownTransform : false},
                          {"clipPlaying", active && active->isOpen() ? active->source->isPlaying() : false},
                          {"clipPosition", active && active->isOpen() ? active->source->position() : 0.0},
                          {"effects", chainJson(l.effects)}});
    }
    j["layers"] = layers;
    {
        const ui::PreviewInfo pv = ui::previewInfo();
        const ui::PreviewOptions& po = ui::previewOptions();
        j["preview"] = {{"mode", po.source == ui::PreviewSource::Layer ? "layer" : "clip"},
                        {"checker", po.checker},
                        {"fit", po.fit == FitMode::Fill ? "fill" : "fit"},
                        {"layer", pv.layer},
                        {"column", pv.column},
                        {"hasClip", pv.hasClip},
                        {"live", pv.live},
                        {"own", pv.ownSource},
                        {"playing", pv.playing},
                        {"position", pv.position},
                        {"duration", pv.duration}};
        j["layoutPresets"] = ui::layoutUiInfo().presets;
        j["lastLayoutPreset"] = settings_.lastLayoutPreset;
    }
    j["masterEffects"] = chainJson(c.masterEffects);
    j["isf"] = {{"scanning", fx::IsfLibrary::instance().scanning()},
                {"entries", fx::IsfLibrary::instance().entries().size()}};
    j["ffgl"] = {{"scanning", ffgl::Catalog::instance().scanning()},
                 {"entries", ffgl::Catalog::instance().entries().size()},
                 {"blocked", ffgl::Catalog::instance().blocked().size()}};
    {
        ndi::Runtime& rt = ndi::Runtime::instance();
        json sources = json::array();
        if (rt.available()) {
            ndi::Finder::shared().start();
            for (const ndi::SourceInfo& s : ndi::Finder::shared().sources())
                sources.push_back(s.name);
        }
        json outs = json::array();
        for (const Screen& s : output_.screens)
            if (s.kind == OutputKind::Ndi)
                outs.push_back({{"screen", s.name}, {"name", s.ndiName}, {"connections", outputs_->ndiConnections(s.id)}});
        j["ndi"] = {{"available", rt.available()}, {"version", rt.version()}, {"error", rt.error()},
                    {"sources", sources}, {"outputs", outs}};
        j["spout"] = {{"senders", media::SpoutSource::activeSenders()}};
    }
    j["focused"] = window_ && glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
    j["columns"] = c.columnCount();
    j["bpm"] = c.bpm;
    j["masterOpacity"] = c.masterOpacity;
    j["paused"] = c.paused;
    j["canvas"] = {c.width, c.height};
    j["layerViews"] = compositor_ ? compositor_->layerViewCount() : 0;
    json cells = json::array();
    if (c.validDeck(c.currentDeck))
        for (int l = 0; l < c.layerCount(); ++l)
            for (int col = 0; col < c.columnCount(); ++col)
                if (!c.clip(l, col).empty())
                    cells.push_back({l, col, c.clip(l, col).name});
    j["cells"] = cells;
    j["selection"] = {{"kind", static_cast<int>(selection_.kind)}, {"layer", selection_.layer}, {"column", selection_.column}};
    if (selection_.kind == Selection::Kind::Clip && c.validCell(selection_.layer, selection_.column)) {
        const Clip& clip = c.clip(selection_.layer, selection_.column);
        j["selClip"] = {{"name", clip.name}, {"speed", clip.speed}, {"loop", static_cast<int>(clip.loop)},
                        {"fit", static_cast<int>(clip.fit)}, {"playing", clip.playing}, {"params", clip.params.json()},
                        {"open", clip.isOpen()}, {"effects", chainJson(clip.effects)},
                        {"status", clip.isOpen() ? clip.source->status().text : std::string()}};
    }
    json screens = json::array();
    for (const Screen& s : output_.screens) {
        json slices = json::array();
        for (const Slice& sl : s.slices) {
            slices.push_back({{"name", sl.name},
                              {"zFilter", sl.zFilter},
                              {"zSource", sl.zSource},
                              {"source", sl.source},
                              {"layerId", sl.layerId},
                              {"includeMaster", sl.includeMaster},
                              {"applyLayerTransform", sl.applyLayerTransform},
                              {"input", {sl.input.x, sl.input.y, sl.input.w, sl.input.h}},
                              {"corners", {sl.corners[0].x, sl.corners[0].y, sl.corners[1].x, sl.corners[1].y,
                                           sl.corners[2].x, sl.corners[2].y, sl.corners[3].x, sl.corners[3].y}}});
        }
        screens.push_back({{"name", s.name}, {"w", s.width}, {"h", s.height}, {"slices", slices},
                           {"kind", outputKindName(s.kind)}});
    }
    j["screens"] = screens;
    const ui::MapEditorInfo mapInfo = ui::mapEditorInfo();
    j["mapping"] = {{"screen", mapping_.screen},     {"slice", mapping_.slice},          {"corner", mapping_.corner},
                    {"multi", mapping_.multi},       {"pointsMode", mapping_.pointsMode}, {"undo", mapInfo.undoCount},
                    {"redo", mapInfo.redoCount},     {"snap", mapInfo.snap},             {"grid", mapInfo.grid},
                    {"gridStep", mapInfo.gridStep},  {"inputZoom", mapInfo.inputZoom},   {"outputZoom", mapInfo.outputZoom},
                    {"dragging", mapInfo.dragging},  {"guides", mapInfo.guides}};
    {
        const pixelmap::Config& pmc = c.pixelMap;
        const pixelmap::Validation pmv = pixelmap::validate(pmc);
        const ui::PixelMapUiInfo pmi = ui::pixelMapUiInfo();
        json panels = json::array();
        for (size_t i = 0; i < pmc.panels.size(); ++i) {
            const pixelmap::Panel& p = pmc.panels[i];
            const RectI r = pixelmap::scaledRectRounded(pmc, p);
            panels.push_back({{"name", p.name},
                              {"phys", {p.x, p.y, p.w, p.h}},
                              {"comp", {r.x, r.y, r.w, r.h}},
                              {"cabinets", p.byCabinets},
                              {"overlap", pmv.overlap[i]},
                              {"outside", pmv.outside[i]}});
        }
        j["pixelmap"] = {{"input", {pmc.inW, pmc.inH}},
                         {"linked", pmc.linkCanvas},
                         {"phys", {pmc.physW, pmc.physH}},
                         {"watermark", static_cast<int>(pmc.watermark)},
                         {"panels", panels},
                         {"scaleWarn", pmv.scaleWarn},
                         {"selected", pmi.selected},
                         {"snap", pmi.snap},
                         {"dragging", pmi.dragging},
                         {"guides", pmi.guides},
                         {"zoom", pmi.zoom},
                         {"status", pmi.status}};
    }
    {
        const ui::SeqUiInfo si = ui::seqUiInfo();
        const ui::EnvelopeUiInfo ei = ui::envelopeUiInfo(envelopes_);
        json sequencias = json::array();
        for (const seq::Sequencia& s : c.sequences.sequencias) {
            json acesos = json::array();
            for (const seq::Passo& p : s.passos)
                acesos.push_back(static_cast<int>(p.size()));
            sequencias.push_back({{"id", s.id},
                                  {"name", s.nome},
                                  {"steps", static_cast<int>(s.passos.size())},
                                  {"lit", acesos},
                                  {"generator", s.temGerador},
                                  {"genDirection", static_cast<int>(s.gerador.direcao)},
                                  {"genManual", s.gerador.bordasManuais},
                                  {"genBordersX", s.gerador.bordasX.size()},
                                  {"genBordersY", s.gerador.bordasY.size()}});
        }
        json visoes = json::array();
        for (const seq::Visao& v : c.sequences.visoes)
            visoes.push_back({{"id", v.id}, {"name", v.nome}, {"count", static_cast<int>(v.ligados.size())}, {"preview", v.previaVisivel}});
        json envs = json::array();
        for (const seq::Envelope& e : envelopes_.lista)
            envs.push_back({{"id", e.id}, {"name", e.nome}, {"anchors", static_cast<int>(e.ancoras.size())}});
        int slicesTotal = 0;
        for (const Screen& s : output_.screens)
            slicesTotal += static_cast<int>(s.slices.size());
        j["sequences"] = {{"list", sequencias},
                          {"views", visoes},
                          {"envelopes", envs},
                          {"selected", si.sequenceId},
                          {"view", si.viewId},
                          {"step", si.step},
                          {"tool", si.tool},
                          {"undo", si.canUndo},
                          {"redo", si.canRedo},
                          {"gesture", si.gestureOpen},
                          {"gridEdit", si.gridEdit},
                          {"zoom", si.zoom},
                          {"fit", si.fit},
                          {"ghost", si.ghost},
                          {"backdrop", si.backdrop},
                          {"status", si.status},
                          {"slices", slicesTotal},
                          {"envelope", {{"id", ei.envelopeId},
                                        {"anchors", ei.anchors},
                                        {"selected", ei.selected},
                                        {"undo", ei.canUndo},
                                        {"redo", ei.canRedo},
                                        {"dragging", ei.dragging},
                                        {"snap", ei.snap},
                                        {"status", ei.status}}}};
    }
    {
        const control::KeyService::Info& ki = keyService_.info();
        const ui::KeysUiInfo kui = ui::keysUiInfo();
        j["keys"] = {{"project", control::bindingsToJson(composition_.keys)},
                     {"app", control::bindingsToJson(settings_.keys)},
                     {"fired", ki.firedCount},
                     {"lastAction", ki.lastAction},
                     {"lastKey", ki.lastKey},
                     {"lastHit", ki.lastHit},
                     {"held", keyService_.dispatcher.heldCount()},
                     {"systemRegistered", keyService_.system.registeredCount()},
                     {"editorOpen", kui.editorOpen},
                     {"capturing", kui.capturing},
                     {"conflictOpen", kui.conflictOpen},
                     {"rows", kui.rows}};
        j["blackout"] = c.blackout;
    }
    {
        const dmx::Snapshot& ds = dmx_.snapshot();
        const ui::DmxUiInfo du = ui::dmxUiInfo();
        json universes = json::array();
        for (const dmx::UniverseView& u : ds.universes)
            universes.push_back({{"universe", u.universe}, {"active", u.active}, {"packets", u.packets}, {"pps", u.packetsPerSec}});
        j["dmx"] = {{"state", static_cast<int>(dmx_.state())}, {"running", ds.running}, {"status", ds.status},
                    {"universes", universes},               {"windowOpen", du.open},  {"tab", du.tab},
                    {"learning", du.learning},              {"blocks", du.blocks},    {"maps", du.maps},
                    {"applied", dmx_.engine().info().applied}, {"enabled", settings_.dmxNet.enabled}};
    }
    {
        const ui::TransformEditorInfo te = ui::transformEditorInfo();
        j["transformEditor"] = {{"output", te.onOutput}, {"preview", te.onPreview}, {"crop", te.crop},    {"snap", te.snap},
                                {"undo", te.undo},       {"redo", te.redo},         {"dragging", te.dragging},
                                {"drag", te.drag},       {"guides", te.guides}};
    }
    {
        const ui::ViewInfo vi = ui::viewInfo();
        json panels = json::array();
        for (const ui::ViewInfo::PanelRow& p : vi.panels)
            panels.push_back({{"key", p.key},
                              {"visible", p.visible},
                              {"detached", p.detached},
                              {"seen", p.seen},
                              {"docked", p.docked},
                              {"rect", {p.rect[0] - vi.mainPos[0], p.rect[1] - vi.mainPos[1], p.rect[2], p.rect[3]}}});
        j["view"] = {{"panels", panels}, {"hidden", vi.hidden}, {"detached", vi.detached}, {"locked", vi.locked},
                     {"fullscreen", vi.fullscreen}, {"viewports", vi.viewports}, {"mainPos", {vi.mainPos[0], vi.mainPos[1]}}};
    }
    {
        const update::Status us = updates_.status();
        const ui::UpdateUiInfo uu = ui::updateUiInfo();
        const ui::ReportUiInfo ru = ui::reportUiInfo();
        j["update"] = {{"phase", static_cast<int>(us.phase)}, {"checking", us.checking}, {"hasUpdate", us.hasUpdate},
                       {"version", us.hasUpdate ? us.release.version.toString() : std::string()},
                       {"asset", us.assetName},            {"error", us.error},       {"checkError", us.checkError},
                       {"windowOpen", uu.windowOpen},      {"notice", uu.noticeShown}, {"noticeText", uu.noticeText},
                       {"autoCheck", settings_.update.autoCheck}, {"beta", settings_.update.beta},
                       {"skipped", settings_.update.skippedVersion}, {"lastCheck", settings_.update.lastCheckUnix},
                       {"reminded", updates_.reminded()}, {"current", BSLSTAGE_VERSION}, {"repo", BSLSTAGE_GITHUB_REPO}};
        j["report"] = {{"open", ru.open},         {"kind", ru.kind},           {"system", ru.includeSystem}, {"log", ru.includeLog},
                       {"bodyLength", ru.bodyLength}, {"urlLength", ru.urlLength}, {"truncated", ru.truncated},
                       {"lastUrl", ru.lastUrl},   {"lastClipboard", ru.lastClipboard}};
    }
    {
        const ui::LegalUiInfo li = ui::legalUiInfo();
        j["legal"] = {{"windowOpen", li.windowOpen},     {"tab", li.tab},
                      {"noticeVersion", legal::kNoticeVersion},
                      {"thirdParty", li.thirdPartyCount}, {"licenseFull", li.licenseFullOpen},
                      {"openedTexts", li.openedTexts},  {"docsFound", li.docsFound},
                      {"lastUrl", li.lastUrl}};
    }
    {
        const ui::PluginUiInfo pi = ui::pluginUiInfo();
        j["plugins"] = {{"wizardOpen", pi.wizardOpen},   {"created", pi.created},       {"createdFile", pi.createdFile},
                        {"createdId", pi.createdId},     {"error", pi.error},           {"rows", pi.rows},
                        {"importOpen", pi.importOpen},   {"importValid", pi.importValid}, {"importName", pi.importName},
                        {"importLicense", pi.importLicense}, {"importAuthor", pi.importAuthor}, {"importError", pi.importError},
                        {"installedFolder", pi.installedFolder}, {"exportOpen", pi.exportOpen}, {"exportedFile", pi.exportedFile},
                        {"lastAction", pi.lastAction},   {"lastPath", pi.lastPath},     {"message", pi.message}};
    }
    j["dirty"] = project_ ? project_->dirty() : false;
    j["project"] = project_ ? paths::toUtf8(project_->path()) : std::string();
    j["busy"] = project_ ? project_->busy() : false;
    return j.dump();
}

void App::limitFrameRate(Clock::time_point frameStart) const
{
    int fps = settingsFor(settings_.profile).uiFps;
    if (fps <= 0 && outputs_ && outputs_->hasWindowOutput())
        fps = kOutputWindowUiFps;
    if (fps <= 0)
        return;

    const auto target = frameStart + std::chrono::duration_cast<Clock::duration>(
                                         std::chrono::duration<double>(1.0 / fps));
    while (true) {
        const auto now = Clock::now();
        if (now >= target)
            break;
        if (target - now > std::chrono::milliseconds(2))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
            std::this_thread::yield();
    }
}

}
