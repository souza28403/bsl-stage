#pragma once

#include "output/OutputSetup.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::pixelmap {

inline constexpr int kMaxPanels = 256;
inline constexpr int kMaxDim = 16384;

inline constexpr std::uint32_t kPaletteRgb[4] = {0xFF0A33, 0x00E5FF, 0xFFFFFF, 0xFFCC00};
inline constexpr float kPaletteFill[4] = {0.2f, 0.2f, 0.1f, 0.2f};
inline constexpr std::uint32_t kAmberRgb = 0xFFB020;
inline constexpr std::uint32_t kInvalidRgb = 0xFF2D2D;

struct Panel
{
    std::string name = "Painel";
    int x = 0;
    int y = 0;
    int w = 128;
    int h = 128;
    std::uint32_t color = 0;

    bool byCabinets = false;
    int cols = 1;
    int rows = 1;
    int cabW = 128;
    int cabH = 128;

    int port = 0;
    int cableOrder = -1;

    bool operator==(const Panel&) const = default;
};

enum class Watermark { Monogram, Image, None };

struct Config
{
    int inW = 1920;
    int inH = 1080;
    bool linkCanvas = true;
    int physW = 2048;
    int physH = 640;
    Watermark watermark = Watermark::Monogram;
    std::string watermarkPath;
    std::vector<Panel> panels;

    bool operator==(const Config&) const = default;
};

double jsRound(double v);

struct Scale
{
    double x = 1.0;
    double y = 1.0;
};
Scale scaleOf(const Config& c);

struct RectD
{
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};
RectD scaledRect(const Config& c, const Panel& p);
RectI scaledRectRounded(const Config& c, const Panel& p);

std::uint32_t panelColor(const Panel& p, int index);
float panelFill(const Panel& p, int index);

void normalize(Config& c);
void applyCabinets(Panel& p);
Panel makePanel(const std::string& name, int x, int y, int w, int h);
std::string nextPanelName(const Config& c);
void syncInput(Config& c, int canvasW, int canvasH);

struct Validation
{
    std::vector<bool> overlap;
    std::vector<bool> outside;
    bool scaleWarn = false;
    int overlapCount = 0;
    int outsideCount = 0;
};
Validation validate(const Config& c);
bool isCleanScale(double s);

std::string toCsv(const Config& c);
bool fromCsv(const std::string& text, std::vector<Panel>& out, std::string* error = nullptr);

std::string patternFileName(const Panel& p);
std::string sanitizeFileName(const std::string& name);

std::string upperUtf8(const std::string& s);

enum class MapMode { Replace, Add };
int createMapping(const Config& c, OutputSetup& out, MapMode mode, int targetScreen, int canvasW, int canvasH);
bool importFromScreen(Config& c, const Screen& screen);

}
