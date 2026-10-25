#pragma once

#include <string>
#include <vector>

namespace bsl {

struct LabelBitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba; // linha 0 = topo; não pré-multiplicado
    bool empty() const { return width <= 0 || height <= 0; }
};

// Placa escura translúcida com 2 linhas de texto centralizadas (Barlow Bold via stb_truetype).
// Usado pelo modo "Identificar" para escrever o nome dos slices nas saídas reais.
// Vazio se a fonte não for encontrada em assets/fonts.
LabelBitmap renderLabel(const std::string& line1, float pixelHeight1, const std::string& line2, float pixelHeight2);

} // namespace bsl
