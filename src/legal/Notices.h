#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::legal {

inline constexpr int kNoticeVersion = 1;

struct ThirdParty
{
    std::string name;
    std::string version;
    std::string license;
    std::string copyright;
    std::string url;
    std::string usage;
    std::string status;
    std::vector<std::string> licenseFiles;
};

std::vector<ThirdParty> parseThirdParty(const std::string& markdown);

bool safeLicensePath(const std::string& relative);

std::string readTextFile(const std::filesystem::path& file, size_t maxBytes = 4u * 1024u * 1024u);

std::filesystem::path findDocsRoot(const std::filesystem::path& startDir);
std::filesystem::path docsRoot();

std::vector<std::string> wrapText(const std::string& text, int columns);

}
