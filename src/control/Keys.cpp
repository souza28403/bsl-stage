#include "control/Keys.h"

#include <string>
#include <vector>

namespace bsl::control {

namespace {

struct KeyInfo
{
    std::string id;
    std::string label;
    unsigned vk = 0;
};

const std::vector<KeyInfo>& table()
{
    static const std::vector<KeyInfo> t = [] {
        std::vector<KeyInfo> v;
        v.push_back({"", "", 0});
        for (int i = 0; i < 26; ++i) {
            const std::string name(1, static_cast<char>('A' + i));
            v.push_back({name, name, static_cast<unsigned>('A' + i)});
        }
        for (int i = 0; i < 10; ++i) {
            const std::string name(1, static_cast<char>('0' + i));
            v.push_back({name, name, static_cast<unsigned>('0' + i)});
        }
        for (int i = 1; i <= 12; ++i) {
            const std::string name = "F" + std::to_string(i);
            v.push_back({name, name, static_cast<unsigned>(0x6F + i)});
        }
        v.push_back({"Space", "Espaço", 0x20});
        v.push_back({"Enter", "Enter", 0x0D});
        v.push_back({"Tab", "Tab", 0x09});
        v.push_back({"Escape", "Esc", 0x1B});
        v.push_back({"Backspace", "Backspace", 0x08});
        v.push_back({"Delete", "Delete", 0x2E});
        v.push_back({"Insert", "Insert", 0x2D});
        v.push_back({"Home", "Home", 0x24});
        v.push_back({"End", "End", 0x23});
        v.push_back({"PageUp", "Page Up", 0x21});
        v.push_back({"PageDown", "Page Down", 0x22});
        v.push_back({"Left", "Esquerda", 0x25});
        v.push_back({"Right", "Direita", 0x27});
        v.push_back({"Up", "Cima", 0x26});
        v.push_back({"Down", "Baixo", 0x28});
        for (int i = 0; i < 10; ++i)
            v.push_back({"Num" + std::to_string(i), "Num " + std::to_string(i), static_cast<unsigned>(0x60 + i)});
        v.push_back({"NumAdd", "Num +", 0x6B});
        v.push_back({"NumSub", "Num -", 0x6D});
        v.push_back({"NumMul", "Num *", 0x6A});
        v.push_back({"NumDiv", "Num /", 0x6F});
        v.push_back({"NumDecimal", "Num .", 0x6E});
        v.push_back({"NumEnter", "Num Enter", 0});
        v.push_back({"-", "-", 0xBD});
        v.push_back({"=", "=", 0xBB});
        v.push_back({"[", "[", 0xDB});
        v.push_back({"]", "]", 0xDD});
        v.push_back({";", ";", 0xBA});
        v.push_back({"'", "'", 0xDE});
        v.push_back({",", ",", 0xBC});
        v.push_back({".", ".", 0xBE});
        v.push_back({"/", "/", 0xBF});
        v.push_back({"\\", "\\", 0xDC});
        v.push_back({"`", "`", 0xC0});
        return v;
    }();
    return t;
}

const KeyInfo& info(Key key)
{
    const size_t i = static_cast<size_t>(key);
    return i < table().size() ? table()[i] : table()[0];
}

std::string joinMods(std::uint8_t mods, const std::string& key)
{
    std::string out;
    if (mods & ModCtrl)
        out += "Ctrl+";
    if (mods & ModAlt)
        out += "Alt+";
    if (mods & ModShift)
        out += "Shift+";
    return out + key;
}

}

const char* keyId(Key key)
{
    return info(key).id.c_str();
}

const char* keyLabel(Key key)
{
    return info(key).label.c_str();
}

Key keyFromId(const std::string& id)
{
    if (id.empty())
        return Key::None;
    const std::vector<KeyInfo>& t = table();
    for (size_t i = 1; i < t.size(); ++i)
        if (t[i].id == id)
            return static_cast<Key>(i);
    return Key::None;
}

unsigned keyVirtualCode(Key key)
{
    return info(key).vk;
}

bool keyIsReservedForCapture(Key key)
{
    return key == Key::Escape || key == Key::None;
}

std::string chordId(Chord chord)
{
    if (!chord.valid())
        return {};
    return joinMods(chord.mods, keyId(chord.key));
}

std::string chordLabel(Chord chord)
{
    if (!chord.valid())
        return {};
    return joinMods(chord.mods, keyLabel(chord.key));
}

bool chordFromId(const std::string& text, Chord& out)
{
    Chord c;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t plus = text.find('+', pos);
        if (plus == std::string::npos)
            break;
        const std::string token = text.substr(pos, plus - pos);
        if (token == "Ctrl")
            c.mods |= ModCtrl;
        else if (token == "Shift")
            c.mods |= ModShift;
        else if (token == "Alt")
            c.mods |= ModAlt;
        else
            return false;
        pos = plus + 1;
    }
    c.key = keyFromId(text.substr(pos));
    if (!c.valid())
        return false;
    out = c;
    return true;
}

}
