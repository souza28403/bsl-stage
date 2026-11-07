#pragma once

#include "control/KeyService.h"
#include "dmx/DmxMap.h"
#include "dmx/DmxNet.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace bsl {
struct Composition;
namespace fx {
struct EffectInstance;
}
}

namespace bsl::dmx {

struct LearnTarget
{
    Map prototype;
    std::string title;
};

struct EngineInfo
{
    std::uint64_t applied = 0;
    std::uint64_t triggers = 0;
    std::string lastEvent;
    int blocksLive = 0;
    int mapsLive = 0;
};

class Engine
{
public:
    int apply(Composition& comp, control::KeyService& keys, const control::SelectionRef& selection, const Snapshot& snap);

    void learnBegin(LearnTarget target, const Snapshot& snap);
    void learnCancel();
    bool learning() const { return learning_; }
    const LearnTarget& learnTarget() const { return learnTarget_; }
    int takeLearned(std::string* message = nullptr);

    void reset();
    const EngineInfo& info() const { return info_; }

private:
    struct BlockState
    {
        int mode = -1;
        int fxCount = -1;
        int universe = -1;
        int channel = -1;
        std::vector<ChannelRow> rows;
        std::array<int, 40> last{};
        std::array<bool, 40> seen{};
        int speedClipDeck = -2, speedClipColumn = -2;
        double speed = 1.0;
        bool speedSet = false;
        bool live = false;
    };
    struct MapState
    {
        int universe = -1;
        int channel = -1;
        int span = 0;
        int last = -1;
        bool high = false;
        bool live = false;
    };

    int applyBlock(Composition& comp, control::KeyService& keys, control::ActionContext& ctx, const Block& b, const Snapshot& snap);
    int applyMap(Composition& comp, control::KeyService& keys, control::ActionContext& ctx, const Map& m, const Snapshot& snap);
    bool learnStep(Composition& comp, const Snapshot& snap);

    std::unordered_map<int, BlockState> blocks_;
    std::unordered_map<int, MapState> maps_;
    bool learning_ = false;
    LearnTarget learnTarget_;
    std::unordered_map<int, DmxData> learnBase_;
    int learnedUid_ = 0;
    std::string learnedMessage_;
    EngineInfo info_;
};

struct ParamRef
{
    ParamKind kind = ParamKind::LayerOpacity;
    int layerId = 0;
    int fxScope = 1;
    int deck = -1;
    int column = 0;
    int effect = 0;
    std::string paramId;
};
bool applyParam(Composition& comp, const ParamRef& ref, int raw, int max, std::string* what = nullptr);
bool setEffectParamNormalized(fx::EffectInstance& inst, const std::string& paramId, double n);

std::string layerLabel(const Composition& comp, int layerId);
std::string describeMap(const Composition& comp, const Map& m);
std::string describeBlock(const Composition& comp, const Block& b);
std::string universeChannelLabel(int universe, int channel);

std::vector<std::string> channelOwnersFor(const Composition& comp, int universe);

}
