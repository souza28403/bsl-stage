#include "ui/LayoutPreset.h"

#include "ui/ViewState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace bsl::ui::layout {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr size_t kMaxDockBytes = 256 * 1024;
constexpr size_t kMaxFileBytes = 512 * 1024;
constexpr size_t kMaxNameBytes = 64;

fs::path pathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool setError(std::string* error, const std::string& text)
{
    if (error)
        *error = text;
    return false;
}

struct Section
{
    std::string header;
    std::string type;
    std::string name;
    std::vector<std::string> lines;
};

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::vector<Section> parseIni(const std::string& text)
{
    std::vector<Section> sections;
    for (const std::string& line : splitLines(text)) {
        if (line.size() >= 4 && line.front() == '[' && line.back() == ']') {
            const size_t mid = line.find("][");
            if (mid != std::string::npos) {
                Section s;
                s.header = line;
                s.type = line.substr(1, mid - 1);
                s.name = line.substr(mid + 2, line.size() - mid - 3);
                sections.push_back(std::move(s));
                continue;
            }
        }
        if (line.front() == ';' || sections.empty())
            continue;
        sections.back().lines.push_back(line);
    }
    return sections;
}

std::string writeIni(const std::vector<Section>& sections)
{
    std::string out;
    for (const Section& s : sections) {
        out += s.header;
        out += '\n';
        for (const std::string& l : s.lines) {
            out += l;
            out += '\n';
        }
        out += '\n';
    }
    return out;
}

bool isPanelWindow(const std::string& name)
{
    if (name.find("cmp_") != std::string::npos)
        return true;
    return name == "Lista" || name == "Entrada" || name == "Saída" || name == "Propriedades";
}

bool isWindowSection(const Section& s)
{
    return s.type == "Window" && isPanelWindow(s.name);
}

bool isDockSection(const Section& s)
{
    return s.type == "Docking" && s.name == "Data";
}

bool isKnownRoot(unsigned id)
{
    return id == kCompDockRoot || id == kMapDockRoot;
}

size_t findKey(const std::string& line, const char* key, size_t from = 0)
{
    const std::string k(key);
    size_t pos = from;
    while ((pos = line.find(k, pos)) != std::string::npos) {
        if (pos == 0 || line[pos - 1] == ' ')
            return pos;
        pos += k.size();
    }
    return std::string::npos;
}

std::optional<unsigned> hexAfter(const std::string& line, const char* key)
{
    const size_t pos = findKey(line, key);
    if (pos == std::string::npos)
        return std::nullopt;
    const size_t start = pos + std::char_traits<char>::length(key);
    unsigned v = 0;
    int digits = 0;
    for (size_t i = start; i < line.size() && digits < 8; ++i, ++digits) {
        const char c = line[i];
        unsigned d = 0;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = static_cast<unsigned>(c - 'A' + 10);
        else
            break;
        v = (v << 4) | d;
    }
    if (digits == 0)
        return std::nullopt;
    return v;
}

