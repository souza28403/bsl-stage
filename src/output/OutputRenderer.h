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
    Idle,           // desativada (ou sem destino)
    Active,         // saindo
    MonitorMissing, // destino Monitor, mas o monitor não está conectado
    Error           // falhou ao abrir janela / sender
};

struct ScreenView
{
    unsigned int texture = 0; // textura GL do FBO da Screen (origem embaixo)
    int width = 0;
    int height = 0;
};

// Renderiza cada Screen (slices com warp perspectivo, soft edge e mistura aditiva) num FBO do
// tamanho dela e entrega o resultado à janela de saída (monitor ou janela comum) e/ou ao sender
// Spout. O mesmo FBO alimenta a miniatura do editor de mapeamento.
//
// Contextos GL: cada janela de saída tem contexto próprio compartilhado com o principal.
// Texturas e programas são compartilhados; FBOs e VAOs não (um VAO por contexto).
class OutputRenderer
{
public:
    explicit OutputRenderer(GLFWwindow* mainWindow); // contexto principal atual
    ~OutputRenderer();
    OutputRenderer(const OutputRenderer&) = delete;
    OutputRenderer& operator=(const OutputRenderer&) = delete;

    const std::vector<MonitorInfo>& monitors() const { return monitors_; }

    // Chamar uma vez por frame, depois do Compositor::render, com o contexto principal atual.
    // Ajusta 'setup' quando o hardware manda (resolução do monitor, janela de teste fechada).
    // 'previewScreenId': Screen que o editor quer ver mesmo sem saída (-1 = nenhuma).
    void render(OutputSetup& setup, const Compositor& canvas, int previewScreenId);

    // Textura renderizada neste frame para a Screen (false se não foi renderizada)
    bool screenView(int screenId, ScreenView& out) const;
    // Lê o FBO da Screen (linhas de cima para baixo, RGBA8)
    bool readScreen(int screenId, std::vector<unsigned char>& rgba, int& width, int& height) const;
    ScreenStatus status(int screenId) const;
    // Receptores NDI conectados à Screen (-1 se ela não está saindo por NDI)
    int ndiConnections(int screenId) const;
    // Frame rate da composição: o ritmo do envio NDI
    void setFrameRate(float fps) { fps_ = fps; }

    // "Identificar": borda + nome + grade de teste por cima das saídas
    void setIdentify(bool on) { identify_ = on; }
    bool identify() const { return identify_; }

    // Há uma janela de saída com vsync (o loop principal passa a ser ritmado por ela)
    bool hasVsyncWindow() const { return vsyncWindow_; }

    // Já existe alguma Screen saindo? (o App renderiza o canvas a 100% nesse caso)
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
    void present(Runtime& rt, const Screen& screen, bool vsync);
    unsigned int labelTexture(const std::string& line1, float px1, const std::string& line2, float px2,
                              int& width, int& height);

    GLFWwindow* mainWindow_ = nullptr;
    std::vector<MonitorInfo> monitors_;
    std::vector<GLFWmonitor*> monitorHandles_;
    bool monitorsDirty_ = false;
    bool identify_ = false;
    bool vsyncWindow_ = false;
    float fps_ = 30.0f;

    std::map<int, std::unique_ptr<Runtime>> runtimes_;
    std::unique_ptr<Shader> sliceShader_;
    std::unique_ptr<Shader> labelShader_;
    std::unique_ptr<Shader> blitShader_;
    unsigned int vao_ = 0; // VAO do contexto principal (vértices vêm de gl_VertexID)

    struct Label
    {
        Texture texture;
    };
    std::map<std::string, Label> labels_;
};

} // namespace bsl
