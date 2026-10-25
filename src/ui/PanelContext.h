#pragma once

#include "app/PerformanceProfile.h"
#include "app/Settings.h"
#include "core/Composition.h"
#include "output/OutputSetup.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl {

class Compositor;
class OutputRenderer;
class ProjectSession;
class ThumbnailCache;

// Layout da janela: Composicao (clips/camadas) ou Mapeamento (screens/slices)
enum class AppMode { Composition, Mapping };

// Selecao no editor de mapeamento (indices em OutputSetup; validados a cada frame)
struct MappingSelection
{
    int screen = 0;
    int slice = 0;   // -1 = so a Screen
    int corner = -1; // canto selecionado no painel Saida (-1 = nenhum)
};

struct Selection
{
    enum class Kind { None, Layer, Clip };

    Kind kind = Kind::None;
    int layer = -1;
    int column = -1; // so vale para Kind::Clip
};

struct FrameStats
{
    float fps = 0.0f;
    float frameMs = 0.0f; // intervalo medio entre frames
    float workMs = 0.0f;  // tempo medio de trabalho (sem a espera do limitador)
};

struct GlInfo
{
    std::string version;
    std::string gpu;
};

// Arquivos soltos na janela vindos do Explorer (glfwSetDropCallback). A Composição
// (celulas da grade) consome o pedido no frame seguinte, conforme a celula sob o mouse.
struct DropRequest
{
    bool pending = false;
    float x = 0.0f; // posicao do mouse na janela (coordenadas do ImGui)
    float y = 0.0f;
    std::vector<std::filesystem::path> files;
};

// Tudo que os paineis precisam, repassado a cada frame
struct PanelContext
{
    Composition& composition;
    Selection& selection;
    const Compositor& canvas;
    PerformanceProfile& profile;
    const FrameStats& stats;
    const GlInfo& glInfo;
    float previewScale;
    Settings& settings;
    ThumbnailCache& thumbs;
    DropRequest& drop;
    bool& quitRequested;
    OutputSetup& output;
    OutputRenderer& outputRenderer;
    MappingSelection& mapping;
    AppMode& mode;
    ProjectSession& project;
};

} // namespace bsl
