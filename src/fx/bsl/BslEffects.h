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

std::vector<media::ParamDesc> describeSequencia();
std::unique_ptr<IEffect> makeSequencia();

std::vector<media::ParamDesc> describeDisparo();
std::unique_ptr<IEffect> makeDisparo();

std::vector<media::ParamDesc> describeAtrasoSlices();
std::unique_ptr<IEffect> makeAtrasoSlices();

std::vector<media::ParamDesc> describeSlicesAleatorios();
std::unique_ptr<IEffect> makeSlicesAleatorios();

std::vector<media::ParamDesc> describeContorno();
std::unique_ptr<IEffect> makeContorno();

std::vector<media::ParamDesc> describeLadrilhos();
std::unique_ptr<IEffect> makeLadrilhos();

std::vector<media::ParamDesc> describeAmostrador();
std::unique_ptr<IEffect> makeAmostrador();

std::vector<media::ParamDesc> describeVarredura();
std::unique_ptr<IEffect> makeVarredura();

std::vector<media::ParamDesc> describeBordaNeon();
std::unique_ptr<IEffect> makeBordaNeon();

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
