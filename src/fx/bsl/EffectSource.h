#pragma once

#include "fx/IEffect.h"
#include "media/ISource.h"

#include <memory>
#include <vector>

namespace bsl::fx::bsllight {

std::unique_ptr<media::ISource> makeEffectSource(std::unique_ptr<IEffect> efeito, std::vector<media::ParamDesc> descs,
                                                 int canvasW, int canvasH);

std::vector<media::ParamDesc> descreverGerador(std::vector<media::ParamDesc> descs);

}
