#pragma once

#include "media/SourceParams.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bsl::fx {

enum class IsfInputType { Float, Bool, Long, Color, Point2D, Event, Image, Audio, AudioFft, Cube, Unknown };

struct IsfInput
{
    std::string name;
    std::string label;
    IsfInputType type = IsfInputType::Unknown;
    double minV = 0.0, maxV = 1.0, defV = 0.0;
    bool defBool = false;
    std::vector<long long> values;
    std::vector<std::string> labels;
    float defColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float defPoint[2] = {0.0f, 0.0f};
    float minPoint[2] = {0.0f, 0.0f};
    float maxPoint[2] = {0.0f, 0.0f};
};

struct IsfPass
{
    std::string target;
    bool persistent = false;
    bool isFloat = false;
    std::string widthExpr;
    std::string heightExpr;
};

struct IsfImported
{
    std::string name;
    std::string path;
};

struct IsfDoc
{
    std::string name;
    std::string description;
    std::string credit;
    std::vector<std::string> categories;
    std::vector<IsfInput> inputs;
    std::vector<IsfPass> passes;
    std::vector<IsfImported> imported;
    std::string fragmentSource;
    std::string vertexSource;
    std::vector<std::string> warnings;

    bool isEffect() const;
    int passCount() const { return passes.empty() ? 1 : static_cast<int>(passes.size()); }
};

struct IsfParseResult
{
    bool ok = false;
    std::string error;
    std::shared_ptr<IsfDoc> doc;
};

IsfParseResult parseIsf(const std::string& fragment, const std::string& vertex, const std::string& name);

std::vector<media::ParamDesc> isfParamDescs(const IsfDoc& doc);

using IsfVariableLookup = std::function<bool(const std::string& name, double& out)>;
bool evalIsfExpression(const std::string& expr, const IsfVariableLookup& variable, double& result);

struct IsfSources
{
    std::string vertex;
    std::string fragment;
    int vertexPreambleLines = 0;
    int fragmentPreambleLines = 0;
    std::vector<std::string> imageNames;
};
IsfSources buildIsfSources(const IsfDoc& doc);

std::string rewriteIsfSource(const std::string& source, bool fragmentStage);

struct IsfCompileLog
{
    std::string text;
    int firstLine = 0;
};
IsfCompileLog remapGlslLog(const std::string& log, int preambleLines, const char* stage);

}
