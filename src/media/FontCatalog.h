#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl::media {

struct FontEntry
{
    std::string file;
    std::string label;
    std::filesystem::path path;
    bool bundled = false;
};

class FontCatalog
{
public:
    static FontCatalog& instance();

    void ensureScanned();
    bool ready() const;
    std::vector<FontEntry> entries() const;

    std::filesystem::path resolve(const std::string& file) const;
    std::string defaultFile() const;
    std::string labelFor(const std::string& file) const;

    ~FontCatalog();

private:
    FontCatalog();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool readFontLabel(const std::filesystem::path& file, std::string& label);

}
