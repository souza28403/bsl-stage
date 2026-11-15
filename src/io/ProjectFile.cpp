#include "io/ProjectFile.h"

#include "app/Paths.h"
#include "control/ActionRegistry.h"
#include "seq/EnvelopeStore.h"
#include "seq/SeqJson.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace bsl::project {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

constexpr int kMaxLayers = 64;
constexpr int kMaxDecks = 64;
constexpr int kMaxColumns = 256;
constexpr int kMaxScreens = 32;
constexpr int kMaxSlices = 128;

struct BlendKey { BlendMode mode; const char* key; };
constexpr BlendKey kBlendKeys[] = {
    {BlendMode::Normal, "normal"},     {BlendMode::Add, "add"},         {BlendMode::Subtract, "subtract"},
    {BlendMode::Multiply, "multiply"}, {BlendMode::Screen, "screen"},   {BlendMode::Overlay, "overlay"},
    {BlendMode::Lighten, "lighten"},   {BlendMode::Darken, "darken"},   {BlendMode::Difference, "difference"},
    {BlendMode::LumaMask, "luma-mask"}, {BlendMode::LumaMaskInv, "luma-mask-inv"}};

struct LoopKey { media::LoopMode mode; const char* key; };
constexpr LoopKey kLoopKeys[] = {{media::LoopMode::Loop, "loop"},
                                 {media::LoopMode::Once, "once"},
                                 {media::LoopMode::PingPong, "pingpong"}};

struct FitKey { FitMode mode; const char* key; };
constexpr FitKey kFitKeys[] = {{FitMode::Fit, "fit"}, {FitMode::Fill, "fill"},
                               {FitMode::Stretch, "stretch"}, {FitMode::Original, "original"}};

struct KindKey { OutputKind mode; const char* key; };
constexpr KindKey kKindKeys[] = {{OutputKind::Disabled, "disabled"}, {OutputKind::Monitor, "monitor"},
                                 {OutputKind::Window, "window"}, {OutputKind::Spout, "spout"}, {OutputKind::Ndi, "ndi"}};

struct ZoneKey { int mode; const char* key; };
constexpr ZoneKey kZoneKeys[] = {{kZoneAll, "all"}, {kZoneFront, "front"}, {kZoneMiddle, "middle"},
                                 {kZoneBack, "back"}, {kZone4, "zone4"}};

template <typename Table, typename E>
const char* keyOf(const Table& table, E value)
{
    for (const auto& e : table)
        if (e.mode == value)
            return e.key;
    return table[0].key;
}

template <typename Table, typename E>
E fromKey(const Table& table, const std::string& key, E fallback)
{
    for (const auto& e : table)
        if (key == e.key)
            return e.mode;
    return fallback;
}

