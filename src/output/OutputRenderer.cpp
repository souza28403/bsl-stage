#include "output/OutputRenderer.h"

#include "app/Log.h"
#include "output/NdiOutput.h"
#include "output/OutputPresenter.h"
#include "output/SpoutOutput.h"
#include "output/TextLabel.h"
#include "output/Warp.h"
#include "render/Compositor.h"
#include "render/Fbo.h"
#include "render/ZoneMasks.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace bsl {

namespace {

constexpr int kMinScreenSize = 16;
constexpr int kMaxScreenSize = 8192;
constexpr long long kFirstStallLogMs = 3000;
constexpr long long kStallLogStepMs = 10000;

const char* const kFullscreenVertex = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kSliceFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uCanvas;
uniform sampler2D uZoneMask;
uniform int uZone;
uniform vec2 uScreen;
uniform mat3 uInv;
uniform vec3 uLine[4];
uniform vec4 uEdgeW;
uniform vec4 uEdgeG;
uniform vec4 uInput;
uniform vec2 uCanvasSize;
uniform float uCanvasScale;
uniform vec2 uFlip;
uniform float uOpacity;
uniform vec3 uGain;
uniform float uBright;
uniform int uIdentify;
uniform vec3 uIdColor;
uniform vec2 uGridCells;

float ramp(float d, float w, float g)
{
    if (w <= 0.0)
        return 1.0;
    float t = clamp(d / w, 0.0, 1.0);
    return t < 0.5 ? 0.5 * pow(2.0 * t, g) : 1.0 - 0.5 * pow(2.0 * (1.0 - t), g);
}

void main()
{
    vec2 p = vec2(gl_FragCoord.x, uScreen.y - gl_FragCoord.y);
    float dTop = dot(uLine[0].xy, p) + uLine[0].z;
    float dRight = dot(uLine[1].xy, p) + uLine[1].z;
    float dBottom = dot(uLine[2].xy, p) + uLine[2].z;
    float dLeft = dot(uLine[3].xy, p) + uLine[3].z;
    float dMin = min(min(dTop, dRight), min(dBottom, dLeft));

    vec3 q = uInv * vec3(p, 1.0);
    vec2 uv = q.xy / q.z;
    vec2 grid = uv * uGridCells;
    vec2 gridWidth = max(fwidth(grid), vec2(1e-4));

    float cover = clamp(dMin + 0.5, 0.0, 1.0);
    if (cover <= 0.0)
        discard;

    uv = clamp(uv, 0.0, 1.0);
    vec2 s = vec2(uFlip.x > 0.5 ? 1.0 - uv.x : uv.x, uFlip.y > 0.5 ? 1.0 - uv.y : uv.y);
    vec2 cpx = uInput.xy + s * uInput.zw;
    cpx = clamp(cpx, uInput.xy + 0.5, uInput.xy + uInput.zw - 0.5);
    vec2 tc = vec2(cpx.x / uCanvasSize.x, 1.0 - cpx.y / uCanvasSize.y) * uCanvasScale;

    vec3 rgb = max(textureLod(uCanvas, tc, 0.0).rgb * uGain + uBright, 0.0);
    if (uZone >= 0) {
        vec4 zw = textureLod(uZoneMask, vec2(cpx.x / uCanvasSize.x, 1.0 - cpx.y / uCanvasSize.y), 0.0);
        rgb *= uZone == 0 ? zw.r : (uZone == 1 ? zw.g : (uZone == 2 ? zw.b : zw.a));
    }

    float edge = ramp(dLeft, uEdgeW.x, uEdgeG.x) * ramp(dTop, uEdgeW.y, uEdgeG.y)
               * ramp(dRight, uEdgeW.z, uEdgeG.z) * ramp(dBottom, uEdgeW.w, uEdgeG.w);

    if (uIdentify != 0) {
        vec2 gd = abs(fract(grid + 0.5) - 0.5) / gridWidth;
        float line = 1.0 - clamp(min(gd.x, gd.y) - 0.5, 0.0, 1.0);
        float border = 1.0 - clamp(dMin - 5.0, 0.0, 1.0);
        vec3 id = rgb * 0.30;
        id = mix(id, vec3(1.0), line * 0.55);
        id = mix(id, uIdColor, border);
        if (uv.x + uv.y < 0.06)
            id = vec3(1.0);
        rgb = id;
    }

    fragColor = vec4(rgb * (edge * uOpacity * cover), 1.0);
}
)";

