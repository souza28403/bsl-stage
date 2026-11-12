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

std::vector<media::ParamDesc> describeGeradorContorno();
std::unique_ptr<media::ISource> makeGeradorContorno(int canvasW, int canvasH);

std::vector<media::ParamDesc> describeGeradorLadrilhos();
std::unique_ptr<media::ISource> makeGeradorLadrilhos(int canvasW, int canvasH);

std::vector<media::ParamDesc> describeGeradorBordaNeon();
std::unique_ptr<media::ISource> makeGeradorBordaNeon(int canvasW, int canvasH);

const GeneratorInfo* bslGeneratorInfo(const std::string& id);

}
