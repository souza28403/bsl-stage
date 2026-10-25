#include "ui/Brand.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "render/TextureLoader.h"

namespace bsl::ui {

namespace {
Brand g_brand;
}

void loadBrand()
{
    const std::filesystem::path dir = paths::assetsDir();
    if (dir.empty()) {
        log::warn("Pasta assets não encontrada: a marca BSL não será exibida.");
        return;
    }
    g_brand.horizontal = loadTexture(dir / L"brand" / L"bsl-horizontal-white.png");
    g_brand.monogram = loadTexture(dir / L"brand" / L"bsl-monogram.png");
    if (!g_brand.horizontal || !g_brand.monogram)
        log::warn("Não foi possível carregar as imagens de assets/brand.");
}

void unloadBrand()
{
    g_brand = Brand{};
}

const Brand& brand()
{
    return g_brand;
}

} // namespace bsl::ui
