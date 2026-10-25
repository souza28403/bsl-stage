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

// Editor de parametros do tipo Custom: desenha o controle de 'desc' (valor atual em 'current') e devolve true
// com o novo valor em 'out' quando o usuario muda algo.
using CustomParamEditor =
    std::function<bool(const media::ParamDesc& desc, const media::Json& current, media::Json& out)>;

// Desenha qualquer lista de parametros (rotulo cinza a esquerda, controle ao lado, secoes por "group").
// Devolve as mudancas feitas neste quadro. O chamador grava (Clip::setParam).
std::vector<ParamEdit> drawParams(const std::vector<media::ParamDesc>& descs, const media::ParamValues& values,
                                  const CustomParamEditor& custom = {});

// "id", "!id", "id=valor" ou "id!=valor" contra os valores atuais (ParamDesc::enabledIf)
bool paramConditionMet(const std::string& condition, const media::ParamSet& set);

} // namespace bsl::ui
