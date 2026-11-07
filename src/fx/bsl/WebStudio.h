#pragma once

#include "media/SourceParams.h"

#include <string>
#include <vector>

namespace bsl::fx::bsllight {

struct StudioBgParam
{
    std::string id, label, type;
    double min = 0.0, max = 1.0, step = 0.01;
    media::Json def;
    std::vector<std::string> options;
};
struct StudioBackground
{
    std::string id, label, category;
    std::vector<StudioBgParam> params;
};
struct StudioPalette
{
    std::string id, name;
    std::vector<std::string> colors;
};
struct StudioNamed
{
    std::string id, label;
    std::vector<int> weights;
};
struct StudioData
{
    std::vector<StudioBackground> backgrounds;
    std::vector<StudioPalette> palettes;
    std::vector<StudioNamed> fonts, textIn, textLoop, textOut;
    bool loaded = false;
};
const StudioData& studioData();

std::vector<media::ParamDesc> describeStudioParams(const media::ParamValues& values);
media::Json buildStudioState(const media::ParamSet& params);
bool applyStudioState(const media::Json& state, media::ParamValues& values);

}
