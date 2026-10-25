#pragma once

#include "render/Texture.h"

namespace bsl::ui {

// Imagens da marca BSL Light (assets/brand), carregadas uma vez com o contexto GL ativo.
struct Brand
{
    Texture horizontal; // bsl-horizontal-white.png
    Texture monogram;   // bsl-monogram.png
};

void loadBrand();
void unloadBrand(); // antes de destruir o contexto GL
const Brand& brand();

} // namespace bsl::ui