const char* const kLabelVertex = R"(#version 330 core
out vec2 vUv;
uniform vec4 uRect;
uniform vec2 uScreen;
void main()
{
    vec2 c = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    vUv = c;
    vec2 p = uRect.xy + c * uRect.zw;
    gl_Position = vec4(p.x / uScreen.x * 2.0 - 1.0, 1.0 - p.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* const kLabelFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main()
{
    fragColor = texture(uTex, vUv);
}
)";

const char* const kBlitFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main()
{
    fragColor = vec4(texture(uTex, vUv).rgb, 1.0);
}
)";

OutputRenderer* g_instance = nullptr;
GLFWmonitorfun g_previousMonitorCallback = nullptr;

const float kIdColors[6][3] = {{0.925f, 0.200f, 0.216f}, {0.184f, 0.498f, 0.878f}, {0.118f, 0.655f, 0.353f},
                               {0.961f, 0.647f, 0.141f}, {0.180f, 0.800f, 0.800f}, {0.800f, 0.300f, 0.800f}};

}

struct OutputRenderer::Runtime
{
    std::unique_ptr<Fbo> fbo;
    bool rendered = false;

    GLFWwindow* window = nullptr;
    std::unique_ptr<OutputPresenter> presenter;
    long long stallLogMs = kFirstStallLogMs;
    OutputKind windowKind = OutputKind::Disabled;
    int monitorX = 0, monitorY = 0, monitorW = 0, monitorH = 0;
    std::string windowTitle;

    std::unique_ptr<SpoutOutput> spout;
    bool spoutWarned = false;

    std::unique_ptr<NdiOutput> ndi;
    bool ndiWarned = false;

    ScreenStatus status = ScreenStatus::Idle;
    bool missingWarned = false;
    std::string failedSignature;
};

OutputRenderer::OutputRenderer(GLFWwindow* mainWindow) : mainWindow_(mainWindow)
{
    sliceShader_ = std::make_unique<Shader>(kFullscreenVertex, kSliceFragment);
    labelShader_ = std::make_unique<Shader>(kLabelVertex, kLabelFragment);
    blitShader_ = std::make_unique<Shader>(kFullscreenVertex, kBlitFragment);
    glGenVertexArrays(1, &vao_);

    refreshMonitors();
    g_instance = this;
    g_previousMonitorCallback = glfwSetMonitorCallback(&OutputRenderer::monitorCallback);
}

