#include "control/KeyBinding.h"

#include "control/ActionRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace bsl::control {

namespace {

KeyBinding makeBinding(const char* action, Key key, std::uint8_t mods = ModNone, Mode mode = Mode::Press)
{
    KeyBinding b;
    b.action = action;
    b.chord = {key, mods};
    b.mode = mode;
    return b;
}

constexpr Key kColumnKeys[10] = {Key::D1, Key::D2, Key::D3, Key::D4, Key::D5, Key::D6, Key::D7, Key::D8, Key::D9, Key::D0};
constexpr Key kCellKeys[10] = {Key::Q, Key::W, Key::E, Key::R, Key::T, Key::Y, Key::U, Key::I, Key::O, Key::P};

bool isShow(const KeyBinding& b)
{
    const ActionDef* def = findAction(b.action);
    return def && def->show;
}

}

int KeyBindingSet::find(const std::string& action, const ActionArgs& args) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].action == action && items[i].args == args)
            return static_cast<int>(i);
    return -1;
}

KeyBindingSet defaultAppKeys()
{
    KeyBindingSet set;
    set.items.push_back(makeBinding("project.save", Key::S, ModCtrl));
    set.items.push_back(makeBinding("project.saveAs", Key::S, ModCtrl | ModShift));
    set.items.push_back(makeBinding("project.open", Key::O, ModCtrl));
    set.items.push_back(makeBinding("project.new", Key::N, ModCtrl));
    set.items.push_back(makeBinding("view.fullscreen", Key::F11));
    for (int i = 0; i < 9; ++i) {
        KeyBinding b = makeBinding("ui.layoutPreset", kColumnKeys[i], ModAlt);
        b.args.index = i;
        set.items.push_back(b);
    }
    return set;
}

KeyBindingSet defaultProjectKeys()
{
    KeyBindingSet set;
    for (int i = 0; i < 10; ++i) {
        KeyBinding b = makeBinding("column.trigger", kColumnKeys[i]);
        b.args.column = i;
        set.items.push_back(b);
    }
    for (int i = 0; i < 10; ++i) {
        KeyBinding b = makeBinding("clip.trigger", kCellKeys[i]);
        b.args.selected = true;
        b.args.column = i;
        set.items.push_back(b);
    }
    set.items.push_back(makeBinding("transport.toggle", Key::Space));
    KeyBinding clear = makeBinding("layer.clear", Key::Delete);
    clear.args.selected = true;
    set.items.push_back(clear);
    set.items.push_back(makeBinding("master.blackout", Key::B, ModNone, Mode::Hold));
    return set;
}

Scope defaultScopeFor(const std::string& action)
{
    const ActionDef* def = findAction(action);
    return (def && !def->show) ? Scope::App : Scope::Project;
}

bool supportsHold(const std::string& action)
{
    const ActionDef* def = findAction(action);
    return def && def->isToggle();
}

std::optional<Match> resolveKey(const Chord& chord, const KeyBindingSet& app, const KeyBindingSet& project)
{
    if (!chord.valid())
        return std::nullopt;
    std::optional<Match> best;
    int bestRank = 99;
    auto scan = [&](const KeyBindingSet& set, Scope scope) {
        for (size_t i = 0; i < set.items.size(); ++i) {
            const KeyBinding& b = set.items[i];
            if (!(b.chord == chord))
                continue;
            const bool show = isShow(b);
            const int rank = scope == Scope::Project ? (show ? 0 : 3) : (show ? 2 : 1);
            if (rank < bestRank) {
                bestRank = rank;
                best = Match{scope, static_cast<int>(i), &b};
            }
        }
    };
    scan(project, Scope::Project);
    scan(app, Scope::App);
    return best;
}

std::vector<Match> findConflicts(const Chord& chord, const KeyBindingSet& app, const KeyBindingSet& project,
                                 int ignoreScope, int ignoreIndex)
{
    std::vector<Match> out;
    if (!chord.valid())
        return out;
    auto scan = [&](const KeyBindingSet& set, Scope scope) {
        for (size_t i = 0; i < set.items.size(); ++i) {
            if (ignoreScope == static_cast<int>(scope) && ignoreIndex == static_cast<int>(i))
                continue;
            if (set.items[i].chord == chord)
                out.push_back(Match{scope, static_cast<int>(i), &set.items[i]});
        }
    };
    scan(project, Scope::Project);
    scan(app, Scope::App);
    return out;
}

nlohmann::json bindingsToJson(const KeyBindingSet& set)
{
    using nlohmann::json;
    json arr = json::array();
    const ActionArgs def;
    for (const KeyBinding& b : set.items) {
        json j = json::object();
        j["action"] = b.action;
        j["key"] = chordId(b.chord);
        j["mode"] = b.mode == Mode::Hold ? "hold" : "press";
        if (b.global)
            j["global"] = true;
        json a = json::object();
        if (b.args.selected != def.selected)
            a["selected"] = b.args.selected;
        if (b.args.deck != def.deck)
            a["deck"] = b.args.deck;
        if (b.args.layerId > 0)
            a["layerId"] = b.args.layerId;
        else if (b.args.layer != def.layer)
            a["layer"] = b.args.layer;
        if (b.args.column != def.column)
            a["column"] = b.args.column;
        if (b.args.index != def.index)
            a["index"] = b.args.index;
        if (b.args.value != def.value)
            a["value"] = b.args.value;
        if (b.args.fxScope != def.fxScope)
            a["fxScope"] = b.args.fxScope;
        if (b.args.effect != def.effect)
            a["effect"] = b.args.effect;
        if (!a.empty())
            j["args"] = std::move(a);
        arr.push_back(std::move(j));
    }
    return arr;
}

