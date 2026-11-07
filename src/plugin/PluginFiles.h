#pragma once

#include "plugin/PluginTemplates.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::plugin {

std::string safeFileName(const std::string& name);

std::filesystem::path uniqueFile(const std::filesystem::path& folder, const std::string& stem, const char* ext = ".fs");

struct CreateResult
{
    bool ok = false;
    std::filesystem::path file;
    std::string error;
};

CreateResult createPlugin(const PluginSpec& spec, const std::filesystem::path& folder);

CreateResult duplicatePlugin(const std::filesystem::path& file, const std::filesystem::path& targetFolder = {});

bool writeTextAtomic(const std::filesystem::path& file, const std::string& text, std::string* error = nullptr);
bool readTextFile(const std::filesystem::path& file, std::string& out, size_t maxBytes = 4u << 20);

bool isPluginImageExtension(const std::string& lowerExt);

}