OutputRenderer::~OutputRenderer()
{
    glfwSetMonitorCallback(nullptr);
    g_previousMonitorCallback = nullptr;
    g_instance = nullptr;

    for (auto& [id, rt] : runtimes_)
        destroyWindow(*rt);
    runtimes_.clear();
    labels_.clear();
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

void OutputRenderer::monitorCallback(GLFWmonitor* monitor, int event)
{
    if (g_previousMonitorCallback)
        g_previousMonitorCallback(monitor, event);
    if (g_instance)
        g_instance->monitorsDirty_ = true;
}

void OutputRenderer::refreshMonitors()
{
    monitors_.clear();
    monitorHandles_.clear();
    int count = 0;
    GLFWmonitor** list = glfwGetMonitors(&count);
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    for (int i = 0; i < count; ++i) {
        MonitorInfo info;
        info.index = i;
        const char* name = glfwGetMonitorName(list[i]);
        info.name = name ? name : "Monitor";
        glfwGetMonitorPos(list[i], &info.x, &info.y);
        if (const GLFWvidmode* mode = glfwGetVideoMode(list[i])) {
            info.width = mode->width;
            info.height = mode->height;
        }
        info.primary = list[i] == primary;
        monitors_.push_back(std::move(info));
        monitorHandles_.push_back(list[i]);
    }
}

int OutputRenderer::findMonitor(const Screen& s, const std::vector<bool>& claimed) const
{
    int byName = -1;
    for (size_t i = 0; i < monitors_.size(); ++i) {
        if (claimed[i] || s.monitorName.empty() || monitors_[i].name != s.monitorName)
            continue;
        if (static_cast<int>(i) == s.monitorIndex)
            return static_cast<int>(i);
        if (byName < 0)
            byName = static_cast<int>(i);
    }
    if (byName >= 0)
        return byName;
    if (s.monitorName.empty() && s.monitorIndex >= 0 && s.monitorIndex < static_cast<int>(monitors_.size())
        && !claimed[static_cast<size_t>(s.monitorIndex)])
        return s.monitorIndex;
    return -1;
}

bool OutputRenderer::anyLive(const OutputSetup& setup)
{
    return std::any_of(setup.screens.begin(), setup.screens.end(), [](const Screen& s) { return s.live(); });
}

OutputRenderer::Runtime& OutputRenderer::runtimeFor(int screenId)
{
    auto& slot = runtimes_[screenId];
    if (!slot)
        slot = std::make_unique<Runtime>();
    return *slot;
}

int OutputRenderer::ndiConnections(int screenId) const
{
    const auto it = runtimes_.find(screenId);
    if (it == runtimes_.end() || !it->second->ndi)
        return -1;
    return it->second->ndi->connections();
}

ScreenStatus OutputRenderer::status(int screenId) const
{
    const auto it = runtimes_.find(screenId);
    return it == runtimes_.end() ? ScreenStatus::Idle : it->second->status;
}

bool OutputRenderer::screenView(int screenId, ScreenView& out) const
{
    const auto it = runtimes_.find(screenId);
    if (it == runtimes_.end() || !it->second->rendered || !it->second->fbo)
        return false;
    out = {it->second->fbo->texture(), it->second->fbo->width(), it->second->fbo->height()};
    return true;
}

bool OutputRenderer::readScreen(int screenId, std::vector<unsigned char>& rgba, int& width, int& height) const
{
    const auto it = runtimes_.find(screenId);
    if (it == runtimes_.end() || !it->second->rendered || !it->second->fbo)
        return false;
    const Fbo& fbo = *it->second->fbo;
    width = fbo.width();
    height = fbo.height();
    const size_t stride = static_cast<size_t>(width) * 4;
    std::vector<unsigned char> raw(stride * static_cast<size_t>(height));
    fbo.bind();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    Fbo::unbind();

    rgba.resize(raw.size());
    for (int y = 0; y < height; ++y)
        std::copy_n(&raw[static_cast<size_t>(height - 1 - y) * stride], stride, &rgba[static_cast<size_t>(y) * stride]);
    return true;
}

bool OutputRenderer::createWindow(const Screen& screen, Runtime& rt, const MonitorInfo* monitor)
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, monitor ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, monitor ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    int w = 0, h = 0;
    if (monitor) {
        w = monitor->width;
        h = monitor->height;
        glfwWindowHint(GLFW_POSITION_X, monitor->x);
        glfwWindowHint(GLFW_POSITION_Y, monitor->y);
    } else {
        const double k = std::min(1.0, std::min(1280.0 / screen.width, 720.0 / screen.height));
        w = std::max(160, static_cast<int>(screen.width * k));
        h = std::max(90, static_cast<int>(screen.height * k));
        glfwWindowHint(GLFW_POSITION_X, GLFW_ANY_POSITION);
        glfwWindowHint(GLFW_POSITION_Y, GLFW_ANY_POSITION);
    }

    rt.windowTitle = "BSL Stage — Saída: " + screen.name;
    GLFWwindow* win = glfwCreateWindow(w, h, rt.windowTitle.c_str(), nullptr, mainWindow_);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_TRUE);
    glfwWindowHint(GLFW_POSITION_X, GLFW_ANY_POSITION);
    glfwWindowHint(GLFW_POSITION_Y, GLFW_ANY_POSITION);
    if (!win)
        return false;

    if (monitor) {
        glfwSetWindowPos(win, monitor->x, monitor->y);
        glfwSetWindowSize(win, monitor->width, monitor->height);
    }
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    rt.window = win;
    rt.windowKind = monitor ? OutputKind::Monitor : OutputKind::Window;
    rt.presenter = std::make_unique<OutputPresenter>(win, blitShader_->id(), !monitor);
    rt.stallLogMs = kFirstStallLogMs;
    if (monitor) {
        rt.monitorX = monitor->x;
        rt.monitorY = monitor->y;
        rt.monitorW = monitor->width;
        rt.monitorH = monitor->height;
    }
    return true;
}

