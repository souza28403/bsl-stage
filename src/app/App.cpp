#include "app/App.h"

#include "app/FfglSetup.h"
#include "app/IsfSetup.h"
#include "app/Log.h"
#include "app/Paths.h"
#include "app/ProjectSession.h"
#include "app/Qa.h"
#include "ai/AiEngine.h"
#include "audio/AudioAnalyzer.h"
#include "ffgl/FfglCatalog.h"
#include "fx/IsfLibrary.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslSources.h"
#include "fx/bsl/PixelMapFeed.h"
#include "fx/bsl/SliceFeed.h"
#include "media/DocThumb.h"
#include "media/SpoutSource.h"
#include "media/WorkerHost.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiRuntime.h"
#include "output/OutputRenderer.h"
#include "render/Compositor.h"
#include "render/LiveThumb.h"
#include "render/TextureLoader.h"
#include "render/ThumbnailCache.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/LayoutUi.h"
#include "ui/Ui.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/MapView.h"
#include "ui/panels/PixelMapPanels.h"
#include "ui/panels/PreviewPanel.h"
#include "ui/panels/TabPanels.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <stb_image_write.h>

#include <windows.h>
#include <mmsystem.h> // timeBeginPeriod
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <thread>
#include <vector>

namespace bsl {

namespace {

constexpr const char* kWindowTitle = "BSL Stage";

void glfwErrorCallback(int code, const char* description)
{
    std::fprintf(stderr, "GLFW erro %d: %s\n", code, description);
}

// Mesmo layout do DROPFILES do Windows (shlobj.h traz macros que atrapalham o resto do arquivo)
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

// Reduz 'src' para caber em size x size mantendo a proporcao (centralizada, fundo transparente)
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

// Grava RGBA8 (linhas de cima para baixo) como PNG opaco
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

} // namespace

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
    while (!glfwWindowShouldClose(window_)) {
        const auto frameStart = Clock::now();
        glfwPollEvents();

        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lastUpdate_ = Clock::now();
            continue;
        }

        updateStats(frameStart);
        frame();

        // Tempo de trabalho medido antes da espera do limitador
        smoothedWork_ += 0.1f * (std::chrono::duration<float>(Clock::now() - frameStart).count()
                                 - smoothedWork_);
        limitFrameRate(frameStart);
    }

    shutdown();
    return 0;
}

