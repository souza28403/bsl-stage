#pragma once

#include "render/Texture.h"

namespace bsl::ui {

struct Brand
{
    Texture horizontal;
    Texture monogram;
};

void loadBrand();
void unloadBrand();
const Brand& brand();

}