void OutputRenderer::destroyWindow(Runtime& rt)
{
    if (!rt.window)
        return;
    rt.presenter.reset();
    glfwDestroyWindow(rt.window);
    rt.window = nullptr;
    rt.windowKind = OutputKind::Disabled;
}

void OutputRenderer::syncTarget(Screen& screen, Runtime& rt, std::vector<bool>& claimed)
{
    switch (screen.kind) {
    case OutputKind::Monitor: {
        rt.spout.reset();
        rt.ndi.reset();
        const int idx = findMonitor(screen, claimed);
        if (idx < 0) {
            destroyWindow(rt);
            rt.status = ScreenStatus::MonitorMissing;
            if (!rt.missingWarned) {
                log::warn("Monitor ausente: \"" + screen.monitorName + "\" (" + screen.name + ") — sem saída até reconectar");
                rt.missingWarned = true;
            }
            return;
        }
        if (rt.missingWarned) {
            log::info("Monitor reconectado: \"" + screen.monitorName + "\" (" + screen.name + ")");
            rt.missingWarned = false;
        }
        claimed[static_cast<size_t>(idx)] = true;
        const MonitorInfo& m = monitors_[static_cast<size_t>(idx)];
        screen.monitorIndex = idx;
        screen.monitorName = m.name;
        screen.width = m.width;
        screen.height = m.height;

        const bool moved = rt.window && (rt.windowKind != OutputKind::Monitor || rt.monitorX != m.x
                                         || rt.monitorY != m.y || rt.monitorW != m.width || rt.monitorH != m.height);
        if (moved)
            destroyWindow(rt);
        if (!rt.window) {
            const std::string sig = "m" + std::to_string(idx) + ":" + std::to_string(m.x) + "," + std::to_string(m.y)
                                    + "," + std::to_string(m.width) + "x" + std::to_string(m.height);
            if (rt.failedSignature == sig) {
                rt.status = ScreenStatus::Error;
                return;
            }
            if (!createWindow(screen, rt, &m)) {
                rt.failedSignature = sig;
                rt.status = ScreenStatus::Error;
                log::warn("Não foi possível abrir a janela de saída no monitor \"" + m.name + "\"");
                return;
            }
            rt.failedSignature.clear();
            log::info("Saída \"" + screen.name + "\" aberta em " + m.name + " (" + std::to_string(m.width) + "x"
                      + std::to_string(m.height) + ")");
        }
        if (glfwWindowShouldClose(rt.window))
            glfwSetWindowShouldClose(rt.window, GLFW_FALSE);
        rt.status = ScreenStatus::Active;
        break;
    }
    case OutputKind::Window: {
        rt.spout.reset();
        rt.ndi.reset();
        if (rt.window && rt.windowKind != OutputKind::Window)
            destroyWindow(rt);
        if (!rt.window) {
            const std::string sig = "w";
            if (rt.failedSignature == sig) {
                rt.status = ScreenStatus::Error;
                return;
            }
            if (!createWindow(screen, rt, nullptr)) {
                rt.failedSignature = sig;
                rt.status = ScreenStatus::Error;
                log::warn("Não foi possível abrir a janela de saída \"" + screen.name + "\"");
                return;
            }
            rt.failedSignature.clear();
        }
        if (glfwWindowShouldClose(rt.window)) {
            log::info("Janela de saída \"" + screen.name + "\" fechada: destino desativado");
            destroyWindow(rt);
            screen.kind = OutputKind::Disabled;
            rt.status = ScreenStatus::Idle;
            return;
        }
        const std::string title = "BSL Stage — Saída: " + screen.name;
        if (title != rt.windowTitle) {
            glfwSetWindowTitle(rt.window, title.c_str());
            rt.windowTitle = title;
        }
        rt.status = ScreenStatus::Active;
        break;
    }
    case OutputKind::Spout:
        destroyWindow(rt);
        rt.ndi.reset();
        if (!rt.spout)
            rt.spout = std::make_unique<SpoutOutput>();
        rt.spout->setName(screen.spoutName.empty() ? "BSL Stage" : screen.spoutName);
        rt.status = ScreenStatus::Active;
        break;
    case OutputKind::Ndi:
        destroyWindow(rt);
        rt.spout.reset();
        if (!rt.ndi)
            rt.ndi = std::make_unique<NdiOutput>();
        if (!rt.ndi->runtimeAvailable()) {
            rt.status = ScreenStatus::Error;
            if (!rt.ndiWarned) {
                rt.ndiWarned = true;
                log::warn("NDI: runtime não encontrado; a saída \"" + screen.name + "\" não vai enviar");
            }
            return;
        }
        rt.ndi->setName(screen.ndiName.empty() ? "BSL Stage" : screen.ndiName);
        rt.ndi->setBgra(screen.ndiBgra);
        rt.ndi->setFrameRate(fps_);
        rt.status = ScreenStatus::Active;
        break;
    case OutputKind::Disabled:
        break;
    }
}