bool App::init()
{
    if (!options_.qaDir.empty()) { // teste de interface: settings, imgui.ini, log e autosave ficam isolados na pasta
        std::error_code ec;
        std::filesystem::create_directories(options_.qaDir, ec);
        paths::setAppDataOverride(options_.qaDir / L"appdata");
        qaOn_ = true;
    }
    settings_ = Settings::load();
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
    composition_ = Composition::createDefault();
    {
        Settings ffglSettings = settings_; // --plugins entra so na varredura, nao nas configuracoes salvas
        for (const std::filesystem::path& dir : options_.pluginFolders)
            ffglSettings.ffglFolders.push_back(paths::toUtf8(dir));
        configureFfglCatalog(ffglSettings); // plugins FFGL: varredura em segundo plano (cache em %LOCALAPPDATA%)
    }
    configureIsfLibrary(settings_); // pastas ISF (Documents\BSL Stage\ISF + extras); varredura em segundo plano
    audio::AudioAnalyzer::instance().setConfig(settings_.audio); // analise de audio: thread propria (loopback por padrao)
    audio::AudioAnalyzer::instance().start();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    const bool sizeOverride = options_.windowWidth > 0 && options_.windowHeight > 0;
    window_ = glfwCreateWindow(sizeOverride ? options_.windowWidth : settings_.windowWidth,
                               sizeOverride ? options_.windowHeight : settings_.windowHeight, kWindowTitle,
                               nullptr, nullptr);
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

    // Resolucao de timer de 1 ms para o limitador de FPS
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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    // Viewports multiplos ficam desabilitados (nao ligar ImGuiConfigFlags_ViewportsEnable)
    iniPath_ = paths::toUtf8(paths::appDataDir() / L"imgui.ini");
    io.IniFilename = iniPath_.c_str();
    io.ConfigDebugHighlightIdConflicts = false; // so no --qa (qa::init liga): o usuario final nao ve o aviso vermelho
    if (qaOn_)
        qa::init(options_.qaDir);

    ui::loadFont(io);
    ui::applyTheme();
    ui::loadBrand();
    setWindowIcon();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    imguiReady_ = true;

    // Depois do ImGui: o OutputRenderer encadeia o callback de monitores que o backend instalou
    try {
        outputs_ = std::make_unique<OutputRenderer>(window_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erro ao criar o renderer de saída: %s\n", e.what());
        return false;
    }
    output_ = OutputSetup::createDefault(composition_.width, composition_.height, outputs_->monitors());
    mode_ = options_.startInMapping ? AppMode::Mapping : options_.startInPixelMap ? AppMode::PixelMap : AppMode::Composition;
    if (options_.outputWindow)
        setupOutputTest();
    if (options_.testMissingMonitor && !output_.screens.empty()) {
        Screen& s = output_.screens.front();
        s.kind = OutputKind::Monitor;
        s.monitorIndex = 7;
        s.monitorName = "Monitor Fantasma";
    }
    outputs_->setIdentify(options_.identify);
    if (options_.canvasWidth > 0 && options_.canvasHeight > 0) { // syncCanvasSize recria o FBO no 1o frame
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

    // Projeto: Novo/Abrir/Salvar, autosave e recuperação. Capturas de teste não mexem em lock/autosave.
    project_ = std::make_unique<ProjectSession>(
        ProjectSession::Targets{composition_, output_, selection_, mapping_, settings_},
        [this](int w, int h) { return OutputSetup::createDefault(w, h, outputs_->monitors()); });
    project_->setWindow(window_);
    const bool captureRun = !options_.screenshotPath.empty() || !options_.screenshotOutputPath.empty();
    if (!captureRun)
        project_->enableCrashRecovery(options_.projectPath.empty());
    project_->markClean();
    if (!options_.projectPath.empty()) {
        project_->openNow(options_.projectPath);
        project_->takeLoaded();
        afterProjectLoaded();
        // Testes: --trigger <camada> <coluna> também vale com um projeto (dispara e seleciona o clip)
        for (const auto& [layer, column] : options_.triggers)
            composition_.trigger(layer - 1, column - 1);
        if (!options_.triggers.empty())
            selection_ = {Selection::Kind::Clip, options_.triggers.back().first - 1, options_.triggers.back().second - 1};
    }
    return true;
}

void App::shutdown()
{
    if (qaOn_) {
        qa::finish();
        qaOn_ = false;
    }
    ffgl::Catalog::instance().shutdown(); // mata a varredura de plugin em andamento
    audio::AudioAnalyzer::instance().stop();
    if (project_)
        project_->shutdown(); // encerramento normal: apaga o lock e os autosaves

    // Janelas de saída e senders Spout/NDI primeiro (precisam do contexto principal vivo)
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
        // --window-size e so para testes: nao sobrescreve o tamanho salvo da janela
        if (!glfwGetWindowAttrib(window_, GLFW_ICONIFIED)
            && !glfwGetWindowAttrib(window_, GLFW_MAXIMIZED)
            && !(options_.windowWidth > 0 && options_.windowHeight > 0)) {
            glfwGetWindowSize(window_, &settings_.windowWidth, &settings_.windowHeight);
        }
        settings_.save();
    }

    // Tudo que tem recursos GL precisa ser liberado com o contexto ainda vivo
    if (window_) {
        ui::releasePreview();         // fonte da Prévia
        ai::AiEngine::instance().shutdown(); // thread da IA, texturas e sessões (precisa do contexto GL)
        composition_ = Composition{}; // fecha as fontes abertas
        thumbs_.reset();
        liveThumbs_.reset();
        // Fontes fechadas acima pedem o encerramento dos workers (PDF/Office/Web); espera um pouco para nao deixar
        // Office ou navegador abertos. O job object do app derruba o que sobrar quando o processo sair.
        media::WorkerHost::waitAllStopped(8000);
        ui::pixelMapRelease();
        ui::unloadBrand();
        compositor_.reset();
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        timeEndPeriod(1);
    }
}

void App::applyProfile()
{
    applySwapInterval();
}

// vsync da janela principal: so no perfil Maximo e so se nao houver janela de saida com vsync.
// Quando ha, o vsync fica na janela de saida (o canvas segue o monitor de verdade) e o swap da
// principal nao espera; nos outros perfis o limitador manual controla o FPS.
void App::applySwapInterval()
{
    const bool outputPaces = outputs_ && outputs_->hasVsyncWindow();
    const int want = (!outputPaces && settingsFor(settings_.profile).uiFps == 0) ? 1 : 0;
    if (want != swapInterval_) {
        glfwSwapInterval(want);
        swapInterval_ = want;
    }
}

// Teste automatico (--output-window): Screen em janela 1280x720 com 2 slices, metades do canvas;
// a segunda com warp perspectivo e soft edge na esquerda.
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

// Canvas redimensionado no Inspetor: recria os FBOs e acompanha os retangulos de entrada
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

// Projeto novo/aberto: o canvas pode ter outro tamanho. Redimensiona o compositor direto (sem o
// rescaleInputs do syncCanvasSize: os slices do projeto já estão no tamanho certo).
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
    ui::mapEditorReset(output_); // o historico de desfazer do mapeamento e do projeto anterior
    ui::pixelMapReset();
}