std::string hex8(unsigned v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

bool remapHex(std::string& line, const char* key, const std::map<unsigned, unsigned>& remap)
{
    const size_t pos = findKey(line, key);
    if (pos == std::string::npos)
        return true;
    const std::optional<unsigned> old = hexAfter(line, key);
    if (!old)
        return false;
    const auto it = remap.find(*old);
    if (it == remap.end())
        return false;
    const size_t start = pos + std::char_traits<char>::length(key);
    size_t digits = 0;
    while (start + digits < line.size() && digits < 8 && std::isxdigit(static_cast<unsigned char>(line[start + digits])))
        ++digits;
    line.replace(start, digits, hex8(it->second));
    return true;
}

void scalePair(std::string& line, const char* key, double sx, double sy)
{
    const size_t pos = findKey(line, key);
    if (pos == std::string::npos)
        return;
    const size_t start = pos + std::char_traits<char>::length(key);
    const char* text = line.c_str() + start;
    char* end = nullptr;
    const long a = std::strtol(text, &end, 10);
    if (end == text || *end != ',')
        return;
    const char* second = end + 1;
    const long b = std::strtol(second, &end, 10);
    if (end == second)
        return;
    const long na = std::clamp(std::lround(static_cast<double>(a) * sx), -30000L, 30000L);
    const long nb = std::clamp(std::lround(static_cast<double>(b) * sy), -30000L, 30000L);
    line.replace(start, static_cast<size_t>(end - text), std::to_string(na) + "," + std::to_string(nb));
}

struct DockBlock
{
    unsigned root = 0;
    std::vector<std::string> lines;
};

bool startsWithWord(const std::string& s, const char* word)
{
    return s.compare(0, std::char_traits<char>::length(word), word) == 0;
}

bool blockValid(const DockBlock& b)
{
    if (b.lines.empty() || !startsWithWord(b.lines.front(), "DockSpace") || !isKnownRoot(b.root))
        return false;
    if (findKey(b.lines.front(), "Pos=") == std::string::npos || findKey(b.lines.front(), "Size=") == std::string::npos)
        return false;
    std::set<unsigned> defined;
    for (size_t i = 0; i < b.lines.size(); ++i) {
        std::string t = b.lines[i];
        t.erase(0, t.find_first_not_of(' '));
        if (!startsWithWord(t, "DockNode") && !startsWithWord(t, "DockSpace"))
            return false;
        const std::optional<unsigned> id = hexAfter(t, "ID=0x");
        if (!id || defined.count(*id))
            return false;
        if (i > 0) {
            const std::optional<unsigned> parent = hexAfter(t, "Parent=0x");
            if (!parent || !defined.count(*parent))
                return false;
            if (findKey(t, "SizeRef=") == std::string::npos)
                return false;
        }
        defined.insert(*id);
    }
    return true;
}

std::vector<DockBlock> dockBlocks(const std::vector<Section>& sections)
{
    std::vector<DockBlock> blocks;
    for (const Section& s : sections) {
        if (!isDockSection(s))
            continue;
        for (const std::string& line : s.lines) {
            const bool top = !line.empty() && line.front() != ' ';
            if (top) {
                DockBlock b;
                b.root = hexAfter(line, "ID=0x").value_or(0);
                b.lines.push_back(line);
                blocks.push_back(std::move(b));
            } else if (!blocks.empty()) {
                blocks.back().lines.push_back(line);
            }
        }
    }
    return blocks;
}

std::set<unsigned> definedIds(const std::vector<DockBlock>& blocks)
{
    std::set<unsigned> ids;
    for (const DockBlock& b : blocks)
        for (const std::string& l : b.lines)
            if (const std::optional<unsigned> id = hexAfter(l, "ID=0x"))
                ids.insert(*id);
    return ids;
}

void dropKeys(Section& s, const char* prefix)
{
    std::erase_if(s.lines, [&](const std::string& l) { return startsWithWord(l, prefix); });
}

}

std::string sanitizeName(const std::string& name)
{
    std::string s;
    for (const char c : name)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
            s.push_back(c);
    const size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    s = s.substr(first, s.find_last_not_of(" \t") - first + 1);
    if (s.size() > kMaxNameBytes) {
        size_t cut = kMaxNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
            --cut;
        s.resize(cut);
        const size_t last = s.find_last_not_of(' ');
        s = last == std::string::npos ? std::string() : s.substr(0, last + 1);
    }
    return s;
}