unsigned int OutputRenderer::labelTexture(const std::string& line1, float px1, const std::string& line2, float px2,
                                          int& width, int& height)
{
    const std::string key = line1 + "\n" + line2 + "\n" + std::to_string(static_cast<int>(px1));
    auto it = labels_.find(key);
    if (it == labels_.end()) {
        if (labels_.size() > 64)
            labels_.clear();
        const LabelBitmap bmp = renderLabel(line1, px1, line2, px2);
        Label label;
        if (!bmp.empty()) {
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bmp.width, bmp.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         bmp.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            label.texture = Texture(id, bmp.width, bmp.height);
        }
        it = labels_.emplace(key, std::move(label)).first;
    }
    width = it->second.texture.width;
    height = it->second.texture.height;
    return it->second.texture.id;
}

void OutputRenderer::renderScreen(const Screen& screen, Runtime& rt, const Compositor& canvas)
{
    const int sw = screen.width, sh = screen.height;
    if (!rt.fbo || rt.fbo->width() != sw || rt.fbo->height() != sh) {
        try {
            rt.fbo = std::make_unique<Fbo>(sw, sh);
        } catch (const std::exception& e) {
            log::warn(std::string("FBO da Screen \"") + screen.name + "\": " + e.what());
            rt.fbo.reset();
            return;
        }
    }

    rt.fbo->bind();
    glViewport(0, 0, sw, sh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);

    Shader& sh_ = *sliceShader_;
    sh_.use();
    sh_.setInt("uCanvas", 0);
    sh_.setInt("uZoneMask", 1);
    sh_.setInt("uZone", -1);
    sh_.setVec2("uScreen", static_cast<float>(sw), static_cast<float>(sh));
    sh_.setVec2("uCanvasSize", static_cast<float>(canvas.width()), static_cast<float>(canvas.height()));
    sh_.setFloat("uCanvasScale", canvas.uvMax());
    sh_.setInt("uIdentify", identify_ ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, canvas.texture());
    glBindVertexArray(vao_);
    glEnable(GL_SCISSOR_TEST);

    struct Placed
    {
        const Slice* slice;
        Point2 center;
    };
    std::vector<Placed> placed;

    int sliceNumber = -1;
    for (const Slice& slice : screen.slices) {
        ++sliceNumber;
        if (!slice.enabled || slice.input.w < 1 || slice.input.h < 1)
            continue;
        if (slice.opacity <= 0.0f && !identify_)
            continue;

        Mat3 forward, inverse;
        if (!squareToQuad(slice.corners, forward) || !invert(forward, inverse))
            continue;

        Point2 centroid{0.0f, 0.0f};
        for (const Point2& c : slice.corners) {
            centroid.x += c.x * 0.25f;
            centroid.y += c.y * 0.25f;
        }
        float lines[12];
        bool degenerate = false;
        for (int i = 0; i < 4; ++i) {
            const Point2& a = slice.corners[i];
            const Point2& b = slice.corners[(i + 1) % 4];
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-3f) {
                degenerate = true;
                break;
            }
            float nx = -dy / len, ny = dx / len;
            float c = -(nx * a.x + ny * a.y);
            if (nx * centroid.x + ny * centroid.y + c < 0.0f) {
                nx = -nx;
                ny = -ny;
                c = -c;
            }
            lines[i * 3 + 0] = nx;
            lines[i * 3 + 1] = ny;
            lines[i * 3 + 2] = c;
        }
        if (degenerate)
            continue;

        float inv[9];
        for (int i = 0; i < 9; ++i)
            inv[i] = static_cast<float>(inverse.m[i]);
        sh_.setMat3RowMajor("uInv", inv);
        sh_.setVec3Array("uLine", lines, 4);
        sh_.setVec4("uEdgeW", slice.edges[kEdgeLeft].width, slice.edges[kEdgeTop].width,
                    slice.edges[kEdgeRight].width, slice.edges[kEdgeBottom].width);
        sh_.setVec4("uEdgeG", std::max(0.05f, slice.edges[kEdgeLeft].gamma), std::max(0.05f, slice.edges[kEdgeTop].gamma),
                    std::max(0.05f, slice.edges[kEdgeRight].gamma), std::max(0.05f, slice.edges[kEdgeBottom].gamma));
        sh_.setVec4("uInput", static_cast<float>(slice.input.x), static_cast<float>(slice.input.y),
                    static_cast<float>(slice.input.w), static_cast<float>(slice.input.h));
        sh_.setVec2("uFlip", slice.flipH ? 1.0f : 0.0f, slice.flipV ? 1.0f : 0.0f);
        sh_.setFloat("uOpacity", std::clamp(slice.opacity, 0.0f, 1.0f));
        GLuint sourceTex = canvas.texture();
        bool blackSource = false;
        if (slice.source == kSliceFromLayer) {
            GLuint layerTex = 0;
            if (canvas.layerView(slice.layerId, slice.includeMaster, layerTex, slice.applyLayerTransform) && layerTex != 0)
                sourceTex = layerTex;
            else
                blackSource = true;
        }
        if (blackSource && !identify_)
            continue;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sourceTex);
        sh_.setVec3("uGain", blackSource ? 0.0f : slice.gain[0], blackSource ? 0.0f : slice.gain[1],
                    blackSource ? 0.0f : slice.gain[2]);
        sh_.setFloat("uBright", blackSource ? 0.0f : slice.brightness);
        int zone = -1;
        if (slice.zFilter > kZoneAll && slice.zFilter < kZoneFilterCount) {
            ZoneMask zm;
            if (ZoneMasks::instance().find(slice.zSource, zm)) {
                zone = slice.zFilter - 1;
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, zm.texture);
                glActiveTexture(GL_TEXTURE0);
            }
        }
        sh_.setInt("uZone", zone);
        const float* idc = kIdColors[sliceNumber % 6];
        sh_.setVec3("uIdColor", idc[0], idc[1], idc[2]);

        auto dist = [](Point2 a, Point2 b) { return std::hypot(b.x - a.x, b.y - a.y); };
        const float outW = 0.5f * (dist(slice.corners[kCornerTL], slice.corners[kCornerTR])
                                   + dist(slice.corners[kCornerBL], slice.corners[kCornerBR]));
        const float outH = 0.5f * (dist(slice.corners[kCornerTL], slice.corners[kCornerBL])
                                   + dist(slice.corners[kCornerTR], slice.corners[kCornerBR]));
        sh_.setVec2("uGridCells", std::max(2.0f, std::round(outW / 100.0f)), std::max(2.0f, std::round(outH / 100.0f)));

        float minX = slice.corners[0].x, maxX = minX, minY = slice.corners[0].y, maxY = minY;
        for (const Point2& c : slice.corners) {
            minX = std::min(minX, c.x);
            maxX = std::max(maxX, c.x);
            minY = std::min(minY, c.y);
            maxY = std::max(maxY, c.y);
        }
        const int x0 = std::clamp(static_cast<int>(std::floor(minX)) - 2, 0, sw);
        const int x1 = std::clamp(static_cast<int>(std::ceil(maxX)) + 2, 0, sw);
        const int y0 = std::clamp(static_cast<int>(std::floor(minY)) - 2, 0, sh);
        const int y1 = std::clamp(static_cast<int>(std::ceil(maxY)) + 2, 0, sh);
        if (x1 <= x0 || y1 <= y0)
            continue;
        glScissor(x0, sh - y1, x1 - x0, y1 - y0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        placed.push_back({&slice, centroid});
    }
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    if (identify_ && !placed.empty()) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        labelShader_->use();
        labelShader_->setInt("uTex", 0);
        labelShader_->setVec2("uScreen", static_cast<float>(sw), static_cast<float>(sh));
        const float px1 = std::clamp(static_cast<float>(sh) * 0.045f, 18.0f, 140.0f);
        const std::string info = screen.name + "  ·  " + std::to_string(sw) + "x" + std::to_string(sh);
        for (const Placed& p : placed) {
            int lw = 0, lh = 0;
            const unsigned int tex = labelTexture(p.slice->name, px1, info, px1 * 0.5f, lw, lh);
            if (!tex)
                continue;
            const float x = std::clamp(p.center.x - static_cast<float>(lw) * 0.5f, 0.0f, std::max(0.0f, static_cast<float>(sw - lw)));
            const float y = std::clamp(p.center.y - static_cast<float>(lh) * 0.5f, 0.0f, std::max(0.0f, static_cast<float>(sh - lh)));
            labelShader_->setVec4("uRect", x, y, static_cast<float>(lw), static_cast<float>(lh));
            glBindTexture(GL_TEXTURE_2D, tex);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    Fbo::unbind();
    rt.rendered = true;
}

