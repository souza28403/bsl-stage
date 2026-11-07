#pragma once

#include "update/SemVer.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bsl::update {

struct Asset
{
    std::string name;
    std::string url;
    int64_t size = 0;
};

struct Release
{
    std::string tag;
    std::string name;
    std::string body;
    std::string htmlUrl;
    std::string published;
    std::string raw;
    SemVer version;
    bool prerelease = false;
    bool draft = false;
    std::vector<Asset> assets;

    bool isBeta() const { return prerelease || version.isPrerelease(); }
};

bool parseRelease(std::string_view jsonText, Release& out, std::string* error = nullptr);
bool parseReleaseList(std::string_view jsonText, std::vector<Release>& out, std::string* error = nullptr);

int pickNewest(const std::vector<Release>& releases, bool includeBeta);

enum class AssetKind { None, Zip, Installer };

struct AssetChoice
{
    AssetKind kind = AssetKind::None;
    const Asset* asset = nullptr;
};

AssetChoice chooseAsset(const Release& release);
const Asset* findChecksumsAsset(const Release& release);

std::map<std::string, std::string> parseChecksums(std::string_view text);

bool isTrustedDownloadUrl(std::string_view url);

std::string safeFileName(std::string_view name);

}
