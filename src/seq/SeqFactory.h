#pragma once

#include "seq/SeqModel.h"

#include <vector>

namespace bsl {
struct OutputSetup;
}

namespace bsl::seq {

struct GeoSlice
{
    int uid = 0;
    float cx = 0.0f;
    float cy = 0.0f;
    float largura = 0.0f;
    float altura = 0.0f;
};

struct Grade
{
    std::vector<float> bordasX;
    std::vector<float> bordasY;

    int colunas() const { return static_cast<int>(bordasX.size()) + 1; }
    int linhas() const { return static_cast<int>(bordasY.size()) + 1; }
    int colunaDe(float cx) const;
    int linhaDe(float cy) const;
};

std::vector<GeoSlice> coletarGeometria(const OutputSetup& saida);

Grade detectarGrade(const std::vector<GeoSlice>& slices, float limiar);
Grade gradeDoGerador(const std::vector<GeoSlice>& slices, const ParametrosGerador& parametros);

std::vector<Passo> gerarPassos(const std::vector<GeoSlice>& slices, const ParametrosGerador& parametros,
                               Grade* grade = nullptr);

void aplicarGerador(Sequencia& sequencia, const std::vector<GeoSlice>& slices);

}
