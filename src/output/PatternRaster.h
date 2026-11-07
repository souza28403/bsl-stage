#pragma once

#include "output/PixelMap.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::pixelmap {

struct Image
{
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;

    Image() = default;
    Image(int width, int height, std::uint32_t rgb = 0x000000);
    bool empty() const { return w <= 0 || h <= 0; }
    std::uint8_t* px(int x, int y) { return rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4; }
    const std::uint8_t* px(int x, int y) const { return rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4; }
};

struct Color
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};
Color rgb(std::uint32_t hex, float alpha = 1.0f);

enum class FontId { Bold, Mono };

class Surface
{
public:
    explicit Surface(Image& image) : img_(image) {}

    void fillRect(double x, double y, double w, double h, Color c);
    void strokeRect(double x, double y, double w, double h, double lw, Color c);
    void hline(double y, double x0, double x1, double lw, Color c);
    void vline(double x, double y0, double y1, double lw, Color c);
    void ring(double cx, double cy, double r, double lw, Color c, double dashOn = 0.0, double dashOff = 0.0);
    void fillRadial(double cx, double cy, double r1, Color c0, Color c1);
    void fillRampH(double x0, double x1, double y, double h, Color c0, Color c1);
    void drawImage(const Image& src, double x, double y, double w, double h, float alpha = 1.0f, bool smooth = true);
    void text(const std::string& utf8, double cx, double cy, double sizePx, Color c, FontId font);

private:
    Image& img_;
};

double textWidth(const std::string& utf8, double sizePx, FontId font);
bool fontsAvailable();

Image renderPattern(const Panel& p, const Image* logo);
Image renderOverview(const Config& c, const Image* watermark);

bool loadWatermark(const Config& c, Image& out);
bool loadBrandMonogram(Image& out);
bool loadImageFile(const std::filesystem::path& file, Image& out, std::string* error = nullptr);
bool writePng(const std::filesystem::path& file, const Image& img, std::string* error = nullptr);

}
