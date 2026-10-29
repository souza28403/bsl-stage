#pragma once

#include "output/OutputSetup.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::pixelmap {

// Modelo e logica pura do Pixel Map (sem ImGui, sem GL): a ferramenta "BSL Light Pixel Mapping Pro" dentro do BSL Stage.
//
//   Config global: tamanho do input (a composicao) e a resolucao fisica total (a da processadora / NovaLCT)
//   Paineis: nome + retangulo FISICO (x, y, w, h em px, como no NovaLCT)
//   Escala: na composicao o painel ocupa  fisico * (input / fisicoTotal)   (Math.round, como no HTML)
//
// Tudo aqui e dado simples: o projeto (.bslstage) grava o Config e os testes rodam sem janela.

inline constexpr int kMaxPanels = 256;
inline constexpr int kMaxDim = 16384; // lado maximo (input, fisico total e painel)

// Paleta do HTML: bordas (coresBorder) e opacidade do preenchimento (coresNeon)
inline constexpr std::uint32_t kPaletteRgb[4] = {0xFF0A33, 0x00E5FF, 0xFFFFFF, 0xFFCC00};
inline constexpr float kPaletteFill[4] = {0.2f, 0.2f, 0.1f, 0.2f};
inline constexpr std::uint32_t kAmberRgb = 0xFFB020;  // sobreposicao
inline constexpr std::uint32_t kInvalidRgb = 0xFF2D2D; // fora da area fisica

struct Panel
{
    std::string name = "Painel";
    int x = 0; // posicao fisica (NovaLCT)
    int y = 0;
    int w = 128; // tamanho fisico real
    int h = 128;
    std::uint32_t color = 0; // 0 = automatica (ciclo da paleta pelo indice); senao 0xRRGGBB (nunca 0)

    // Definicao por gabinetes (como no LEDMapperPro): w = colunas * largura do gabinete, h = linhas * altura
    bool byCabinets = false;
    int cols = 1;
    int rows = 1;
    int cabW = 128;
    int cabH = 128;

    bool operator==(const Panel&) const = default;
};

enum class Watermark { Monogram, Image, None };

struct Config
{
    int inW = 1920; // input = composicao
    int inH = 1080;
    bool linkCanvas = true; // input acompanha o canvas da composicao
    int physW = 2048;       // fisico total (processadora)
    int physH = 640;
    Watermark watermark = Watermark::Monogram;
    std::string watermarkPath; // so para Watermark::Image (UTF-8)
    std::vector<Panel> panels;

    bool operator==(const Config&) const = default;
};

// --- Calculo (igual ao do HTML) ---
// Math.round do JavaScript: meio arredonda para cima (-2,5 -> -2)
double jsRound(double v);

struct Scale
{
    double x = 1.0; // input / fisico total
    double y = 1.0;
};
Scale scaleOf(const Config& c);

struct RectD
{
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};
// Retangulo do painel na composicao, sem arredondar (o que o overview desenha) e arredondado (tabela, mapeamento)
RectD scaledRect(const Config& c, const Panel& p);
RectI scaledRectRounded(const Config& c, const Panel& p);

// Cor de borda do painel (indice = posicao na lista) e opacidade do preenchimento
std::uint32_t panelColor(const Panel& p, int index);
float panelFill(const Panel& p, int index);

// Limites e consistencia (tamanhos >= 1, <= kMaxDim; gabinetes recalculam w/h). Chamar apos editar ou ler do arquivo.
void normalize(Config& c);
void applyCabinets(Panel& p); // se byCabinets, w/h vem dos gabinetes
Panel makePanel(const std::string& name, int x, int y, int w, int h);
// Nome que o "Adicionar painel" sugere: "Painel N"
std::string nextPanelName(const Config& c);
// O input acompanha o canvas
void syncInput(Config& c, int canvasW, int canvasH);

// --- Validacao ---
struct Validation
{
    std::vector<bool> overlap; // por painel: sobrepoe outro (area positiva) -> ambar
    std::vector<bool> outside; // por painel: sai da area fisica total ou tem tamanho invalido -> vermelho
    bool scaleWarn = false;    // escala nao inteira em algum eixo
    int overlapCount = 0;
    int outsideCount = 0;
};
Validation validate(const Config& c);
// Escala "limpa": 1:1, multiplo inteiro (2x, 3x...) ou divisor inteiro (1/2, 1/3...)
bool isCleanScale(double s);

// --- CSV (Excel pt-BR: ';' e UTF-8 com BOM; a leitura aceita ';' ',' e tab) ---
// Colunas: nome;fisico_x;fisico_y;fisico_w;fisico_h;comp_x;comp_y;comp_w;comp_h
std::string toCsv(const Config& c);
// Le os paineis (so as colunas fisicas contam; as da composicao sao recalculadas). false + 'error' se nao deu.
bool fromCsv(const std::string& text, std::vector<Panel>& out, std::string* error = nullptr);

// Nome do arquivo do padrao de um painel: BSLLight_Pattern_<nome>_<w>x<h>.png (caracteres invalidos viram '_')
std::string patternFileName(const Panel& p);
std::string sanitizeFileName(const std::string& name);

// Texto em maiusculas (ASCII e Latin-1; o suficiente para os nomes em portugues)
std::string upperUtf8(const std::string& s);

// --- Ponte com o mapeamento (OutputSetup) ---
enum class MapMode { Replace, Add };
// Cria (Add) ou atualiza (Replace) a Screen: resolucao = fisico total, um Slice por painel. A entrada do slice e o retangulo
// escalado NO CANVAS REAL (canvasW x canvasH; se o input do Pixel Map difere do canvas, escala de novo) e a saida e a
// posicao fisica. Devolve o indice da Screen (Replace sem 'targetScreen' valido cria uma).
int createMapping(const Config& c, OutputSetup& out, MapMode mode, int targetScreen, int canvasW, int canvasH);
// O inverso: fisico total = resolucao da Screen e um painel por slice (retangulo que envolve os cantos de saida).
// Mantem a marca d'agua e o input. false se a Screen nao tem slices.
bool importFromScreen(Config& c, const Screen& screen);

} // namespace bsl::pixelmap
