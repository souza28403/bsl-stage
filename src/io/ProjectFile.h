#pragma once

#include "core/Composition.h"
#include "output/OutputSetup.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::project {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFormatName = "bslstage";
inline constexpr const char* kExtension = ".bslstage";

struct LoadResult
{
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
    int missingFiles = 0;
};

std::string serialize(const Composition& comp, const OutputSetup& output, const std::filesystem::path& projectDir);

LoadResult parse(const std::string& text, const std::filesystem::path& projectDir, Composition& comp,
                 OutputSetup& output);

bool saveFile(const std::filesystem::path& file, const std::string& text, std::string* error);
LoadResult loadFile(const std::filesystem::path& file, Composition& comp, OutputSetup& output);

int relinkMissingInFolder(Composition& comp, const std::filesystem::path& folder);

bool relinkClip(Clip& clip, const std::filesystem::path& file);

}
