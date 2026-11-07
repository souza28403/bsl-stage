#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::plugin {

constexpr std::uint64_t kMaxPackageBytes = 24u << 20;
constexpr std::uint64_t kMaxEntryBytes = 16u << 20;
constexpr std::uint64_t kMaxTotalBytes = 48u << 20;
constexpr size_t kMaxEntries = 64;
constexpr size_t kMaxShaders = 32;

struct Manifest
{
    std::string name;
    std::string author;
    std::string version;
    std::string license;
    std::string description;
};

struct PackageFile
{
    std::string name;
    std::uint64_t size = 0;
};

struct PackageInfo
{
    Manifest manifest;
    std::vector<PackageFile> files;
    std::vector<std::string> shaders;
    std::uint64_t totalSize = 0;
    std::vector<std::string> warnings;
};

struct PackageResult
{
    bool ok = false;
    std::string error;
};

const std::vector<std::string>& suggestedLicenses();

PackageResult exportPlugin(const std::filesystem::path& shaderFile, const Manifest& manifest, const std::filesystem::path& outFile);

PackageResult inspectPackage(const std::filesystem::path& package, PackageInfo& info);

struct InstallResult
{
    bool ok = false;
    std::string error;
    std::filesystem::path folder;
    std::vector<std::filesystem::path> shaders;
    PackageInfo info;
};

InstallResult installPackage(const std::filesystem::path& package, const std::filesystem::path& destinationParent);

struct ZipItem
{
    std::string name;
    std::string data;
};
std::vector<std::uint8_t> zipStoreItems(const std::vector<ZipItem>& items);

bool checkEntryName(const std::string& name, std::string* why);

}
