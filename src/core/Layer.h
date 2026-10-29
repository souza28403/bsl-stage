#pragma once

#include "fx/EffectChain.h"

#include <string>

namespace bsl {

enum class BlendMode { Normal, Add, Subtract, Multiply, Screen, Overlay, Lighten, Darken, Difference };

inline constexpr int kBlendModeCount = 9;

inline const char* blendModeName(BlendMode mode)
{
    static const char* const names[kBlendModeCount] = {
        "Normal", "Adicionar", "Subtrair", "Multiplicar", "Tela",
        "Sobrepor", "Clarear", "Escurecer", "Diferença"};
    return names[static_cast<int>(mode)];
}

// Camadas sao globais (valem para todos os decks); so a grade de clips e por deck.
struct Layer
{
    std::string name;
    float opacity = 1.0f;  // 0..1
    BlendMode blendMode = BlendMode::Normal;
    bool bypass = false;   // ignora a camada na composicao
    bool solo = false;     // se alguma camada esta em solo, so as em solo aparecem
    int activeDeck = -1;   // deck do clip tocando (-1 = nenhum)
    int activeColumn = -1; // coluna do clip tocando (-1 = nenhuma)
    fx::EffectChain effects; // efeitos da camada (no tamanho do canvas, antes do blend)
};

} // namespace bsl
