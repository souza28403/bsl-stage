#pragma once

#include "core/Generators.h"
#include "media/ISource.h"

#include <memory>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

std::vector<media::ParamDesc> describePixelMapPattern();
std::unique_ptr<media::ISource> makePixelMapPattern(int canvasW, int canvasH);

std::vector<media::ParamDesc> describeWebBackgrounds(const media::ParamValues& values);
std::unique_ptr<media::ISource> makeWebBackgrounds(int canvasW, int canvasH);
bool bslWebAvailable();

const GeneratorInfo* bslGeneratorInfo(const std::string& id);

}