void OutputRenderer::watchStall(Runtime& rt, const Screen& screen)
{
    const long long stalled = rt.presenter->stalledMs();
    if (stalled < kFirstStallLogMs) {
        if (rt.stallLogMs != kFirstStallLogMs) {
            log::info("Saída \"" + screen.name + "\": apresentação retomada");
            rt.stallLogMs = kFirstStallLogMs;
        }
        return;
    }
    if (stalled >= rt.stallLogMs) {
        log::warn("Saída \"" + screen.name + "\": apresentação parada há " + std::to_string(stalled / 1000) + " s");
        rt.stallLogMs = (stalled / kStallLogStepMs + 1) * kStallLogStepMs;
    }
}

void OutputRenderer::render(OutputSetup& setup, const Compositor& canvas, int previewScreenId)
{
    if (monitorsDirty_) {
        monitorsDirty_ = false;
        refreshMonitors();
    }

    for (auto it = runtimes_.begin(); it != runtimes_.end();) {
        if (setup.findScreenIndex(it->first) < 0) {
            destroyWindow(*it->second);
            it = runtimes_.erase(it);
        } else {
            ++it;
        }
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    std::vector<bool> claimed(monitors_.size(), false);
    bool anyWindow = false;

    for (Screen& screen : setup.screens) {
        Runtime& rt = runtimeFor(screen.id);
        rt.rendered = false;
        screen.width = std::clamp(screen.width, kMinScreenSize, kMaxScreenSize);
        screen.height = std::clamp(screen.height, kMinScreenSize, kMaxScreenSize);

        if (screen.live()) {
            syncTarget(screen, rt, claimed);
        } else {
            destroyWindow(rt);
            rt.spout.reset();
            rt.ndi.reset();
            rt.ndiWarned = false;
            rt.status = ScreenStatus::Idle;
            rt.missingWarned = false;
            rt.failedSignature.clear();
        }
        screen.width = std::clamp(screen.width, kMinScreenSize, kMaxScreenSize);
        screen.height = std::clamp(screen.height, kMinScreenSize, kMaxScreenSize);

        const bool output = screen.live() && rt.status == ScreenStatus::Active;
        if (output || screen.id == previewScreenId) {
            renderScreen(screen, rt, canvas);
        } else {
            rt.fbo.reset();
        }

        if (output && rt.rendered) {
            if (rt.spout) {
                if (!rt.spout->send(rt.fbo->texture(), rt.fbo->width(), rt.fbo->height()) && !rt.spoutWarned) {
                    rt.spoutWarned = true;
                    log::warn("Spout: não foi possível enviar \"" + screen.spoutName + "\"");
                }
            }
            if (rt.ndi && !rt.ndi->send(rt.fbo->texture(), rt.fbo->width(), rt.fbo->height()) && !rt.ndiWarned) {
                rt.ndiWarned = true;
                log::warn("NDI: não foi possível enviar \"" + screen.ndiName + "\"");
            }
            if (rt.window && rt.presenter) {
                int fw = 0, fh = 0;
                glfwGetFramebufferSize(rt.window, &fw, &fh);
                rt.presenter->submit(*rt.fbo, fw, fh);
            }
        }
        if (rt.window && rt.presenter) {
            anyWindow = true;
            watchStall(rt, screen);
        }
    }

    windowOutput_ = anyWindow;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

}