std::string toJson(const LayoutPreset& p)
{
    json j;
    j["format"] = "bsl-layout";
    j["version"] = kFormatVersion;
    j["name"] = p.name;
    j["created"] = p.created;
    j["refSize"] = {p.refW, p.refH};
    j["builtin"] = p.builtin;
    j["preview"] = {{"source", p.preview.source}, {"checker", p.preview.checker}, {"fit", p.preview.fit}};
    j["hidden"] = p.hidden;
    if (p.hasUiScale)
        j["uiScale"] = {{"mode", p.uiMode}, {"value", p.uiScale}};
    j["dock"] = p.dock;
    return j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool fromJson(const std::string& text, LayoutPreset& out, std::string* error)
{
    if (text.size() > kMaxFileBytes)
        return setError(error, "arquivo grande demais");
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return setError(error, "não é um JSON válido");

    try {
        LayoutPreset p;
        if (j.contains("format")) {
            if (!j["format"].is_string() || j["format"].get<std::string>() != "bsl-layout")
                return setError(error, "não é um layout do BSL Stage");
        }
        if (j.contains("version")) {
            if (!j["version"].is_number_integer())
                return setError(error, "versão inválida");
            if (j["version"].get<int>() > kFormatVersion)
                return setError(error, "layout de uma versão mais nova do BSL Stage");
        }
        if (!j.contains("name") || !j["name"].is_string())
            return setError(error, "sem nome");
        p.name = sanitizeName(j["name"].get<std::string>());
        if (p.name.empty())
            return setError(error, "nome vazio");

        if (j.contains("created") && j["created"].is_number_integer())
            p.created = std::max<std::int64_t>(0, j["created"].get<std::int64_t>());
        if (j.contains("refSize") && j["refSize"].is_array() && j["refSize"].size() == 2 && j["refSize"][0].is_number()
            && j["refSize"][1].is_number()) {
            const double w = j["refSize"][0].get<double>(), h = j["refSize"][1].get<double>();
            if (std::isfinite(w) && std::isfinite(h) && w >= 0.0 && h >= 0.0 && w <= 65536.0 && h <= 65536.0) {
                p.refW = static_cast<int>(w);
                p.refH = static_cast<int>(h);
            }
        }
        if (j.contains("builtin") && j["builtin"].is_string())
            p.builtin = j["builtin"].get<std::string>();
        if (j.contains("preview") && j["preview"].is_object()) {
            const json& v = j["preview"];
            if (v.contains("source") && v["source"].is_string()) {
                const std::string s = v["source"].get<std::string>();
                p.preview.source = s == "layer" ? "layer" : "clip";
            }
            if (v.contains("checker") && v["checker"].is_boolean())
                p.preview.checker = v["checker"].get<bool>();
            if (v.contains("fit") && v["fit"].is_string())
                p.preview.fit = v["fit"].get<std::string>() == "fill" ? "fill" : "fit";
        }
        if (j.contains("hidden") && j["hidden"].is_array()) {
            std::vector<std::string> keys;
            for (const json& item : j["hidden"])
                if (item.is_string())
                    keys.push_back(item.get<std::string>());
            p.hidden = view::sanitizeKeys(keys);
        }
        if (j.contains("uiScale") && j["uiScale"].is_object()) {
            const json& u = j["uiScale"];
            p.hasUiScale = true;
            const std::string mode = u.contains("mode") && u["mode"].is_string() ? u["mode"].get<std::string>() : "window";
            p.uiMode = mode == "system" ? "system" : mode == "fixed" ? "fixed" : "window";
            const double v = u.contains("value") && u["value"].is_number() ? u["value"].get<double>() : 1.0;
            p.uiScale = std::isfinite(v) ? static_cast<float>(std::clamp(v, 0.75, 3.0)) : 1.0f;
        }
        if (j.contains("dock")) {
            if (!j["dock"].is_string())
                return setError(error, "dados de dock inválidos");
            p.dock = j["dock"].get<std::string>();
            if (p.dock.size() > kMaxDockBytes)
                return setError(error, "dados de dock grandes demais");
        }
        out = std::move(p);
        return true;
    } catch (const json::exception& e) {
        return setError(error, std::string("campo inválido: ") + e.what());
    }
}

bool readFile(const fs::path& file, LayoutPreset& out, std::string* error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return setError(error, "não foi possível abrir o arquivo");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromJson(text, out, error);
}

bool writeFile(const fs::path& file, const LayoutPreset& preset, std::string* error)
{
    std::error_code ec;
    if (file.has_parent_path())
        fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return setError(error, "não foi possível gravar o arquivo");
        out << toJson(preset);
        if (!out)
            return setError(error, "falha ao gravar o arquivo");
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return setError(error, "não foi possível gravar o arquivo");
    }
    return true;
}

std::string extractDock(const std::string& fullIni)
{
    std::vector<Section> in = parseIni(fullIni);
    std::vector<Section> out;
    for (Section& s : in) {
        if (isWindowSection(s)) {
            dropKeys(s, "LastUsed=");
            out.push_back(std::move(s));
        }
    }
    Section dock;
    dock.header = "[Docking][Data]";
    dock.type = "Docking";
    dock.name = "Data";
    bool any = false;
    for (const DockBlock& b : dockBlocks(in)) {
        if (!isKnownRoot(b.root))
            continue;
        any = true;
        dock.lines.insert(dock.lines.end(), b.lines.begin(), b.lines.end());
    }
    if (any)
        out.push_back(std::move(dock));
    return writeIni(out);
}

bool hasDockRoot(const std::string& dockIni, unsigned rootId)
{
    const std::vector<Section> secs = parseIni(dockIni);
    for (const DockBlock& b : dockBlocks(secs))
        if (b.root == rootId && blockValid(b))
            return true;
    return false;
}

