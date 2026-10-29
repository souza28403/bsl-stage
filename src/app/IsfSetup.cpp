#include "app/IsfSetup.h"

#include "app/Paths.h"
#include "fx/IsfLibrary.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bsl {

void configureIsfLibrary(const Settings& settings)
{
    std::vector<std::filesystem::path> dirs;
    dirs.push_back(paths::isfDir());
    for (const std::string& utf8 : settings.isfFolders)
        dirs.emplace_back(std::u8string(utf8.begin(), utf8.end()));

    std::filesystem::path samples;
    const std::filesystem::path assets = paths::assetsDir();
    if (!assets.empty())
        samples = assets / L"isf";

    fx::IsfLibrary& lib = fx::IsfLibrary::instance();
    lib.configure(std::move(dirs), samples);
    lib.rescan();
}

} // namespace bsl
