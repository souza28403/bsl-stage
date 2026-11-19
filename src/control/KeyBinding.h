#pragma once

#include "control/ActionArgs.h"
#include "control/Keys.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace bsl::control {

enum class Scope { App, Project };
enum class Mode { Press, Hold, Repeat };

struct KeyBinding
{
    std::string action;
    ActionArgs args;
    Chord chord;
    Mode mode = Mode::Press;
    int rate = 4;
    bool global = false;

    bool operator==(const KeyBinding&) const = default;
};

struct KeyBindingSet
{
    std::vector<KeyBinding> items;

    bool operator==(const KeyBindingSet&) const = default;
    int find(const std::string& action, const ActionArgs& args) const;
};

KeyBindingSet defaultAppKeys();
KeyBindingSet defaultProjectKeys();

struct Match
{
    Scope scope = Scope::Project;
    int index = 0;
    const KeyBinding* binding = nullptr;
};

std::optional<Match> resolveKey(const Chord& chord, const KeyBindingSet& app, const KeyBindingSet& project);

std::vector<Match> findConflicts(const Chord& chord, const KeyBindingSet& app, const KeyBindingSet& project,
                                 int ignoreScope = -1, int ignoreIndex = -1);

Scope defaultScopeFor(const std::string& action);

bool supportsHold(const std::string& action);
bool supportsRepeat(const std::string& action);
bool supportsMode(const std::string& action, Mode mode);
int snapRate(int rate);

nlohmann::json bindingsToJson(const KeyBindingSet& set);
KeyBindingSet bindingsFromJson(const nlohmann::json& j, std::vector<std::string>* warnings = nullptr);

std::string keysFileText(const KeyBindingSet& app, const KeyBindingSet& project);
bool parseKeysFile(const std::string& text, KeyBindingSet& app, KeyBindingSet& project, std::string* error,
                   std::vector<std::string>* warnings = nullptr);
bool saveKeysFile(const std::filesystem::path& file, const KeyBindingSet& app, const KeyBindingSet& project,
                  std::string* error);
bool loadKeysFile(const std::filesystem::path& file, KeyBindingSet& app, KeyBindingSet& project, std::string* error,
                  std::vector<std::string>* warnings = nullptr);

inline constexpr const char* kKeysFormat = "bslkeys";
inline constexpr const char* kKeysExtension = ".bslkeys";
inline constexpr int kKeysVersion = 1;

}
