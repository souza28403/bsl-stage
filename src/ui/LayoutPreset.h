#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::ui::layout {

inline constexpr unsigned kCompDockRoot = 0x42534C43;
inline constexpr unsigned kMapDockRoot = 0x42534C4D;

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFileExtension = ".bsllayout";

struct PreviewPrefs
{
    std::string source = "clip";
    bool checker = true;
    std::string fit = "fit";
    bool operator==(const PreviewPrefs&) const = default;
};

struct LayoutPreset
{
    std::string name;
    std::int64_t created = 0;
    int refW = 0, refH = 0;
    std::string dock;
    std::string builtin;
    PreviewPrefs preview;
    std::vector<std::string> hidden;
    bool hasUiScale = false;
    std::string uiMode = "window";
    float uiScale = 1.0f;
};

std::string toJson(const LayoutPreset& preset);
bool fromJson(const std::string& text, LayoutPreset& out, std::string* error = nullptr);
bool readFile(const std::filesystem::path& file, LayoutPreset& out, std::string* error = nullptr);
bool writeFile(const std::filesystem::path& file, const LayoutPreset& preset, std::string* error = nullptr);

std::string sanitizeName(const std::string& name);

std::string extractDock(const std::string& fullIni);
bool hasDockRoot(const std::string& dockIni, unsigned rootId);
std::string applyDock(const std::string& currentIni, const std::string& presetDock, double sx, double sy);

class PresetStore
{
public:
    struct Entry
    {
        LayoutPreset preset;
        std::filesystem::path file;
    };

    explicit PresetStore(std::filesystem::path dir);

    const std::filesystem::path& dir() const { return dir_; }
    void reload();
    int skipped() const { return skipped_; }
    const std::vector<Entry>& entries() const { return entries_; }
    int count() const { return static_cast<int>(entries_.size()); }
    int find(const std::string& name) const;

    bool save(LayoutPreset preset, std::string* error = nullptr);
    bool rename(int index, const std::string& newName, std::string* error = nullptr);
    bool remove(int index);
    bool exportTo(int index, const std::filesystem::path& file, std::string* error = nullptr) const;
    bool importFrom(const std::filesystem::path& file, std::string* error = nullptr, std::string* importedName = nullptr);

private:
    std::filesystem::path uniqueFile(const std::string& name) const;
    std::int64_t nextCreated() const;
    void sortEntries();

    std::filesystem::path dir_;
    std::vector<Entry> entries_;
    int skipped_ = 0;
};

}
