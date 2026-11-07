#pragma once

#include "media/SourceParams.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::media {

enum class TextHAlign { Left, Center, Right };
enum class TextVAlign { Top, Middle, Bottom };
enum class TextMarquee { None, Horizontal, Vertical };

struct TextStyle
{
    std::string text;
    std::filesystem::path fontPath;
    float sizePx = 96.0f;
    Color color{1, 1, 1, 1};
    TextHAlign hAlign = TextHAlign::Center;
    TextVAlign vAlign = TextVAlign::Middle;
    int margin = 40;
    bool wrap = true;
    float lineSpacing = 1.0f;
    float letterSpacing = 0.0f;

    bool outline = false;
    Color outlineColor{0, 0, 0, 1};
    float outlineWidth = 4.0f;

    bool shadow = false;
    Color shadowColor{0, 0, 0, 0.7f};
    int shadowX = 4;
    int shadowY = 4;
    int shadowBlur = 4;

    bool background = false;
    Color bgColor{0, 0, 0, 1};
    float bgOpacity = 0.6f;
    bool bgFullScreen = false;
    int bgPadding = 24;

    TextMarquee marquee = TextMarquee::None;
    int marqueeGap = 0;
};

struct TextBitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    int period = 0;
    bool truncated = false;
    bool fontMissing = false;
};

bool rasterizeText(const TextStyle& style, int canvasW, int canvasH, int maxTexture, TextBitmap& out,
                   std::string* error = nullptr);

std::vector<char32_t> decodeUtf8(const std::string& text);

}
