#pragma once

#include "fx/EffectChain.h"
#include "ui/PanelContext.h"

#include <imgui.h>

#include <string>

namespace bsl::ui {

inline constexpr const char* kEffectPayload = "BSL_EFFECT";
inline constexpr const char* kEffectMovePayload = "BSL_EFFECT_MOVE";

struct EffectMoveRef
{
    const fx::EffectChain* chain;
    int index;
};

inline constexpr int kNoZoneSource = -2;

struct EffectOwner
{
    int scope = -1;
    int deck = -1;
    int layer = -1;
    int column = -1;
};
bool drawEffectChain(fx::EffectChain& chain, const char* id, PanelContext* ctx = nullptr, int zoneSource = kNoZoneSource,
                     const EffectOwner& owner = {});

std::string foldSearchText(const std::string& s);

std::string acceptEffectDrop();

void drawEffectsBadge(ImDrawList* dl, ImVec2 center, const fx::EffectChain& chain);

}
