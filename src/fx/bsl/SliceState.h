#pragma once

#include "media/SourceParams.h"

#include <map>
#include <string>

namespace bsl::fx::bsllight {

inline constexpr int kSliceGroups = 8;
inline constexpr int kSliceEffects = 12;

extern const char* const kSliceEffectIds[kSliceEffects];
extern const char* const kSliceEffectLabels[kSliceEffects];

enum class SliceMode { Off = 0, Speed = 1, Bpm = 2 };

struct SliceGroup
{
    std::string name;
    bool enabled = true;
    bool fillVideo = true;
    float masterSize = 0.25f;
    float corners[4] = {1, 1, 1, 1};
    bool equalCorners = true;
    float offset = 0.0f;
    bool merge = true;
    float mergeSmooth = 0.0f;
    float softEdge = 0.0f;

    float strokeWidth = 6.0f;
    bool strokeEffectMode = false;
    float color[3] = {1, 1, 1};
    float colorB[3] = {0, 0, 0};
    int effect = 0;
    float speed = 0.5f;
    float variation = 0.5f;
    bool smooth = true;
    bool symX = false, symY = false;
    float glow = 0.0f;
    bool dualOn = false;
    float dualWidth = 3.0f, dualGap = 8.0f;

    SliceMode blinkMode = SliceMode::Off;
    float blinkRate = 2.0f;

    SliceMode bounceMode = SliceMode::Off;
    float bounceSpeed = 1.0f;
    float bounceBeats = 1.0f;
    float bounceMin = 0.85f;
    bool bounceMulti = false;
    bool bounceSmooth = true;

    bool rotOn = false;
    float rotSpeed = 0.2f;
    float rotBars = 0.0f;
};

struct SliceState
{
    std::map<std::string, int> assign;
    SliceGroup groups[kSliceGroups];

    SliceState();
};

void setDefaultSliceGroup(SliceGroup& g, int index);

bool sliceStateFromJson(const media::Json& j, SliceState& out, std::string* error = nullptr);
media::Json sliceStateToJson(const SliceState& s);
media::Json defaultSliceStateJson();

bool parseHexColor(const std::string& s, float rgb[3]);
std::string formatHexColor(const float rgb[3]);

}
