#pragma once

#include "seq/SeqModel.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bsl::seq {

nlohmann::json conjuntoParaJson(const SequenceSet& conjunto);
SequenceSet conjuntoDeJson(const nlohmann::json& j, std::vector<std::string>* avisos = nullptr);

}