std::string u8(const fs::path& p)
{
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

fs::path pathFromU8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

Json num(float v)
{
    if (!std::isfinite(v))
        return Json(0);
    char buf[48];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    const double d = std::strtod(std::string(buf, r.ptr).c_str(), nullptr);
    if (d == std::floor(d) && std::fabs(d) < 1e9)
        return Json(static_cast<long long>(d));
    return Json(d);
}

Json transformToJson(const LayerTransform& t)
{
    Json j = Json::object();
    j["x"] = num(t.x);
    j["y"] = num(t.y);
    j["w"] = num(t.w);
    j["h"] = num(t.h);
    j["rotation"] = num(t.rotation);
    j["anchor"] = Json::array({num(t.anchorX), num(t.anchorY)});
    j["crop"] = Json::array({num(t.cropL), num(t.cropT), num(t.cropR), num(t.cropB)});
    return j;
}

bool transformIsDefault(const LayerTransform& t, int canvasW, int canvasH)
{
    return t.settled(canvasW, canvasH).isDefault();
}

Json clipToJson(int layer, int column, const Clip& clip, const fs::path& projectDirAbs, int canvasW, int canvasH)
{
    Json j = Json::object();
    j["layer"] = layer;
    j["column"] = column;
    if (clip.type == Clip::Type::Generator) {
        j["generator"] = clip.generatorId;
        if (!clip.params.empty())
            j["params"] = Json(clip.params.json());
    } else {
        std::error_code ec;
        const fs::path abs = fs::absolute(clip.path, ec).lexically_normal();
        j["path"] = u8(abs);
        if (!projectDirAbs.empty()) {
            const fs::path rel = abs.lexically_relative(projectDirAbs);
            if (!rel.empty())
                j["relPath"] = u8(rel);
        }
        if (!clip.params.empty())
            j["params"] = Json(clip.params.json());
    }
    j["speed"] = num(clip.speed);
    j["loopMode"] = keyOf(kLoopKeys, clip.loop);
    j["fitMode"] = keyOf(kFitKeys, clip.fit);
    if (clip.replay != clip.defaultReplay())
        j["replayMode"] = clip.replay == ReplayMode::Resume ? "resume" : "restart";
    if (!clip.effects.empty())
        j["effects"] = Json(clip.effects.toJson());
    if (clip.ownTransform || !transformIsDefault(clip.transform, canvasW, canvasH)) {
        j["ownTransform"] = clip.ownTransform;
        j["transform"] = transformToJson(clip.transform.settled(canvasW, canvasH));
    }
    return j;
}

Json sliceToJson(const Slice& s)
{
    Json j = Json::object();
    j["name"] = s.name;
    j["enabled"] = s.enabled;
    j["uid"] = s.uid;
    j["input"] = {{"x", s.input.x}, {"y", s.input.y}, {"w", s.input.w}, {"h", s.input.h}};
    Json corners = Json::array();
    for (const Point2& c : s.corners)
        corners.push_back({{"x", num(c.x)}, {"y", num(c.y)}});
    j["corners"] = std::move(corners);
    j["flipH"] = s.flipH;
    j["flipV"] = s.flipV;
    j["opacity"] = num(s.opacity);
    j["gain"] = Json::array({num(s.gain[0]), num(s.gain[1]), num(s.gain[2])});
    j["brightness"] = num(s.brightness);
    Json edges = Json::array();
    for (const SoftEdge& e : s.edges)
        edges.push_back({{"width", num(e.width)}, {"gamma", num(e.gamma)}});
    j["softEdges"] = std::move(edges);
    if (s.zFilter != kZoneAll || s.zSource != kZoneSourceMaster) {
        j["zFilter"] = keyOf(kZoneKeys, s.zFilter);
        j["zSource"] = s.zSource;
    }
    if (s.source == kSliceFromLayer) {
        j["source"] = "layer";
        j["layerId"] = s.layerId;
        j["includeMaster"] = s.includeMaster;
        if (s.applyLayerTransform)
            j["applyLayerTransform"] = true;
    }
    return j;
}

Json screenToJson(const Screen& s)
{
    Json j = Json::object();
    j["name"] = s.name;
    j["enabled"] = s.enabled;
    j["kind"] = keyOf(kKindKeys, s.kind);
    j["monitor"] = {{"index", s.monitorIndex}, {"name", s.monitorName}};
    j["spoutName"] = s.spoutName;
    j["ndiName"] = s.ndiName;
    j["ndiBgra"] = s.ndiBgra;
    j["width"] = s.width;
    j["height"] = s.height;
    Json slices = Json::array();
    for (const Slice& sl : s.slices)
        slices.push_back(sliceToJson(sl));
    j["slices"] = std::move(slices);
    return j;
}

const Json* member(const Json& j, const char* key)
{
    if (!j.is_object())
        return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

float getFloat(const Json& j, const char* key, float def, float lo, float hi)
{
    const Json* m = member(j, key);
    if (!m || !m->is_number())
        return def;
    return std::clamp(m->get<float>(), lo, hi);
}

int getInt(const Json& j, const char* key, int def, int lo, int hi)
{
    const Json* m = member(j, key);
    if (!m || !m->is_number())
        return def;
    const double d = m->get<double>();
    return static_cast<int>(std::clamp(std::llround(d), static_cast<long long>(lo), static_cast<long long>(hi)));
}

bool getBool(const Json& j, const char* key, bool def)
{
    const Json* m = member(j, key);
    return m && m->is_boolean() ? m->get<bool>() : def;
}

std::string getStr(const Json& j, const char* key, const std::string& def = {})
{
    const Json* m = member(j, key);
    return m && m->is_string() ? m->get<std::string>() : def;
}

const Json* getObj(const Json& j, const char* key)
{
    const Json* m = member(j, key);
    return m && m->is_object() ? m : nullptr;
}

const Json* getArr(const Json& j, const char* key)
{
    const Json* m = member(j, key);
    return m && m->is_array() ? m : nullptr;
}

Slice sliceFromJson(const Json& j, int index, int canvasW, int canvasH, int screenW, int screenH)
{
    Slice s = makeFullSlice("Slice " + std::to_string(index + 1), canvasW, canvasH, screenW, screenH);
    s.name = getStr(j, "name", s.name);
    s.enabled = getBool(j, "enabled", s.enabled);
    s.uid = getInt(j, "uid", 0, 0, 1 << 30);

    bool hasInput = false;
    if (const Json* in = getObj(j, "input")) {
        hasInput = true;
        s.input.x = getInt(*in, "x", 0, 0, 65536);
        s.input.y = getInt(*in, "y", 0, 0, 65536);
        s.input.w = getInt(*in, "w", canvasW, 1, 65536);
        s.input.h = getInt(*in, "h", canvasH, 1, 65536);
    }

    const Json* corners = getArr(j, "corners");
    if (corners && corners->size() == 4) {
        for (size_t i = 0; i < 4; ++i) {
            const Json& c = (*corners)[i];
            s.corners[i].x = getFloat(c, "x", s.corners[i].x, -1.0e6f, 1.0e6f);
            s.corners[i].y = getFloat(c, "y", s.corners[i].y, -1.0e6f, 1.0e6f);
        }
    } else if (hasInput) {
        fitOutputToInput(s);
    }

    s.flipH = getBool(j, "flipH", false);
    s.flipV = getBool(j, "flipV", false);
    s.opacity = getFloat(j, "opacity", 1.0f, 0.0f, 1.0f);
    if (const Json* g = getArr(j, "gain"); g && g->size() == 3)
        for (size_t i = 0; i < 3; ++i)
            s.gain[i] = (*g)[i].is_number() ? std::clamp((*g)[i].get<float>(), 0.0f, 8.0f) : 1.0f;
    s.brightness = getFloat(j, "brightness", 0.0f, -1.0f, 1.0f);
    if (const Json* e = getArr(j, "softEdges"); e && e->size() == 4)
        for (size_t i = 0; i < 4; ++i) {
            s.edges[i].width = getFloat((*e)[i], "width", 0.0f, 0.0f, 16384.0f);
            s.edges[i].gamma = getFloat((*e)[i], "gamma", 1.0f, 0.05f, 16.0f);
        }
    s.zFilter = fromKey(kZoneKeys, getStr(j, "zFilter"), static_cast<int>(kZoneAll));
    s.zSource = getInt(j, "zSource", kZoneSourceMaster, kZoneSourceMaster, 1023);
    if (getStr(j, "source") == "layer") {
        s.source = kSliceFromLayer;
        s.layerId = getInt(j, "layerId", 0, 0, 1 << 30);
        s.includeMaster = getBool(j, "includeMaster", false);
        s.applyLayerTransform = getBool(j, "applyLayerTransform", false);
    }
    return s;
}

Screen screenFromJson(const Json& j, int index, int canvasW, int canvasH)
{
    Screen s;
    s.name = getStr(j, "name", "Tela " + std::to_string(index + 1));
    s.enabled = getBool(j, "enabled", true);
    s.kind = fromKey(kKindKeys, getStr(j, "kind"), OutputKind::Disabled);
    if (const Json* m = getObj(j, "monitor")) {
        s.monitorIndex = getInt(*m, "index", -1, -1, 1024);
        s.monitorName = getStr(*m, "name");
    }
    s.spoutName = getStr(j, "spoutName", "BSL Stage - " + s.name);
    s.ndiName = getStr(j, "ndiName", "BSL Stage - " + s.name);
    s.ndiBgra = getBool(j, "ndiBgra", false);
    s.width = getInt(j, "width", canvasW, 16, 16384);
    s.height = getInt(j, "height", canvasH, 16, 16384);
    if (const Json* slices = getArr(j, "slices")) {
        int i = 0;
        for (const Json& sj : *slices) {
            if (i >= kMaxSlices)
                break;
            s.slices.push_back(sliceFromJson(sj, i, canvasW, canvasH, s.width, s.height));
            ++i;
        }
    }
    return s;
}

fs::path resolveClipPath(const fs::path& projectDirAbs, const std::string& relPath, const std::string& path,
                         bool& found)
{
    std::error_code ec;
    found = false;
    if (!projectDirAbs.empty() && !relPath.empty()) {
        const fs::path candidate = (projectDirAbs / pathFromU8(relPath)).lexically_normal();
        if (fs::is_regular_file(candidate, ec)) {
            found = true;
            return candidate;
        }
    }
    if (!path.empty()) {
        const fs::path candidate = pathFromU8(path);
        found = fs::is_regular_file(candidate, ec);
        return candidate;
    }
    if (!projectDirAbs.empty() && !relPath.empty())
        return (projectDirAbs / pathFromU8(relPath)).lexically_normal();
    return {};
}

LayerTransform transformFromJson(const Json* tj, int canvasW, int canvasH)
{
    LayerTransform t;
    if (!tj || !tj->is_object())
        return t;
    t.x = getFloat(*tj, "x", 0.0f, -1.0e6f, 1.0e6f);
    t.y = getFloat(*tj, "y", 0.0f, -1.0e6f, 1.0e6f);
    t.w = getFloat(*tj, "w", 0.0f, 0.0f, 1.0e6f);
    t.h = getFloat(*tj, "h", 0.0f, 0.0f, 1.0e6f);
    t.rotation = getFloat(*tj, "rotation", 0.0f, -36000.0f, 36000.0f);
    if (const Json* a = getArr(*tj, "anchor"); a && a->size() == 2 && (*a)[0].is_number() && (*a)[1].is_number()) {
        t.anchorX = std::clamp((*a)[0].get<float>(), 0.0f, 1.0f);
        t.anchorY = std::clamp((*a)[1].get<float>(), 0.0f, 1.0f);
    }
    if (const Json* c = getArr(*tj, "crop"); c && c->size() == 4) {
        float* dst[4] = {&t.cropL, &t.cropT, &t.cropR, &t.cropB};
        for (size_t i = 0; i < 4; ++i)
            *dst[i] = (*c)[i].is_number() ? std::clamp((*c)[i].get<float>(), 0.0f, 1.0e6f) : 0.0f;
    }
    t.clampCrop(canvasW, canvasH);
    return t.settled(canvasW, canvasH);
}

fx::EffectChain effectsFromJson(const Json& j, std::vector<std::string>& warnings)
{
    const Json* arr = getArr(j, "effects");
    if (!arr)
        return {};
    return fx::EffectChain::fromJson(media::Json::parse(arr->dump(), nullptr, false), &warnings);
}

Clip clipFromJson(const Json& j, const fs::path& projectDirAbs, std::vector<std::string>& warnings, int& missing,
                  int canvasW, int canvasH)
{
    Clip clip;
    if (!j.is_object())
        return clip;

    if (const Json* gen = member(j, "generator"); gen && gen->is_string()) {
        clip = Clip::generator(gen->get<std::string>());
        if (clip.empty()) {
            clip = Clip::absentGenerator(gen->get<std::string>());
            warnings.push_back("Plugin ausente: " + gen->get<std::string>() + " (a célula fica em âmbar; os parâmetros são mantidos).");
        }
        if (const Json* params = getObj(j, "params")) {
            for (const auto& [key, value] : params->items())
                clip.params.set(key, media::Json::parse(value.dump(), nullptr, false));
            clip.refreshName();
        }
    } else {
        bool found = false;
        const fs::path file = resolveClipPath(projectDirAbs, getStr(j, "relPath"), getStr(j, "path"), found);
        if (file.empty()) {
            warnings.push_back("Clip sem caminho de arquivo ignorado.");
            return clip;
        }
        const media::MediaKind kind = media::classify(file);
        if (kind == media::MediaKind::Unknown) {
            warnings.push_back("Formato não suportado ignorado: " + paths::toUtf8(file.filename()));
            return clip;
        }
        clip.type = Clip::Type::File;
        clip.kind = kind;
        clip.path = file;
        clip.name = paths::toUtf8(file.filename());
        clip.missing = !found;
        if (clip.missing)
            ++missing;
        if (media::isRemoteKind(kind)) {
            clip.params.fillDefaults(clip.paramDescs());
            if (const Json* params = getObj(j, "params")) {
                for (const auto& [key, value] : params->items())
                    clip.params.set(key, media::Json::parse(value.dump(), nullptr, false));
            }
            clip.refreshName();
        }
    }

    clip.effects = effectsFromJson(j, warnings);
    clip.setSpeed(getFloat(j, "speed", 1.0f, 0.0f, 4.0f));
    clip.setLoop(fromKey(kLoopKeys, getStr(j, "loopMode"), media::LoopMode::Loop));
    clip.fit = fromKey(kFitKeys, getStr(j, "fitMode"), FitMode::Fit);
    clip.replay = clip.defaultReplay();
    if (const std::string mode = getStr(j, "replayMode"); mode == "resume")
        clip.replay = ReplayMode::Resume;
    else if (mode == "restart")
        clip.replay = ReplayMode::Restart;
    clip.ownTransform = getBool(j, "ownTransform", false);
    clip.transform = transformFromJson(getObj(j, "transform"), canvasW, canvasH);
    return clip;
}

fs::path absoluteDir(const fs::path& dir)
{
    if (dir.empty())
        return {};
    std::error_code ec;
    return fs::absolute(dir, ec).lexically_normal();
}

constexpr const char* kWatermarkKeys[] = {"monogram", "image", "none"};

bool pixelMapIsDefault(const pixelmap::Config& c)
{
    pixelmap::Config d;
    d.inW = c.inW;
    d.inH = c.inH;
    return c == d;
}

Json pixelMapToJson(const pixelmap::Config& c)
{
    Json j = Json::object();
    j["input"] = {{"w", c.inW}, {"h", c.inH}, {"linked", c.linkCanvas}};
    j["physical"] = {{"w", c.physW}, {"h", c.physH}};
    Json wm = Json::object();
    wm["kind"] = kWatermarkKeys[static_cast<int>(c.watermark)];
    if (c.watermark == pixelmap::Watermark::Image)
        wm["path"] = c.watermarkPath;
    j["watermark"] = std::move(wm);
    Json panels = Json::array();
    for (const pixelmap::Panel& p : c.panels) {
        Json pj = Json::object();
        pj["name"] = p.name;
        pj["x"] = p.x;
        pj["y"] = p.y;
        pj["w"] = p.w;
        pj["h"] = p.h;
        if (p.color != 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "#%06X", p.color & 0xFFFFFFu);
            pj["color"] = buf;
        }
        if (p.byCabinets)
            pj["cabinets"] = {{"cols", p.cols}, {"rows", p.rows}, {"w", p.cabW}, {"h", p.cabH}};
        if (p.port != 0)
            pj["port"] = p.port;
        if (p.cableOrder >= 0)
            pj["cableOrder"] = p.cableOrder;
        panels.push_back(std::move(pj));
    }
    j["panels"] = std::move(panels);
    return j;
}

pixelmap::Config pixelMapFromJson(const Json& j, int canvasW, int canvasH)
{
    pixelmap::Config c;
    if (const Json* in = getObj(j, "input")) {
        c.inW = getInt(*in, "w", c.inW, 1, pixelmap::kMaxDim);
        c.inH = getInt(*in, "h", c.inH, 1, pixelmap::kMaxDim);
        c.linkCanvas = getBool(*in, "linked", true);
    }
    if (const Json* ph = getObj(j, "physical")) {
        c.physW = getInt(*ph, "w", c.physW, 1, pixelmap::kMaxDim);
        c.physH = getInt(*ph, "h", c.physH, 1, pixelmap::kMaxDim);
    }
    if (const Json* wm = getObj(j, "watermark")) {
        const std::string kind = getStr(*wm, "kind", "monogram");
        for (int i = 0; i < 3; ++i)
            if (kind == kWatermarkKeys[i])
                c.watermark = static_cast<pixelmap::Watermark>(i);
        c.watermarkPath = getStr(*wm, "path");
        if (c.watermark == pixelmap::Watermark::Image && c.watermarkPath.empty())
            c.watermark = pixelmap::Watermark::Monogram;
    }
    if (const Json* panels = getArr(j, "panels")) {
        for (const Json& pj : *panels) {
            if (static_cast<int>(c.panels.size()) >= pixelmap::kMaxPanels)
                break;
            pixelmap::Panel p;
            p.name = getStr(pj, "name", "Painel " + std::to_string(c.panels.size() + 1));
            p.x = getInt(pj, "x", 0, -pixelmap::kMaxDim, pixelmap::kMaxDim);
            p.y = getInt(pj, "y", 0, -pixelmap::kMaxDim, pixelmap::kMaxDim);
            p.w = getInt(pj, "w", 128, 1, pixelmap::kMaxDim);
            p.h = getInt(pj, "h", 128, 1, pixelmap::kMaxDim);
            const std::string col = getStr(pj, "color");
            if (col.size() == 7 && col[0] == '#')
                p.color = static_cast<std::uint32_t>(std::strtoul(col.c_str() + 1, nullptr, 16)) & 0xFFFFFFu;
            if (const Json* cab = getObj(pj, "cabinets")) {
                p.byCabinets = true;
                p.cols = getInt(*cab, "cols", 1, 1, 256);
                p.rows = getInt(*cab, "rows", 1, 1, 256);
                p.cabW = getInt(*cab, "w", 128, 1, pixelmap::kMaxDim);
                p.cabH = getInt(*cab, "h", 128, 1, pixelmap::kMaxDim);
            }
            p.port = getInt(pj, "port", 0, 0, 255);
            p.cableOrder = getInt(pj, "cableOrder", -1, -1, 65535);
            c.panels.push_back(std::move(p));
        }
    }
    pixelmap::normalize(c);
    pixelmap::syncInput(c, canvasW, canvasH);
    return c;
}

}

