#pragma once

#include "fx/IsfLibrary.h"
#include "fx/IsfRenderer.h"

#include <memory>
#include <optional>
#include <string>

namespace bsl::fx {

struct IsfReloadState
{
    std::shared_ptr<const IsfDoc> failedDoc;
    std::string error;
};

enum class IsfReloadResult { Unchanged, Replaced };

IsfReloadResult isfReload(std::unique_ptr<IsfRenderer>& renderer, const std::optional<IsfEntry>& entry, bool glReady,
                          bool wantEffect, IsfReloadState& state);

std::string isfReloadMessage(const IsfReloadState& state);

}
