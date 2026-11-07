#pragma once

#include "core/LayerTransform.h"
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

struct Layer
{
    int id = 0;
    std::string name;
    float opacity = 1.0f;
    BlendMode blendMode = BlendMode::Normal;
    bool bypass = false;
    bool solo = false;
    int activeDeck = -1;
    int activeColumn = -1;
    fx::EffectChain effects;
    bool sliceOnly = false;
    LayerTransform transform;
};

}
