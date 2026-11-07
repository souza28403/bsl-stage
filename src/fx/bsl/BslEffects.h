#pragma once

#include "fx/IEffect.h"

#include <memory>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

std::vector<media::ParamDesc> describeSliceRefiner();
std::unique_ptr<IEffect> makeSliceRefiner();

std::vector<media::ParamDesc> describeSliceRefiner3();
std::unique_ptr<IEffect> makeSliceRefiner3();

std::vector<media::ParamDesc> describeVhs();
std::unique_ptr<IEffect> makeVhs();

std::vector<media::ParamDesc> describeRelight();
std::unique_ptr<IEffect> makeRelight();

std::vector<media::ParamDesc> describeBodyGlitch();
std::unique_ptr<IEffect> makeBodyGlitch();

std::vector<media::ParamDesc> describeAura();
std::unique_ptr<IEffect> makeAura();

std::vector<media::ParamDesc> describeZSlice();
std::unique_ptr<IEffect> makeZSlice();

struct BslPluginInfo
{
    const char* id;
    const char* name;
    const char* category;
    const char* description;
    bool generator;
    bool ai = false;
};
const std::vector<BslPluginInfo>& bslPlugins();
const BslPluginInfo* findBslPlugin(const std::string& id);

}