void App::applyDecoderOptions()
{
    // Perfil Eco: decoder leve e video reduzido ao tamanho do canvas
    media::VideoDecoderOptions opt;
    if (settings_.profile == PerformanceProfile::Eco) {
        opt.lowPower = true;
        opt.targetWidth = composition_.width;
        opt.targetHeight = composition_.height;
    }
    composition_.decoderOptions = opt;
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
            continue; // vai para a camada 2
        Clip clip = Clip::fromFile(entry.path());
        if (!clip.empty())
            found.push_back(std::move(clip));
    }
    // Videos primeiro, depois imagens; alfabetico dentro de cada grupo
    std::sort(found.begin(), found.end(), [](const Clip& a, const Clip& b) {
        if (a.kind != b.kind)
            return a.kind == media::MediaKind::Video;
        return a.name < b.name;
    });

    // Com a Fase 3B, a coluna 4 fica livre para os documentos (PDF, PPTX e Web ficam visíveis na grade)
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

    // Fase 3A: Texto com letreiro na camada 3 (coluna 1), captura de tela e webcam ao lado, cor sólida na camada 2
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

    // Fase 4A: um clip com dois efeitos (desfoque + vinheta) e um gerador ISF numa célula (sem disparar: a camada 3
    // continua mostrando o texto)
    if (!composition_.clip(0, 0).empty()) {
        fx::EffectChain& chain = composition_.clip(0, 0).effects;
        chain.add("native:blur").setParam("radius", media::param::floatJson(6.0f));
        chain.add("native:vignette").setParam("amount", media::param::floatJson(0.7f));
    }
    if (composition_.layerCount() >= 3 && composition_.columnCount() >= 3)
        composition_.setClip(2, 2, Clip::generator("isf:Gradiente animado.fs"));

    // Fase 4B: o primeiro plugin FFGL de efeito que houver nas pastas de plugins vai para o clip (0,1)
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

    // Plugins BSL Light nativos: o GenBackgrounds numa celula (camada 3, coluna 5) e o AlphaReactor no clip com alpha
    // (ou no primeiro clip, se nao houver video com alpha)
    while (composition_.columnCount() < 5)
        composition_.addColumn();
    if (composition_.layerCount() >= 3) {
        Clip bg = Clip::generator(fx::bsllight::kGenBackgroundsId);
        bg.setParam("style", "aurora");
        composition_.setClip(2, 4, std::move(bg));
    }
    // BSL WebBackgrounds (pagina no CEF do processo auxiliar) numa celula livre da camada 2, sem disparar
    if (fx::bsllight::bslWebAvailable() && composition_.layerCount() >= 2) {
        while (composition_.columnCount() < 6)
            composition_.addColumn();
        if (composition_.clip(1, 5).empty()) // coluna 6: a coluna 4 e a dos documentos (Fase 3B, logo abaixo)
            composition_.setClip(1, 5, Clip::generator(fx::bsllight::kWebBackgroundsId));
    }
    {
        Clip& alphaClip = !composition_.clip(1, 0).empty() ? composition_.clip(1, 0) : composition_.clip(0, 0);
        if (!alphaClip.empty())
            alphaClip.effects.add(fx::bsllight::kAlphaReactorId);
    }

    composition_.triggerColumn(0);

    // Fase 3B: PDF, PPTX (convertido pelo Office) e página Web em <pasta>/docs, todos ativos na mesma coluna
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
        composition_.layers[1].opacity = 0.55f; // o slide deixa o PDF aparecer por baixo
        composition_.triggerColumn(col);
        selection_ = {Selection::Kind::Clip, 1, col};
    }
    for (const auto& [layer, column] : options_.triggers)
        composition_.trigger(layer - 1, column - 1);
    if (selection_.kind != Selection::Kind::Clip) // a Fase 3B (acima) escolhe a própria célula
        selection_ = composition_.layerCount() >= 3 ? Selection{Selection::Kind::Clip, 2, 0}
                                                    : Selection{Selection::Kind::Clip, 0, 0};
    if (!options_.triggers.empty())
        selection_ = {Selection::Kind::Clip, options_.triggers.back().first - 1, options_.triggers.back().second - 1};
}

