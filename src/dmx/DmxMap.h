#pragma once

#include "control/KeyBinding.h"
#include "dmx/DmxProtocol.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::dmx {

inline constexpr int kMaxBlocks = 256;
inline constexpr int kMaxMaps = 1024;
inline constexpr int kMaxFxSlots = 16;
inline constexpr int kClipRangeWidth = 10;
inline constexpr int kClipMaxColumns = 24;

enum class Func : std::uint8_t {
    Opacity,
    Clip,
    Speed,
    Blend,
    PosX,
    PosY,
    Scale,
    Rotation,
    Fx,
    MasterOpacity,
    Blackout,
    Column,
};

enum class ParamKind : std::uint8_t {
    LayerOpacity,
    LayerSpeed,
    LayerBlend,
    LayerPosX,
    LayerPosY,
    LayerScale,
    LayerRotation,
    MasterOpacity,
    EffectParam,
};
inline constexpr int kParamKindCount = 9;
const char* paramKindKey(ParamKind k);
bool paramKindFromKey(const std::string& key, ParamKind& out);
const char* paramKindLabel(ParamKind k);

enum class LayerMode : std::uint8_t { Basic, Standard, Full, Full16 };
inline constexpr int kLayerModeCount = 4;
const char* layerModeKey(LayerMode m);
const char* layerModeLabel(LayerMode m);
bool layerModeFromKey(const std::string& key, LayerMode& out);

enum class BlockKind : std::uint8_t { Layer, Master };

struct FxSlot
{
    int effect = 0;
    std::string param;
    bool operator==(const FxSlot&) const = default;
};

struct Block
{
    int uid = 0;
    BlockKind kind = BlockKind::Layer;
    int layerId = 0;
    int layerIndex = -1;
    int universe = 1;
    int channel = 1;
    LayerMode mode = LayerMode::Standard;
    bool enabled = true;
    std::vector<FxSlot> fx;

    int channelCount() const;
    int lastChannel() const { return channel + channelCount() - 1; }
    bool operator==(const Block&) const = default;
};

struct Map
{
    enum class Type : std::uint8_t { Param, Action };

    int uid = 0;
    Type type = Type::Param;
    int universe = 1;
    int channel = 1;
    bool bit16 = false;
    bool enabled = true;

    ParamKind param = ParamKind::LayerOpacity;
    int layerId = 0;
    int layerIndex = -1;
    int fxScope = 1;
    int deck = -1;
    int column = 0;
    int effect = 0;
    std::string paramId;
    bool invert = false;

    control::KeyBinding binding;
    int threshold = 128;

    int channelSpan() const { return bit16 ? 2 : 1; }
    bool operator==(const Map&) const = default;
};

bool sameTarget(const Map& a, const Map& b);

struct Config
{
    std::vector<Block> blocks;
    std::vector<Map> maps;
    int nextUid = 1;

    bool empty() const { return blocks.empty() && maps.empty(); }
    void assignUids();
    Block& addBlock(Block b);
    Map& addMap(Map m);
    std::vector<int> universesUsed() const;
    bool sameContent(const Config& o) const;
};

double dmxNormalized(int raw, int max);
double dmxCentered(int raw, int max);
double dmxScaleOf(int raw, int max);
inline double dmxSpeedOf(int raw, int max) { return dmxScaleOf(raw, max); }
double dmxRotationOf(int raw, int max);
int dmxBlendOf(int raw, int max);
int dmxClipRange(int raw8);
int dmxRawFromNormalized(double n, int max);
int dmxDefaultRaw(Func f, int max);
Func dmxFuncOf(ParamKind k);

struct ChannelRow
{
    Func func = Func::Opacity;
    int offset = 1;
    int span = 1;
    int fxIndex = -1;
    std::string name;
    std::string key;
    std::string range;
    int defaultRaw = 0;
    std::string note;
};
std::vector<ChannelRow> layerRows(LayerMode mode, int fxCount);
std::vector<ChannelRow> masterRows();
int layerChannelCount(LayerMode mode, int fxCount);
inline int masterChannelCount() { return 3; }
std::string personalityName(LayerMode mode, int fxCount);

struct ClipRange
{
    int from = 0, to = 0;
    std::string name;
    std::string key;
};
std::vector<ClipRange> clipRanges(int columns = kClipMaxColumns);

using LayerNamer = std::string (*)(int layerId, void* user);
std::vector<std::string> channelOwners(const Config& cfg, int universe, LayerNamer namer, void* user);

nlohmann::json configToJson(const Config& cfg);
Config configFromJson(const nlohmann::json& j, std::vector<std::string>* warnings = nullptr);
int stampLayerIds(Config& cfg, const std::vector<int>& idsByIndex);

}