std::string serialize(const Composition& comp, const OutputSetup& outputIn, const fs::path& projectDir)
{
    OutputSetup output = outputIn;
    ensureSliceUids(output);
    const fs::path dirAbs = absoluteDir(projectDir);

    Json root = Json::object();
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;

    Json c = Json::object();
    c["width"] = comp.width;
    c["height"] = comp.height;
    c["fps"] = num(comp.fps);
    c["bpm"] = num(comp.bpm);
    c["master"] = {{"opacity", num(comp.masterOpacity)}, {"bypass", comp.masterBypass}};
    if (!comp.masterEffects.empty())
        c["master"]["effects"] = Json(comp.masterEffects.toJson());
    c["currentDeck"] = comp.currentDeck;
    c["nextLayerId"] = comp.nextLayerId;
    root["composition"] = std::move(c);

    Json layers = Json::array();
    for (const Layer& l : comp.layers) {
        Json lj = Json::object();
        lj["id"] = l.id;
        lj["name"] = l.name;
        lj["opacity"] = num(l.opacity);
        lj["blend"] = keyOf(kBlendKeys, l.blendMode);
        lj["bypass"] = l.bypass;
        lj["solo"] = l.solo;
        if (l.sliceOnly)
            lj["sliceOnly"] = true;
        if (!l.effects.empty())
            lj["effects"] = Json(l.effects.toJson());
        if (!transformIsDefault(l.transform, comp.width, comp.height))
            lj["transform"] = transformToJson(l.transform.settled(comp.width, comp.height));
        layers.push_back(std::move(lj));
    }
    root["layers"] = std::move(layers);

    Json decks = Json::array();
    for (const Deck& d : comp.decks) {
        Json dj = Json::object();
        dj["name"] = d.name;
        dj["columns"] = d.columnCount();
        Json clips = Json::array();
        for (size_t l = 0; l < d.clips.size(); ++l)
            for (size_t col = 0; col < d.clips[l].size(); ++col)
                if (!d.clips[l][col].empty())
                    clips.push_back(clipToJson(static_cast<int>(l), static_cast<int>(col), d.clips[l][col], dirAbs, comp.width, comp.height));
        dj["clips"] = std::move(clips);
        decks.push_back(std::move(dj));
    }
    root["decks"] = std::move(decks);

    Json screens = Json::array();
    for (const Screen& s : output.screens)
        screens.push_back(screenToJson(s));
    Json out = Json::object();
    out["screens"] = std::move(screens);
    out["nextSliceUid"] = output.nextSliceUid;
    root["output"] = std::move(out);

    pixelmap::Config pm = comp.pixelMap;
    pixelmap::syncInput(pm, comp.width, comp.height);
    if (!pixelMapIsDefault(pm))
        root["pixelMap"] = pixelMapToJson(pm);

    if (!(comp.keys == control::defaultProjectKeys()))
        root["keys"] = Json(control::bindingsToJson(comp.keys));

    if (!comp.sequences.vazio() || !comp.envelopes.empty()) {
        Json sj = Json(seq::conjuntoParaJson(comp.sequences));
        if (!comp.envelopes.empty()) {
            Json lista = Json::array();
            for (const seq::Envelope& e : comp.envelopes)
                lista.push_back(Json(seq::envelopeParaJson(e)));
            sj["envelopes"] = std::move(lista);
        }
        root["sequences"] = std::move(sj);
    }

    if (!comp.dmx.empty())
        root["dmx"] = Json(dmx::configToJson(comp.dmx));

    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

LoadResult parse(const std::string& textIn, const fs::path& projectDir, Composition& comp, OutputSetup& output)
{
    LoadResult res;

    std::string_view text(textIn);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded()) {
        res.error = "o arquivo não é um JSON válido (corrompido ou incompleto)";
        return res;
    }
    if (!root.is_object() || getStr(root, "format") != kFormatName) {
        res.error = "o arquivo não é um projeto do BSL Stage";
        return res;
    }

    const int version = getInt(root, "version", 1, -1000000, 1000000);
    if (version > kFormatVersion)
        res.warnings.push_back("Este projeto foi salvo por uma versão mais nova do BSL Stage (formato "
                               + std::to_string(version) + "; esta versão entende até o "
                               + std::to_string(kFormatVersion)
                               + "). Algumas informações podem ser ignoradas e, ao salvar, serão perdidas.");

    const fs::path dirAbs = absoluteDir(projectDir);

    Composition c;
    c.decoderOptions = comp.decoderOptions;

    const Json emptyObj = Json::object();
    const Json* cj = getObj(root, "composition");
    const Json& cr = cj ? *cj : emptyObj;
    c.width = getInt(cr, "width", c.width, 16, 8192);
    c.height = getInt(cr, "height", c.height, 16, 8192);
    c.fps = getFloat(cr, "fps", c.fps, 1.0f, 480.0f);
    c.bpm = getFloat(cr, "bpm", c.bpm, 20.0f, 300.0f);
    if (const Json* m = getObj(cr, "master")) {
        c.masterOpacity = getFloat(*m, "opacity", 1.0f, 0.0f, 1.0f);
        c.masterBypass = getBool(*m, "bypass", false);
        c.masterEffects = effectsFromJson(*m, res.warnings);
    }

    if (const Json* layers = getArr(root, "layers")) {
        for (const Json& lj : *layers) {
            if (static_cast<int>(c.layers.size()) >= kMaxLayers)
                break;
            Layer l;
            l.id = getInt(lj, "id", 0, 0, 1 << 30);
            l.name = getStr(lj, "name", "Camada " + std::to_string(c.layers.size() + 1));
            l.opacity = getFloat(lj, "opacity", 1.0f, 0.0f, 1.0f);
            l.blendMode = fromKey(kBlendKeys, getStr(lj, "blend"), BlendMode::Normal);
            l.bypass = getBool(lj, "bypass", false);
            l.solo = getBool(lj, "solo", false);
            l.sliceOnly = getBool(lj, "sliceOnly", false);
            l.effects = effectsFromJson(lj, res.warnings);
            l.transform = transformFromJson(getObj(lj, "transform"), c.width, c.height);
            c.layers.push_back(std::move(l));
        }
    }
    if (c.layers.empty()) {
        for (int i = 0; i < 3; ++i) {
            Layer l;
            l.name = "Camada " + std::to_string(i + 1);
            c.layers.push_back(std::move(l));
        }
    }
    c.nextLayerId = getInt(cr, "nextLayerId", 1, 1, 1 << 30);
    c.ensureLayerIds();
    const size_t layerCount = c.layers.size();

    if (const Json* decks = getArr(root, "decks")) {
        for (const Json& dj : *decks) {
            if (static_cast<int>(c.decks.size()) >= kMaxDecks)
                break;
            Deck d;
            d.name = getStr(dj, "name", "Deck " + std::to_string(c.decks.size() + 1));
            const int columns = getInt(dj, "columns", 8, 1, kMaxColumns);
            d.clips.resize(layerCount);
            for (auto& row : d.clips)
                row.resize(static_cast<size_t>(columns));

            if (const Json* clips = getArr(dj, "clips")) {
                for (const Json& clipJson : *clips) {
                    const int layer = getInt(clipJson, "layer", -1, -1, 1000000);
                    const int column = getInt(clipJson, "column", -1, -1, 1000000);
                    if (layer < 0 || layer >= static_cast<int>(layerCount) || column < 0 || column >= columns) {
                        res.warnings.push_back("Clip fora da grade ignorado (camada " + std::to_string(layer + 1)
                                               + ", coluna " + std::to_string(column + 1) + ").");
                        continue;
                    }
                    d.clips[static_cast<size_t>(layer)][static_cast<size_t>(column)] =
                        clipFromJson(clipJson, dirAbs, res.warnings, res.missingFiles, c.width, c.height);
                }
            }
            c.decks.push_back(std::move(d));
        }
    }
    if (c.decks.empty()) {
        c.addDeck();
        c.decks.back().name = "Deck 1";
    }
    c.currentDeck = std::clamp(getInt(cr, "currentDeck", 0, 0, 1000000), 0, c.deckCount() - 1);

    OutputSetup o;
    const Json* oj = getObj(root, "output");
    const Json* screens = oj ? getArr(*oj, "screens") : nullptr;
    if (screens) {
        int i = 0;
        for (const Json& sj : *screens) {
            if (i >= kMaxScreens)
                break;
            Screen s = screenFromJson(sj, i, c.width, c.height);
            s.id = ++i;
            o.screens.push_back(std::move(s));
        }
        o.nextScreenId = i + 1;
        o.nextSliceUid = getInt(*oj, "nextSliceUid", 1, 1, 1 << 30);
        ensureSliceUids(o);
    } else {
        o = OutputSetup::createDefault(c.width, c.height, {});
    }

    if (const Json* pj = getObj(root, "pixelMap"))
        c.pixelMap = pixelMapFromJson(*pj, c.width, c.height);
    else
        pixelmap::syncInput(c.pixelMap, c.width, c.height);

    if (const Json* kj = getArr(root, "keys")) {
        const nlohmann::json plain = nlohmann::json::parse(kj->dump(), nullptr, false);
        std::vector<std::string> keyWarnings;
        c.keys = control::bindingsFromJson(plain, &keyWarnings);
        for (std::string& w : keyWarnings)
            res.warnings.push_back(std::move(w));
        control::stampLayerIds(c.keys, c);
    }

    if (const Json* sj = getObj(root, "sequences")) {
        const nlohmann::json plain = nlohmann::json::parse(sj->dump(), nullptr, false);
        c.sequences = seq::conjuntoDeJson(plain, &res.warnings);
        if (plain.is_object() && plain.contains("envelopes") && plain["envelopes"].is_array()) {
            for (const nlohmann::json& ej : plain["envelopes"]) {
                if (static_cast<int>(c.envelopes.size()) >= seq::kMaxEnvelopes)
                    break;
                c.envelopes.push_back(seq::envelopeDeJson(ej));
            }
        }
    }

    if (const Json* dj = getObj(root, "dmx")) {
        const nlohmann::json plain = nlohmann::json::parse(dj->dump(), nullptr, false);
        c.dmx = dmx::configFromJson(plain, &res.warnings);
        std::vector<int> ids;
        for (const Layer& l : c.layers)
            ids.push_back(l.id);
        dmx::stampLayerIds(c.dmx, ids);
    }

    comp = std::move(c);
    output = std::move(o);
    res.ok = true;
    return res;
}