namespace {

int intField(const nlohmann::json& j, const char* key, int def, int lo, int hi)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return def;
    return std::clamp(j[key].get<int>(), lo, hi);
}

}

KeyBindingSet bindingsFromJson(const nlohmann::json& j, std::vector<std::string>* warnings)
{
    KeyBindingSet set;
    if (!j.is_array())
        return set;
    constexpr size_t kMax = 512;
    for (const nlohmann::json& e : j) {
        if (set.items.size() >= kMax)
            break;
        if (!e.is_object() || !e.contains("action") || !e["action"].is_string())
            continue;
        KeyBinding b;
        b.action = e["action"].get<std::string>();
        if (!findAction(b.action)) {
            if (warnings)
                warnings->push_back("Atalho ignorado: a ação \"" + b.action + "\" não existe nesta versão.");
            continue;
        }
        const std::string key = e.contains("key") && e["key"].is_string() ? e["key"].get<std::string>() : std::string();
        if (!key.empty() && !chordFromId(key, b.chord)) {
            if (warnings)
                warnings->push_back("Atalho ignorado: tecla \"" + key + "\" desconhecida (" + b.action + ").");
            continue;
        }
        const std::string mode = e.contains("mode") && e["mode"].is_string() ? e["mode"].get<std::string>() : "press";
        b.mode = (mode == "hold" && supportsHold(b.action)) ? Mode::Hold : Mode::Press;
        b.global = e.contains("global") && e["global"].is_boolean() && e["global"].get<bool>();
        if (e.contains("args") && e["args"].is_object()) {
            const nlohmann::json& a = e["args"];
            if (a.contains("selected") && a["selected"].is_boolean())
                b.args.selected = a["selected"].get<bool>();
            b.args.deck = intField(a, "deck", b.args.deck, -1, 4096);
            b.args.layer = intField(a, "layer", b.args.layer, 0, 4096);
            b.args.layerId = intField(a, "layerId", b.args.layerId, 0, 1 << 30);
            b.args.column = intField(a, "column", b.args.column, -1, 4096);
            b.args.index = intField(a, "index", b.args.index, 0, 4096);
            if (a.contains("value") && a["value"].is_number())
                b.args.value = std::clamp(a["value"].get<float>(), 0.0f, 1.0f);
            b.args.fxScope = intField(a, "fxScope", b.args.fxScope, 0, 2);
            b.args.effect = intField(a, "effect", b.args.effect, 0, 4096);
        }
        set.items.push_back(std::move(b));
    }
    return set;
}

std::string keysFileText(const KeyBindingSet& app, const KeyBindingSet& project)
{
    nlohmann::json root = nlohmann::json::object();
    root["format"] = kKeysFormat;
    root["version"] = kKeysVersion;
    root["app"] = bindingsToJson(app);
    root["project"] = bindingsToJson(project);
    return root.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
}

bool parseKeysFile(const std::string& textIn, KeyBindingSet& app, KeyBindingSet& project, std::string* error,
                   std::vector<std::string>* warnings)
{
    std::string_view text(textIn);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    const nlohmann::json root = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error)
            *error = "o arquivo não é um JSON válido";
        return false;
    }
    if (!root.contains("format") || !root["format"].is_string() || root["format"].get<std::string>() != kKeysFormat) {
        if (error)
            *error = "o arquivo não é um conjunto de atalhos do BSL Stage (.bslkeys)";
        return false;
    }
    if (root.contains("version") && root["version"].is_number_integer() && root["version"].get<int>() > kKeysVersion && warnings)
        warnings->push_back("Este arquivo foi salvo por uma versão mais nova do BSL Stage; algumas informações podem ser ignoradas.");
    app = bindingsFromJson(root.value("app", nlohmann::json::array()), warnings);
    project = bindingsFromJson(root.value("project", nlohmann::json::array()), warnings);
    return true;
}

bool saveKeysFile(const std::filesystem::path& file, const KeyBindingSet& app, const KeyBindingSet& project,
                  std::string* error)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error)
            *error = "não foi possível gravar o arquivo";
        return false;
    }
    out << keysFileText(app, project);
    out.flush();
    if (!out) {
        if (error)
            *error = "erro ao gravar o arquivo";
        return false;
    }
    return true;
}

bool loadKeysFile(const std::filesystem::path& file, KeyBindingSet& app, KeyBindingSet& project, std::string* error,
                  std::vector<std::string>* warnings)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error)
            *error = "não foi possível abrir o arquivo";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseKeysFile(text, app, project, error, warnings);
}

}
