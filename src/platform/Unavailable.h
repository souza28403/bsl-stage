#pragma once

#include "app/Log.h"

#include <string>

namespace bslplat {

inline std::string unavailableText(const char* feature)
{
    return std::string(feature) + ": indisponivel nesta plataforma";
}

inline void reportUnavailable(const char* feature)
{
    bsl::log::warn(unavailableText(feature));
}

}
