#include "app/FfglSetup.h"

#include "app/Paths.h"
#include "ffgl/FfglCatalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl {

void configureFfglCatalog(const Settings& settings)
{
    namespace fs = std::filesystem;
    std::vector<ffgl::Folder> folders;
    const fs::path mine = paths::pluginsDir();
    std::error_code ec;
    fs::create_directories(mine, ec);
    folders.push_back({mine, "Meus plugins"});

    for (const std::string& utf8 : settings.ffglFolders) {
        const fs::path dir{std::u8string(utf8.begin(), utf8.end())};
        std::string label = paths::toUtf8(dir.filename());
        if (label.empty())
            label = utf8;
        folders.push_back({dir, label});
    }
    if (settings.ffglUseResolume)
        for (const fs::path& dir : paths::resolumeEffectDirs())
            folders.push_back({dir, "Resolume Extra Effects"});

    ffgl::Catalog& catalog = ffgl::Catalog::instance();
    catalog.configure(std::move(folders));
    catalog.rescan();
}

} // namespace bsl
