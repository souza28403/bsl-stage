#pragma once

#include <string>
#include <vector>

namespace bsl::plugin {

enum class Kind { Effect, Generator };

enum class Template { Blank, ColorAdjust, UvDistort, Feedback, Pattern, AudioReactive };
inline constexpr int kTemplateCount = 6;

enum class ParamKind { Float, Color, Bool, Point2D, Image, Event, Audio, Long };

struct ParamSpec
{
    std::string name;
    std::string label;
    ParamKind kind = ParamKind::Float;
    double minV = 0.0, maxV = 1.0, defV = 0.0;
    bool defBool = false;
    float defColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float defPoint[2] = {960.0f, 540.0f};
    std::vector<std::string> options;
    std::string imageFile;
};

struct PluginSpec
{
    std::string name;
    Kind kind = Kind::Effect;
    std::string category = "Meus plugins";
    Template tmpl = Template::Blank;
    std::string description;
    std::string author;
    std::vector<ParamSpec> params;
};

const char* templateLabel(Template t, Kind kind);
const char* templateHint(Template t, Kind kind);
bool templateAvailable(Template t, Kind kind);

std::vector<ParamSpec> defaultParams(Template t, Kind kind);

const std::vector<std::string>& suggestedCategories();

std::string identFromLabel(const std::string& label, const std::vector<std::string>& taken = {});
bool isValidIdent(const std::string& name);
bool isReservedIdent(const std::string& name);

std::string validateSpec(const PluginSpec& spec);

std::string generateIsf(const PluginSpec& spec, const std::vector<std::string>& imageFiles = {});

}
