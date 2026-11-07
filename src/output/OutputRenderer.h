#pragma once

#include "output/OutputSetup.h"
#include "render/Shader.h"
#include "render/Texture.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWmonitor;

namespace bsl {

class Compositor;

enum class ScreenStatus
{
    Idle,
    Active,
    MonitorMissing,
    Error
};

struct ScreenView
{
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
};

class OutputRenderer
{
public:
    explicit OutputRenderer(GLFWwindow* mainWindow);
    ~OutputRenderer();
    OutputRenderer(const OutputRenderer&) = delete;
    OutputRenderer& operator=(const OutputRenderer&) = delete;

    const std::vector<MonitorInfo>& monitors() const { return monitors_; }

    void render(OutputSetup& setup, const Compositor& canvas, int previewScreenId);

    bool screenView(int screenId, ScreenView& out) const;
    bool readScreen(int screenId, std::vector<unsigned char>& rgba, int& width, int& height) const;
    ScreenStatus status(int screenId) const;
    int ndiConnections(int screenId) const;
    void setFrameRate(float fps) { fps_ = fps; }

    void setIdentify(bool on) { identify_ = on; }
    bool identify() const { return identify_; }

    bool hasWindowOutput() const { return windowOutput_; }

    static bool anyLive(const OutputSetup& setup);

private:
    struct Runtime;

    static void monitorCallback(GLFWmonitor* monitor, int event);
    void refreshMonitors();
    int findMonitor(const Screen& screen, const std::vector<bool>& claimed) const;

    Runtime& runtimeFor(int screenId);
    void syncTarget(Screen& screen, Runtime& rt, std::vector<bool>& claimed);
    void destroyWindow(Runtime& rt);
    bool createWindow(const Screen& screen, Runtime& rt, const MonitorInfo* monitor);
    void renderScreen(const Screen& screen, Runtime& rt, const Compositor& canvas);
    void watchStall(Runtime& rt, const Screen& screen);
    unsigned int labelTexture(const std::string& line1, float px1, const std::string& line2, float px2,
                              int& width, int& height);

    GLFWwindow* mainWindow_ = nullptr;
    std::vector<MonitorInfo> monitors_;
    std::vector<GLFWmonitor*> monitorHandles_;
    bool monitorsDirty_ = false;
    bool identify_ = false;
    bool windowOutput_ = false;
    float fps_ = 30.0f;

    std::map<int, std::unique_ptr<Runtime>> runtimes_;
    std::unique_ptr<Shader> sliceShader_;
    std::unique_ptr<Shader> labelShader_;
    std::unique_ptr<Shader> blitShader_;
    unsigned int vao_ = 0;

    struct Label
    {
        Texture texture;
    };
    std::map<std::string, Label> labels_;
};

}
