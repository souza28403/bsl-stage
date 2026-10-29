#pragma once

#include <string>
#include <vector>

namespace bsl {

// Modelo de dados da saída. Só dados simples (sem ponteiros nem handles GL): é o que o
// "salvar/abrir projeto" vai serializar. Quem cuida de janelas/FBOs é o OutputRenderer.
//
//   OutputSetup -> Screen[] -> Slice[]
//
// Coordenadas: a entrada do slice fica em pixels do canvas e a saída em pixels da tela
// (Screen), ambas com origem no canto superior esquerdo e y para baixo.

struct Point2
{
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Point2&) const = default;
};

struct RectI
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const RectI&) const = default;
};

// Ordem dos cantos do quad de saída (sentido horário a partir do canto superior esquerdo)
enum Corner { kCornerTL = 0, kCornerTR = 1, kCornerBR = 2, kCornerBL = 3 };

// Ordem dos lados do soft edge
enum EdgeSide { kEdgeLeft = 0, kEdgeTop = 1, kEdgeRight = 2, kEdgeBottom = 3 };

// Rampa de mistura numa borda do slice: largura em pixels da tela e gamma da curva
struct SoftEdge
{
    float width = 0.0f; // 0 = sem soft edge
    float gamma = 1.0f; // 1 = rampa linear; >1 escurece o miolo da mistura

    bool operator==(const SoftEdge&) const = default;
};

// Roteamento por profundidade (efeito Z-Slice): qual zona o slice mostra. A máscara de zonas vem do Z-Slice ativo na
// fonte escolhida (Master ou uma camada); sem Z-Slice ativo o slice mostra tudo.
enum ZoneFilter { kZoneAll = 0, kZoneFront = 1, kZoneMiddle = 2, kZoneBack = 3, kZone4 = 4 };
inline constexpr int kZoneFilterCount = 5;
inline constexpr int kZoneSourceMaster = -1; // Slice::zSource: -1 = Master; >= 0 = índice da camada (0 = a de baixo)

inline const char* zoneFilterName(int zone)
{
    switch (zone) {
    case kZoneFront: return "Frente";
    case kZoneMiddle: return "Meio";
    case kZoneBack: return "Fundo";
    case kZone4: return "Zona 4";
    default: return "Todas";
    }
}

struct Slice
{
    std::string name;
    bool enabled = true;

    RectI input;               // recorte do canvas, em px
    Point2 corners[4];         // quad de saída em px da tela: TL, TR, BR, BL (warp perspectivo)
    bool flipH = false;
    bool flipV = false;
    float opacity = 1.0f;      // 0..1
    float gain[3] = {1.0f, 1.0f, 1.0f}; // ganho R, G, B (casar painéis LED)
    float brightness = 0.0f;   // soma após o ganho (-1..1)
    SoftEdge edges[4];         // por lado: ver EdgeSide
    int zFilter = kZoneAll;    // ZoneFilter: kZoneAll = sem filtro (sem custo)
    int zSource = kZoneSourceMaster; // de onde vem a máscara de zonas: kZoneSourceMaster ou o índice da camada

    bool operator==(const Slice&) const = default;
};

enum class OutputKind { Disabled, Monitor, Window, Spout, Ndi };

struct Screen
{
    int id = 0; // único e estável durante a sessão (o renderer usa para ligar janelas/FBOs)
    std::string name;
    bool enabled = true;

    OutputKind kind = OutputKind::Disabled;
    int monitorIndex = -1;       // só para Monitor: índice na última vez em que foi visto
    std::string monitorName;     // e o nome, para reencontrar se a ordem mudar
    std::string spoutName;       // só para Spout: nome do sender
    std::string ndiName;         // só para NDI: nome do sender (padrão "BSL Stage - <tela>")
    bool ndiBgra = false;        // só para NDI: false = UYVY (leve), true = BGRA (cheio, com alpha)

    // Resolução da tela. Em Monitor o renderer a mantém igual à do monitor; em Janela/Spout/NDI é livre.
    int width = 1920;
    int height = 1080;

    std::vector<Slice> slices;

    // Saindo de verdade (ativa e com destino)
    bool live() const { return enabled && kind != OutputKind::Disabled; }

    bool operator==(const Screen&) const = default;
};

// Monitor visto pelo GLFW (preenchido pelo OutputRenderer)
struct MonitorInfo
{
    int index = 0;
    std::string name;
    int x = 0; // posição no desktop virtual
    int y = 0;
    int width = 0;
    int height = 0;
    bool primary = false;
};

struct OutputSetup
{
    std::vector<Screen> screens;
    int nextScreenId = 1;

    // Comparacao estrutural (o historico de desfazer do mapeamento usa para saber se algo mudou)
    bool operator==(const OutputSetup&) const = default;

    // Novo projeto: 1 Screen "Tela 1" com 1 Slice cobrindo o canvas, no primeiro monitor
    // secundário se existir (senão Desativado).
    static OutputSetup createDefault(int canvasW, int canvasH, const std::vector<MonitorInfo>& monitors);

    // Cria Screen/Slice novos (com nomes únicos) e devolve o índice
    int addScreen(int canvasW, int canvasH);
    int addSlice(int screenIndex, int canvasW, int canvasH);
    // Duplica; a cópia da Screen nasce com destino Desativado (duas no mesmo monitor conflitam)
    int duplicateScreen(int screenIndex);
    int duplicateSlice(int screenIndex, int sliceIndex);
    void removeScreen(int screenIndex);
    void removeSlice(int screenIndex, int sliceIndex);

    int findScreenIndex(int id) const;

    // O canvas mudou de tamanho: reescala os retângulos de entrada proporcionalmente
    void rescaleInputs(int oldW, int oldH, int newW, int newH);
};

// Slice que cobre o canvas inteiro e preenche a tela inteira
Slice makeFullSlice(const std::string& name, int canvasW, int canvasH, int screenW, int screenH);

// Quad de saída = retângulo de entrada, 1:1 em pixels (mesma posição e tamanho)
void fitOutputToInput(Slice& slice);
// Remove a perspectiva: o quad vira o retângulo que envolve os 4 cantos atuais
void resetWarp(Slice& slice);

const char* outputKindName(OutputKind kind);

} // namespace bsl
