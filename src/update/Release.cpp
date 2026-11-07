#include "update/Release.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace bsl::update {

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool endsWith(const std::string& s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string& s, std::string_view part)
{
    return s.find(part) != std::string::npos;
}

std::string str(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool releaseFromJson(const nlohmann::json& j, Release& out)
{
    if (!j.is_object())
        return false;
    Release r;
    r.tag = str(j, "tag_name");
    r.name = str(j, "name");
    r.body = str(j, "body");
    r.htmlUrl = str(j, "html_url");
    r.published = str(j, "published_at");
    r.raw = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    r.draft = j.value("draft", false);
    r.prerelease = j.value("prerelease", false);
    r.version = parseSemVer(r.tag);
    if (const auto it = j.find("assets"); it != j.end() && it->is_array()) {
        for (const auto& a : *it) {
            if (!a.is_object())
                continue;
            Asset asset;
            asset.name = str(a, "name");
            asset.url = str(a, "browser_download_url");
            if (a.contains("size") && a["size"].is_number_integer())
                asset.size = a["size"].get<int64_t>();
            if (!asset.name.empty() && !asset.url.empty())
                r.assets.push_back(std::move(asset));
        }
    }
    if (r.tag.empty())
        return false;
    out = std::move(r);
    return true;
}

}

bool parseRelease(std::string_view jsonText, Release& out, std::string* error)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(jsonText.begin(), jsonText.end());
        if (!releaseFromJson(j, out)) {
            if (error)
                *error = "resposta sem tag_name (não parece um release)";
            return false;
        }
        return true;
    } catch (const nlohmann::json::exception& e) {
        if (error)
            *error = std::string("JSON inválido: ") + e.what();
        return false;
    }
}

bool parseReleaseList(std::string_view jsonText, std::vector<Release>& out, std::string* error)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(jsonText.begin(), jsonText.end());
        if (!j.is_array()) {
            if (error)
                *error = "esperava uma lista de releases";
            return false;
        }
        out.clear();
        for (const auto& item : j) {
            Release r;
            if (releaseFromJson(item, r))
                out.push_back(std::move(r));
        }
        return true;
    } catch (const nlohmann::json::exception& e) {
        if (error)
            *error = std::string("JSON inválido: ") + e.what();
        return false;
    }
}

int pickNewest(const std::vector<Release>& releases, bool includeBeta)
{
    int best = -1;
    for (size_t i = 0; i < releases.size(); ++i) {
        const Release& r = releases[i];
        if (r.draft || !r.version.valid)
            continue;
        if (r.isBeta() && !includeBeta)
            continue;
        if (best < 0 || compare(r.version, releases[static_cast<size_t>(best)].version) > 0)
            best = static_cast<int>(i);
    }
    return best;
}

AssetChoice chooseAsset(const Release& release)
{
    const std::string exact = lower("BSL-Stage-" + (release.version.valid ? release.version.toString() : release.tag) + "-win64.zip");
    const Asset* best = nullptr;
    int bestScore = 0;
    for (const Asset& a : release.assets) {
        const std::string n = lower(a.name);
        if (!endsWith(n, ".zip"))
            continue;
        int score = 10;
        if (contains(n, "source") || contains(n, "src"))
            score = 0;
        else if (n == exact)
            score = 100;
        else if (contains(n, "win64") || contains(n, "x64"))
            score = 60;
        else if (contains(n, "bsl"))
            score = 20;
        if (score > bestScore) {
            best = &a;
            bestScore = score;
        }
    }
    if (best)
        return {AssetKind::Zip, best};

    for (const Asset& a : release.assets) {
        const std::string n = lower(a.name);
        if (!endsWith(n, ".exe") || contains(n, "updater"))
            continue;
        if (contains(n, "setup") || contains(n, "installer") || contains(n, "win64"))
            return {AssetKind::Installer, &a};
    }
    return {};
}

const Asset* findChecksumsAsset(const Release& release)
{
    for (const Asset& a : release.assets) {
        const std::string n = lower(a.name);
        if (n == "sha256sums.txt" || n == "sha256sums")
            return &a;
    }
    return nullptr;
}

std::map<std::string, std::string> parseChecksums(std::string_view text)
{
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        if (line.size() < 66 || line.front() == '#')
            continue;
        bool hex = true;
        for (size_t i = 0; i < 64; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(line[i])))
                hex = false;
        if (!hex || !(line[64] == ' ' || line[64] == '\t'))
            continue;
        std::string_view name = line.substr(65);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '*'))
            name.remove_prefix(1);
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string_view::npos)
            name = name.substr(slash + 1);
        if (name.empty())
            continue;
        out[lower(name)] = lower(line.substr(0, 64));
    }
    return out;
}

bool isTrustedDownloadUrl(std::string_view url)
{
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size() || lower(url.substr(0, scheme.size())) != scheme)
        return false;
    std::string_view rest = url.substr(scheme.size());
    const size_t end = rest.find_first_of("/?#");
    std::string host = lower(rest.substr(0, end));
    if (host.find('@') != std::string::npos)
        return false;
    if (const size_t colon = host.find(':'); colon != std::string::npos) {
        if (host.substr(colon) != ":443")
            return false;
        host = host.substr(0, colon);
    }
    static const char* const kHosts[] = {"github.com", "api.github.com", "objects.githubusercontent.com",
                                         "github-releases.githubusercontent.com", "release-assets.githubusercontent.com"};
    for (const char* h : kHosts)
        if (host == h)
            return true;
    return false;
}

std::string safeFileName(std::string_view name)
{
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos)
        name = name.substr(slash + 1);
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*')
            continue;
        out.push_back(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    if (out == "." || out == "..")
        return {};
    return out;
}

}