std::string applyDock(const std::string& currentIni, const std::string& presetDock, double sx, double sy)
{
    if (!std::isfinite(sx) || sx <= 0.0)
        sx = 1.0;
    if (!std::isfinite(sy) || sy <= 0.0)
        sy = 1.0;
    sx = std::clamp(sx, 0.05, 20.0);
    sy = std::clamp(sy, 0.05, 20.0);

    const std::vector<Section> cur = parseIni(currentIni);
    const std::vector<Section> pre = parseIni(presetDock);

    std::vector<DockBlock> preBlocks;
    std::set<unsigned> replaced;
    for (DockBlock& b : dockBlocks(pre))
        if (blockValid(b) && !replaced.count(b.root)) {
            replaced.insert(b.root);
            preBlocks.push_back(std::move(b));
        }

    std::vector<DockBlock> kept;
    for (DockBlock& b : dockBlocks(cur))
        if (!replaced.count(b.root))
            kept.push_back(std::move(b));

    std::set<unsigned> used = definedIds(kept);
    used.insert(replaced.begin(), replaced.end());
    used.insert(kCompDockRoot);
    used.insert(kMapDockRoot);
    std::map<unsigned, unsigned> remap;
    for (const unsigned r : replaced)
        remap[r] = r;
    unsigned next = 1;
    for (const DockBlock& b : preBlocks)
        for (const std::string& l : b.lines) {
            const unsigned id = hexAfter(l, "ID=0x").value_or(0);
            if (remap.count(id))
                continue;
            while (used.count(next))
                ++next;
            remap[id] = next;
            used.insert(next);
        }

    std::vector<DockBlock> placed;
    for (DockBlock b : preBlocks) {
        for (std::string& l : b.lines) {
            remapHex(l, "ID=0x", remap);
            remapHex(l, "Parent=0x", remap);
            scalePair(l, "Size=", sx, sy);
            scalePair(l, "SizeRef=", sx, sy);
        }
        placed.push_back(std::move(b));
    }

    std::vector<DockBlock> finalBlocks = kept;
    finalBlocks.insert(finalBlocks.end(), placed.begin(), placed.end());
    const std::set<unsigned> defined = definedIds(finalBlocks);

    std::vector<Section> presetWindows;
    std::set<std::string> presetNames;
    for (const Section& s : pre) {
        if (!isWindowSection(s) || presetNames.count(s.name))
            continue;
        Section w;
        w.header = s.header;
        w.type = s.type;
        w.name = s.name;
        presetNames.insert(w.name);
        for (const std::string& orig : s.lines) {
            if (startsWithWord(orig, "LastUsed="))
                continue;
            if (startsWithWord(orig, "DockId=0x")) {
                const std::optional<unsigned> old = hexAfter(orig, "DockId=0x");
                const auto it = old ? remap.find(*old) : remap.end();
                if (it == remap.end() || !defined.count(it->second))
                    continue;
                const size_t comma = orig.find(',');
                w.lines.push_back("DockId=0x" + hex8(it->second) + (comma == std::string::npos ? "" : orig.substr(comma)));
                continue;
            }
            std::string l = orig;
            scalePair(l, "Pos=", sx, sy);
            scalePair(l, "Size=", sx, sy);
            w.lines.push_back(std::move(l));
        }
        presetWindows.push_back(std::move(w));
    }

    std::vector<Section> out;
    for (const Section& s : cur) {
        if (isDockSection(s))
            continue;
        if (s.type == "Window" && presetNames.count(s.name))
            continue;
        Section copy = s;
        if (s.type == "Window") {
            std::erase_if(copy.lines, [&](const std::string& l) {
                if (!startsWithWord(l, "DockId=0x"))
                    return false;
                const std::optional<unsigned> id = hexAfter(l, "DockId=0x");
                return !id || !defined.count(*id);
            });
        }
        out.push_back(std::move(copy));
    }
    out.insert(out.end(), presetWindows.begin(), presetWindows.end());

    Section dock;
    dock.header = "[Docking][Data]";
    dock.type = "Docking";
    dock.name = "Data";
    for (const DockBlock& b : finalBlocks)
        dock.lines.insert(dock.lines.end(), b.lines.begin(), b.lines.end());
    if (!dock.lines.empty())
        out.push_back(std::move(dock));
    return writeIni(out);
}

PresetStore::PresetStore(fs::path dir) : dir_(std::move(dir)) {}

void PresetStore::sortEntries()
{
    std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.preset.created != b.preset.created)
            return a.preset.created < b.preset.created;
        return lower(a.preset.name) < lower(b.preset.name);
    });
}