// Miniaturas das celulas de geradores/fontes ao vivo: saem da propria fonte enquanto ela esta ativa (1x/s)
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

    // glReadPixels devolve de baixo para cima; alpha opaco
    std::vector<unsigned char> flipped(pixels.size());
    const size_t stride = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        std::copy_n(&pixels[static_cast<size_t>(h - 1 - y) * stride], stride, &flipped[static_cast<size_t>(y) * stride]);
    }
    writePng(file, w, h, flipped);
}

// --screenshot-output: conteudo do FBO da primeira Screen (o que vai para a janela / Spout)
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

// Fechar a janela: cancela o fechamento e pede ao projeto (que pergunta se há alterações não salvas)
void App::onClose(GLFWwindow* window)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app || !app->project_)
        return;
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
        glfwRestoreWindow(window); // o modal precisa de uma janela visível
    app->project_->requestQuit();
}

void App::onDrop(GLFWwindow* window, int count, const char** droppedPaths)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (!app)
        return;
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y); // GLFW ja atualiza a posicao do mouse para o ponto do drop
    app->drop_.x = static_cast<float>(x);
    app->drop_.y = static_cast<float>(y);
    for (int i = 0; i < count; ++i) {
        const std::string utf8(droppedPaths[i]);
        std::filesystem::path file{std::u8string(utf8.begin(), utf8.end())};
        std::wstring ext = file.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
        if (ext == L".bslstage") { // projeto solto na janela: abre (passando pelo modal de não salvos)
            if (app->project_)
                app->project_->requestOpenPath(file);
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

    // Pedidos de projeto (Novo/Abrir/Salvar/fechar) rodam aqui, fora do desenho do ImGui
    if (quitRequested_) {
        quitRequested_ = false;
        project_->requestQuit();
    }
    project_->update();
    if (project_->quitApproved())
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    if (project_->takeLoaded())
        afterProjectLoaded();

    applyDecoderOptions();
    thumbs_->pump();
    composition_.update(dt);
    updateLiveThumbs();
    syncCanvasSize();
    // O Slice Refiner 2 desenha os slices do proprio BSL Stage (OutputSetup)
    fx::bsllight::SliceFeed::instance().set(fx::bsllight::sliceFeedFromOutput(output_), composition_.width,
                                            composition_.height);
    // A fonte "Padrao de calibracao BSL" desenha os paineis do Pixel Map (o input acompanha o canvas enquanto ligado)
    pixelmap::syncInput(composition_.pixelMap, composition_.width, composition_.height);
    fx::bsllight::PixelMapFeed::instance().set(composition_.pixelMap);

    // IA local: taxa do perfil (Eco = desligada), taxa e dispositivo escolhidos em Ver > IA
    {
        ai::AiEngine& aiEngine = ai::AiEngine::instance();
        aiEngine.setProfileRate(settingsFor(settings_.profile).aiFps);
        aiEngine.setRateOverride(settings_.aiFps);
        aiEngine.setDevicePref(settings_.aiDevice == "gpu"   ? ai::DevicePref::Gpu
                               : settings_.aiDevice == "cpu" ? ai::DevicePref::Cpu
                                                             : ai::DevicePref::Auto);
    }

    // Com saida real o canvas e renderizado a 100% (a escala reduzida do perfil so vale para a previa)
    const bool wantOutputShot = !options_.screenshotOutputPath.empty() && !outputShotDone_;
    const bool outputLive = OutputRenderer::anyLive(output_) || wantOutputShot;
    compositor_->render(composition_, outputLive ? 1.0f : settingsFor(settings_.profile).previewScale,
                        settings_.profile == PerformanceProfile::Eco);

    // Screen que o editor de mapeamento mostra (renderizada mesmo sem destino)
    int previewScreenId = -1;
    if (mode_ == AppMode::Mapping && mapping_.screen >= 0 && mapping_.screen < static_cast<int>(output_.screens.size()))
        previewScreenId = output_.screens[static_cast<size_t>(mapping_.screen)].id;
    else if (wantOutputShot && !output_.screens.empty())
        previewScreenId = output_.screens.front().id;
    outputs_->setFrameRate(composition_.fps);
    outputs_->render(output_, *compositor_, previewScreenId);
    applySwapInterval();

    updateUiScale();
    ui::runLayoutDialogs(glfwGetWin32Window(window_)); // Exportar/Importar layout: dialogos de arquivo fora do desenho
    if (qaOn_) {
        runQaCommands();
        qa::beginFrame();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    PanelContext ctx{composition_, selection_, *compositor_, settings_.profile, stats_, glInfo_,
                     compositor_->uvMax(), settings_, *thumbs_, drop_,
                     quitRequested_, output_, *outputs_, mapping_, mode_, *project_};
    ui::drawUi(ctx);
    if (qaOn_) {
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window_, &fw, &fh);
        glfwGetWindowSize(window_, &ww, &wh);
        qa::endFrame(frameCount_, qaStateJson(), ui::uiScale(), fw, fh, ww, wh);
    }

    if (drop_.pending) { // ninguem consumiu (soltou fora das celulas da Composicao)
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

    if (!qaShotPath_.empty()) { // comando "shot" do driver de teste
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

    glfwSwapBuffers(window_);

    if (settings_.profile != before) {
        applyProfile();
        settings_.save();
    }
}

// Escala da interface (Ver > Tamanho da interface):
//  Window: acompanha a janela, min(largura/1600, altura/900) sobre o framebuffer em pixels fisicos
//          (a DPI do Windows ja entra na conta), em passos de 0,05 com histerese para nao recriar o tema a cada
//          pixel do arraste;
//  System: escala do Windows (DPI do monitor);
//  Fixed:  valor escolhido.
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
            return; // minimizada
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

// Comandos do driver de teste (tools/uiqa.py): shot <arquivo.png> | drop <arquivo>[|<arquivo>...] | quit
//  drop: monta um DROPFILES (como o Explorer) e manda WM_DROPFILES para a janela; o GLFW o converte em
//  glfwSetDropCallback -> App::onDrop, exatamente o caminho de um arquivo solto de verdade (a posicao
//  e a do cursor real, que o driver colocou sobre a celula).
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
            while (pos < arg.size()) { // arquivos separados por '|'
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
            SendMessageW(glfwGetWin32Window(window_), WM_DROPFILES, reinterpret_cast<WPARAM>(mem), 0); // o DragFinish libera
        }
    }
}

std::string App::qaStateJson() const
{
    using nlohmann::json;
    json j;
    const Composition& c = composition_;
    j["mode"] = mode_ == AppMode::Mapping ? "mapping" : mode_ == AppMode::PixelMap ? "pixelmap" : "composition";
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
    // Cadeias de efeitos: [{type, bypass}] (testes de interface conferem a ordem e o bypass)
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
        layers.push_back({{"name", l.name}, {"activeDeck", l.activeDeck}, {"activeColumn", l.activeColumn},
                          {"opacity", l.opacity}, {"bypass", l.bypass}, {"solo", l.solo},
                          {"blend", static_cast<int>(l.blendMode)},
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
        // NDI e Spout: situação do runtime, fontes da rede e senders ativos (testes de interface)
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
        // Pixel Map: configuracao, retangulos calculados (Math.round do HTML), validacao e estado da interface
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
    j["dirty"] = project_ ? project_->dirty() : false;
    j["project"] = project_ ? paths::toUtf8(project_->path()) : std::string();
    j["busy"] = project_ ? project_->busy() : false;
    return j.dump();
}

void App::limitFrameRate(Clock::time_point frameStart) const
{
    const int fps = settingsFor(settings_.profile).uiFps;
    if (fps <= 0)
        return; // vsync cuida disso

    const auto target = frameStart + std::chrono::duration_cast<Clock::duration>(
                                         std::chrono::duration<double>(1.0 / fps));
    // Dorme o grosso do tempo e espera o resto ativamente (precisao sub-ms)
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

} // namespace bsl
