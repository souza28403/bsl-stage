#pragma once

#include "ffgl/FfglInfo.h"
#include "media/SourceParams.h"

#include <string>
#include <vector>

namespace bsl::ffgl {

enum class BindKind { Float, Int, Bool, Event, Option, Text, File, Rgb, Hsb, Point };

struct Binding
{
    BindKind kind = BindKind::Float;
    int params[4] = {-1, -1, -1, -1};
    int count = 1;
    float min = 0.0f, max = 1.0f;
    std::vector<OptionInfo> options;
};

struct ParamMap
{
    std::vector<media::ParamDesc> descs;
    std::vector<Binding> bindings;
    int paramCount = 0;
};

ParamMap buildParamMap(const PluginInfo& info);

struct PluginValue
{
    float f = 0.0f;
    std::string s;
};

struct IndexedValue
{
    int index = 0;
    PluginValue value;
};

void toPlugin(const ParamMap& map, size_t bindingIndex, const media::ParamSet& set, std::vector<IndexedValue>& out);
media::Json fromPlugin(const ParamMap& map, size_t bindingIndex, const std::vector<PluginValue>& plugin);
bool pluginValueDiffers(const PluginValue& a, const PluginValue& b);

std::string optionKey(float value);
float optionValue(const std::string& key);

void rgbToHsb(float r, float g, float b, float& h, float& s, float& v);
void hsbToRgb(float h, float s, float v, float& r, float& g, float& b);

}
