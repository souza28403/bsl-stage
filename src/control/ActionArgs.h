#pragma once

#include <cstdint>
#include <string>

namespace bsl::control {

struct Target
{
    enum class Kind : std::uint8_t { None, Cell, Column, Layer, Master, Effect };

    Kind kind = Kind::None;
    int deck = -1;
    int layer = -1;
    int column = -1;
    int scope = 1;
    int effect = -1;

    bool valid() const { return kind != Kind::None; }
    bool operator==(const Target&) const = default;

    static Target cell(int deck, int layer, int column)
    {
        Target t;
        t.kind = Kind::Cell;
        t.deck = deck;
        t.layer = layer;
        t.column = column;
        return t;
    }
    static Target columnOf(int deck, int column)
    {
        Target t;
        t.kind = Kind::Column;
        t.deck = deck;
        t.column = column;
        return t;
    }
    static Target layerOf(int layer)
    {
        Target t;
        t.kind = Kind::Layer;
        t.layer = layer;
        return t;
    }
    static Target master()
    {
        Target t;
        t.kind = Kind::Master;
        return t;
    }
    static Target effectOf(int scope, int deck, int layer, int column, int effect)
    {
        Target t;
        t.kind = Kind::Effect;
        t.scope = scope;
        t.deck = deck;
        t.layer = layer;
        t.column = column;
        t.effect = effect;
        return t;
    }

    std::string key() const
    {
        return std::to_string(static_cast<int>(kind)) + ":" + std::to_string(deck) + ":" + std::to_string(layer) + ":"
               + std::to_string(column) + ":" + std::to_string(scope) + ":" + std::to_string(effect);
    }
};

struct ActionArgs
{
    bool selected = false;
    int deck = -1;
    int layer = 0;
    int layerId = 0;
    int column = 0;
    int index = 0;
    float value = 1.0f;
    int fxScope = 1;
    int effect = 0;
    std::string param;

    bool operator==(const ActionArgs&) const = default;
};

}
