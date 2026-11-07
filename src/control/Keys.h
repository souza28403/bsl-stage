#pragma once

#include <cstdint>
#include <string>

namespace bsl::control {

enum class Key : std::uint8_t {
    None = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Space, Enter, Tab, Escape, Backspace, Delete, Insert, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    NumAdd, NumSub, NumMul, NumDiv, NumDecimal, NumEnter,
    Minus, Equal, LeftBracket, RightBracket, Semicolon, Apostrophe, Comma, Period, Slash, Backslash, Grave,
    Count
};

enum Mod : std::uint8_t { ModNone = 0, ModCtrl = 1, ModShift = 2, ModAlt = 4 };

struct Chord
{
    Key key = Key::None;
    std::uint8_t mods = ModNone;

    bool valid() const { return key != Key::None; }
    bool operator==(const Chord&) const = default;
};

const char* keyId(Key key);
const char* keyLabel(Key key);
Key keyFromId(const std::string& id);
unsigned keyVirtualCode(Key key);
bool keyIsReservedForCapture(Key key);

std::string chordId(Chord chord);
std::string chordLabel(Chord chord);
bool chordFromId(const std::string& text, Chord& out);

}
