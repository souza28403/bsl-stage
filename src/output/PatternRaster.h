#pragma once

#include "output/PixelMap.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::pixelmap {

// Rasterizacao em CPU dos desenhos do Pixel Map (sem GL): o padrao de calibracao HD de um painel e o overview.
// Reproduz, em pixels, o que o canvas 2D do HTML original desenha ("BSL Light Pixel Mapping Pro"), com Barlow Bold no lugar
// da Rajdhani e JetBrains Mono no lugar do "monospace". Usado pelo PNG exportado e pela fonte "Padrao de calibracao BSL".

struct Image
{
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba; // linhas de cima para baixo, alpha reta

    Image() = default;
    Image(int width, int height, std::uint32_t rgb = 0x000000);
    bool empty() const { return w <= 0 || h <= 0; }
    std::uint8_t* px(int x, int y) { return rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4; }
    const std::uint8_t* px(int x, int y) const { return rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4; }
};

struct Color
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; // 0..1
};
Color rgb(std::uint32_t hex, float alpha = 1.0f);

enum class FontId { Bold, Mono }; // Barlow Bold e JetBrains Mono (assets/fonts)

// Superficie com a API do canvas 2D do HTML (coordenadas decimais, cobertura fracionaria nas bordas = antisserrilhado)
class Surface
{
public:
    explicit Surface(Image& image) : img_(image) {}

    void fillRect(double x, double y, double w, double h, Color c);
    // Contorno centrado nas bordas (como strokeRect do canvas), largura 'lw'
    void strokeRect(double x, double y, double w, double h, double lw, Color c);
    // Linhas horizontais/verticais com largura 'lw' centrada na coordenada (como lineTo do canvas)
    void hline(double y, double x0, double x1, double lw, Color c);
    void vline(double x, double y0, double y1, double lw, Color c);
    // Circulo (arco completo, a partir do angulo 0 no sentido horario); dashOn/dashOff > 0 tracejam
    void ring(double cx, double cy, double r, double lw, Color c, double dashOn = 0.0, double dashOff = 0.0);
    // Preenche a imagem toda com um degrade radial de c0 (no centro) a c1 (a 'r1' do centro)
    void fillRadial(double cx, double cy, double r1, Color c0, Color c1);
    // Retangulo com degrade horizontal de c0 (em x0) a c1 (em x1)
    void fillRampH(double x0, double x1, double y, double h, Color c0, Color c1);
    // Desenha 'src' no retangulo; 'smooth' = filtro suave (media ao reduzir), senao pixel mais proximo
    void drawImage(const Image& src, double x, double y, double w, double h, float alpha = 1.0f, bool smooth = true);
    // Texto centralizado em (cx, cy) (alinhamento "center", linha de base "middle"), tamanho = em em px
    void text(const std::string& utf8, double cx, double cy, double sizePx, Color c, FontId font);

private:
    Image& img_;
};

// Largura do texto (px) no tamanho dado; 0 se a fonte nao carregou
double textWidth(const std::string& utf8, double sizePx, FontId font);
// As fontes foram encontradas em assets/fonts?
bool fontsAvailable();

// Padrao de calibracao HD do painel, na resolucao fisica (p.w x p.h). 'logo' = marca no centro (nulo = sem).
Image renderPattern(const Panel& p, const Image* logo);
// Overview (mapa da composicao) do tamanho do input: grade, marca d'agua, paineis coloridos com nome e WxH
Image renderOverview(const Config& c, const Image* watermark);

// Marca d'agua escolhida na configuracao (monograma BSL, imagem do usuario ou nada). false = sem marca/nao carregou.
bool loadWatermark(const Config& c, Image& out);
bool loadBrandMonogram(Image& out);
bool loadImageFile(const std::filesystem::path& file, Image& out, std::string* error = nullptr);
bool writePng(const std::filesystem::path& file, const Image& img, std::string* error = nullptr);

} // namespace bsl::pixelmap
