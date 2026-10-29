#pragma once

// Rasterizacao de texto em CPU (stb_truetype): sem GL, testavel isoladamente.
// Gera uma "faixa" RGBA8 (alpha reta). Sem letreiro a faixa tem o tamanho do canvas; com letreiro ela
// tem o tamanho de um periodo da rolagem (texto + espaco) no eixo que rola, e a fonte repete a faixa
// (GL_REPEAT) deslocando o UV: o loop fica sem emenda e nada e re-rasterizado a cada quadro.

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
    std::string text;            // UTF-8, '\n' separa linhas
    std::filesystem::path fontPath;
    float sizePx = 96.0f;
    Color color{1, 1, 1, 1};
    TextHAlign hAlign = TextHAlign::Center;
    TextVAlign vAlign = TextVAlign::Middle;
    int margin = 40;
    bool wrap = true;            // quebra automatica de linha (ignorada no letreiro horizontal)
    float lineSpacing = 1.0f;    // multiplicador da altura de linha
    float letterSpacing = 0.0f;  // px somados ao avanco de cada caractere (tracking)

    bool outline = false;
    Color outlineColor{0, 0, 0, 1};
    float outlineWidth = 4.0f;   // px

    bool shadow = false;
    Color shadowColor{0, 0, 0, 0.7f};
    int shadowX = 4;
    int shadowY = 4;
    int shadowBlur = 4;          // raio do desfoque, px

    bool background = false;
    Color bgColor{0, 0, 0, 1};
    float bgOpacity = 0.6f;
    bool bgFullScreen = false;   // false: caixa ajustada ao texto
    int bgPadding = 24;          // folga da caixa em volta do texto, px

    TextMarquee marquee = TextMarquee::None;
    int marqueeGap = 0;          // px entre repeticoes; 0 = o tamanho da tela no eixo da rolagem
};

struct TextBitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba; // linhas de cima para baixo, alpha reta
    int period = 0;                  // letreiro: largura (horizontal) ou altura (vertical) de um periodo; 0 = sem rolagem
    bool truncated = false;          // o texto passou do limite de textura e foi cortado
    bool fontMissing = false;        // fontPath nao abriu: nada foi desenhado
};

// Rasteriza o texto. 'maxTexture' limita a faixa do letreiro. false (com 'error') se a fonte nao carregar.
bool rasterizeText(const TextStyle& style, int canvasW, int canvasH, int maxTexture, TextBitmap& out,
                   std::string* error = nullptr);

// UTF-8 -> pontos de codigo (bytes invalidos viram U+FFFD; '\r' e ignorado)
std::vector<char32_t> decodeUtf8(const std::string& text);

} // namespace bsl::media