bool saveFile(const fs::path& file, const std::string& text, std::string* error)
{
    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg;
        return false;
    };

    std::error_code ec;
    if (file.has_parent_path())
        fs::create_directories(file.parent_path(), ec);

    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return fail("não foi possível criar o arquivo temporário em " + paths::toUtf8(tmp.parent_path()));
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return fail("falha ao gravar o arquivo (disco cheio?)");
        }
    }

#ifndef _WIN32
    fs::rename(tmp, file, ec);
    if (ec) {
        const std::string detail = ec.message();
        fs::remove(tmp, ec);
        return fail("não foi possível substituir " + paths::toUtf8(file.filename()) + " (" + detail + ")");
    }
    return true;
#else
    bool ok = false;
    DWORD code = 0;
    if (fs::exists(file, ec)) {
        ok = ReplaceFileW(file.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0;
        if (!ok)
            code = GetLastError();
    }
    if (!ok) {
        ok = MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok && code == 0)
            code = GetLastError();
    }
    if (!ok) {
        fs::remove(tmp, ec);
        return fail("não foi possível substituir " + paths::toUtf8(file.filename()) + " (erro do Windows "
                    + std::to_string(code) + ")");
    }
    return true;
#endif
}

LoadResult loadFile(const fs::path& file, Composition& comp, OutputSetup& output)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        LoadResult r;
        r.error = "não foi possível ler " + paths::toUtf8(file);
        return r;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(text, file.parent_path(), comp, output);
}

bool relinkClip(Clip& clip, const fs::path& file)
{
    const media::MediaKind kind = media::classify(file);
    if (kind == media::MediaKind::Unknown || clip.type != Clip::Type::File)
        return false;
    clip.path = file;
    clip.kind = kind;
    clip.name = paths::toUtf8(file.filename());
    clip.missing = false;
    if (media::isRemoteKind(kind))
        clip.params.fillDefaults(clip.paramDescs());
    clip.setLoop(clip.loop);
    return true;
}

int relinkMissingInFolder(Composition& comp, const fs::path& folder)
{
    int found = 0;
    std::error_code ec;
    for (Deck& deck : comp.decks)
        for (auto& row : deck.clips)
            for (Clip& clip : row) {
                if (!clip.missing || clip.type != Clip::Type::File)
                    continue;
                const fs::path candidate = folder / clip.path.filename();
                if (fs::is_regular_file(candidate, ec) && relinkClip(clip, candidate))
                    ++found;
            }
    return found;
}

}
