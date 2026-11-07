#pragma once

#include "media/SourceParams.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bsl::ui {

struct ParamEdit
{
    std::string id;
    media::Json value;
};

using CustomParamEditor =
    std::function<bool(const media::ParamDesc& desc, const media::Json& current, media::Json& out)>;

std::vector<ParamEdit> drawParams(const std::vector<media::ParamDesc>& descs, const media::ParamValues& values,
                                  const CustomParamEditor& custom = {});

bool paramConditionMet(const std::string& condition, const media::ParamSet& set);

}