void PresetStore::reload()
{
    entries_.clear();
    skipped_ = 0;
    std::error_code ec;
    if (!fs::is_directory(dir_, ec))
        return;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        std::error_code ec2;
        if (e.is_regular_file(ec2) && lower(e.path().extension().string()) == ".json")
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        Entry e;
        if (!readFile(f, e.preset, nullptr) || find(e.preset.name) >= 0) {
            ++skipped_;
            continue;
        }
        e.file = f;
        entries_.push_back(std::move(e));
    }
    sortEntries();
}

int PresetStore::find(const std::string& name) const
{
    const std::string key = lower(sanitizeName(name));
    for (int i = 0; i < count(); ++i)
        if (lower(entries_[static_cast<size_t>(i)].preset.name) == key)
            return i;
    return -1;
}

fs::path PresetStore::uniqueFile(const std::string& name) const
{
    std::string stem;
    for (const char c : name) {
        const bool bad = static_cast<unsigned char>(c) < 0x20 || std::string("<>:\"/\\|?*").find(c) != std::string::npos;
        stem.push_back(bad ? '_' : c);
    }
    while (!stem.empty() && (stem.back() == '.' || stem.back() == ' '))
        stem.pop_back();
    if (stem.empty())
        stem = "layout";
    auto taken = [&](const fs::path& p) {
        std::error_code ec;
        if (fs::exists(p, ec))
            return true;
        for (const Entry& e : entries_)
            if (lower(e.file.filename().string()) == lower(p.filename().string()))
                return true;
        return false;
    };
    fs::path candidate = dir_ / pathFromUtf8(stem + ".json");
    for (int n = 2; taken(candidate); ++n)
        candidate = dir_ / pathFromUtf8(stem + " (" + std::to_string(n) + ").json");
    return candidate;
}

std::int64_t PresetStore::nextCreated() const
{
    std::int64_t latest = 0;
    for (const Entry& e : entries_)
        latest = std::max(latest, e.preset.created);
    return std::max<std::int64_t>(static_cast<std::int64_t>(std::time(nullptr)), latest + 1);
}

bool PresetStore::save(LayoutPreset preset, std::string* error)
{
    preset.name = sanitizeName(preset.name);
    if (preset.name.empty())
        return setError(error, "o nome não pode ficar vazio");
    const int existing = find(preset.name);
    if (existing >= 0) {
        Entry& e = entries_[static_cast<size_t>(existing)];
        preset.name = e.preset.name;
        preset.created = e.preset.created;
        if (!writeFile(e.file, preset, error))
            return false;
        e.preset = std::move(preset);
        return true;
    }
    Entry e;
    preset.created = nextCreated();
    e.file = uniqueFile(preset.name);
    if (!writeFile(e.file, preset, error))
        return false;
    e.preset = std::move(preset);
    entries_.push_back(std::move(e));
    sortEntries();
    return true;
}

bool PresetStore::rename(int index, const std::string& newName, std::string* error)
{
    if (index < 0 || index >= count())
        return setError(error, "layout inexistente");
    const std::string name = sanitizeName(newName);
    if (name.empty())
        return setError(error, "o nome não pode ficar vazio");
    const int other = find(name);
    if (other >= 0 && other != index)
        return setError(error, "já existe um layout com esse nome");
    Entry& e = entries_[static_cast<size_t>(index)];
    LayoutPreset p = e.preset;
    p.name = name;
    if (!writeFile(e.file, p, error))
        return false;
    e.preset = std::move(p);
    return true;
}

bool PresetStore::remove(int index)
{
    if (index < 0 || index >= count())
        return false;
    std::error_code ec;
    fs::remove(entries_[static_cast<size_t>(index)].file, ec);
    if (ec)
        return false;
    entries_.erase(entries_.begin() + index);
    return true;
}

bool PresetStore::exportTo(int index, const fs::path& file, std::string* error) const
{
    if (index < 0 || index >= count())
        return setError(error, "layout inexistente");
    return writeFile(file, entries_[static_cast<size_t>(index)].preset, error);
}

bool PresetStore::importFrom(const fs::path& file, std::string* error, std::string* importedName)
{
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) > kMaxFileBytes)
        return setError(error, "arquivo não encontrado ou grande demais");
    LayoutPreset p;
    if (!readFile(file, p, error))
        return false;
    const std::string base = p.name;
    for (int n = 2; find(p.name) >= 0 && n < 1000; ++n)
        p.name = sanitizeName(base.substr(0, kMaxNameBytes - 8) + " (" + std::to_string(n) + ")");
    if (find(p.name) >= 0)
        return setError(error, "nome repetido");
    if (importedName)
        *importedName = p.name;
    return save(std::move(p), error);
}

}
