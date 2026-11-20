#pragma once

#include "app/PerformanceProfile.h"
#include "app/Settings.h"
#include "core/Composition.h"
#include "output/OutputSetup.h"
#include "seq/Envelope.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bsl {

namespace control {
class KeyService;
}

namespace dmx {
class Service;
}

namespace update {
class Service;
}

class Compositor;
class OutputRenderer;
class ProjectSession;
class ShowCacheQueue;
class ThumbnailCache;

enum class AppMode { Composition, Mapping, PixelMap, Sequences };

struct MappingSelection
{
    int screen = 0;
    int slice = 0;
    int corner = -1;

    std::vector<int> multi;
    int multiScreen = -1;

    bool pointsMode = false;
};

struct Selection
{
    enum class Kind { None, Layer, Clip };

    Kind kind = Kind::None;
    int layer = -1;
    int column = -1;
};

struct FrameStats
{
    float fps = 0.0f;
    float frameMs = 0.0f;
    float workMs = 0.0f;
};

struct GlInfo
{
    std::string version;
    std::string gpu;
};

struct DropRequest
{
    bool pending = false;
    float x = 0.0f;
    float y = 0.0f;
    std::vector<std::filesystem::path> files;
};

struct PanelContext
{
    Composition& composition;
    Selection& selection;
    const Compositor& canvas;
    PerformanceProfile& profile;
    const FrameStats& stats;
    const GlInfo& glInfo;
    float previewScale;
    Settings& settings;
    ThumbnailCache& thumbs;
    DropRequest& drop;
    bool& quitRequested;
    OutputSetup& output;
    OutputRenderer& outputRenderer;
    MappingSelection& mapping;
    AppMode& mode;
    ProjectSession& project;
    control::KeyService& keys;
    dmx::Service& dmx;
    update::Service& updates;
    seq::BibliotecaEnvelopes& envelopes;
    ShowCacheQueue& showCache;
};

}
