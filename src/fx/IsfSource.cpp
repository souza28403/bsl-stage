#include "fx/IsfSource.h"

#include "audio/AudioAnalyzer.h"
#include "fx/IsfLibrary.h"

#include <cmath>
#include <map>
#include <mutex>

namespace bsl::fx {

IsfSource::IsfSource(std::string id, int width, int height) : id_(std::move(id)), width_(width), height_(height)
{
    seenVersion_ = IsfLibrary::instance().version();
    const auto entry = IsfLibrary::instance().find(id_);
    if (!entry) {
        loadError_ = "Shader ISF não encontrado: " + id_;
    } else if (!entry->doc) {
        loadError_ = entry->error;
    } else if (entry->isEffect) {
        loadError_ = "Este ISF é um efeito (tem inputImage), não um gerador.";
    } else {
        descs_ = isfParamDescs(*entry->doc);
        renderer_ = std::make_unique<IsfRenderer>(entry->doc, entry->file.parent_path());
    }
}

IsfSource::~IsfSource()
{
    close();
}

bool IsfSource::open(const std::filesystem::path& /*path*/, std::string* error)
{
    close();
    if (!renderer_) {
        if (error)
            *error = loadError_;
        return false;
    }
    try {
        fbo_ = std::make_unique<Fbo>(width_, height_);
    } catch (const std::exception& e) {
        fbo_.reset();
        if (error)
            *error = e.what();
        return false;
    }
    clock_ = media::ClipClock{};
    renderer_->reset();
    renderer_->setParams(currentParams());
    update(0.0); // primeiro quadro ja na abertura
    return true;
}

void IsfSource::close()
{
    fbo_.reset();
}

void IsfSource::refreshDoc()
{
    IsfLibrary& lib = IsfLibrary::instance();
    const std::uint64_t v = lib.version();
    if (v == seenVersion_)
        return;
    seenVersion_ = v;
    const auto entry = lib.find(id_);
    if (!entry || !entry->doc || entry->isEffect || (renderer_ && renderer_->docPtr() == entry->doc))
        return;
    descs_ = isfParamDescs(*entry->doc);
    renderer_ = std::make_unique<IsfRenderer>(entry->doc, entry->file.parent_path());
    renderer_->setParams(currentParams());
}

void IsfSource::update(double dt)
{
    if (fbo_)
        refreshDoc();
    if (!fbo_ || !renderer_)
        return;
    EffectContext ctx;
    ctx.dt = clock_.playing ? dt * clock_.speed : 0.0;
    ctx.beat = beat_;
    ctx.bpm = bpm_;
    ctx.beatPhase = static_cast<float>(beat_ - std::floor(beat_));
    ctx.barPhase = static_cast<float>(std::fmod(beat_, 4.0) / 4.0);
    ctx.canvasWidth = width_;
    ctx.canvasHeight = height_;
    ctx.frameIndex = frame_++;
    ctx.topOrigin = true; // texturas de fonte: linha 0 = topo
    ctx.audio = audio::AudioAnalyzer::instance().snapshot();
    renderer_->render(ctx, 0, 0, 0, width_, height_, *fbo_);
}

void IsfSource::onParamsChanged()
{
    if (renderer_)
        renderer_->setParams(currentParams());
}

media::SourceStatus IsfSource::status() const
{
    if (!renderer_)
        return {media::SourceStatus::Level::Warning, loadError_};
    if (!renderer_->error().empty())
        return {media::SourceStatus::Level::Warning, renderer_->error()};
    if (!renderer_->warnings().empty())
        return {media::SourceStatus::Level::Info, renderer_->warnings().front()};
    return {};
}

namespace {

struct Node
{
    std::string id;
    std::string name;
    GeneratorInfo info;
};

} // namespace

const GeneratorInfo* IsfSource::generatorInfo(const std::string& id)
{
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Node>> nodes;
    std::lock_guard lock(mutex);
    if (const auto it = nodes.find(id); it != nodes.end())
        return &it->second->info;

    auto node = std::make_unique<Node>();
    node->id = id;
    std::string name = id.substr(id.rfind('/') == std::string::npos ? 4 : id.rfind('/') + 1);
    if (name.size() > 3 && name.compare(name.size() - 3, 3, ".fs") == 0)
        name.erase(name.size() - 3);
    node->name = name;
    node->info.id = node->id.c_str();
    node->info.name = node->name.c_str();
    node->info.live = false;
    node->info.needsSetup = false;
    node->info.describe = [id](const media::ParamValues&) {
        const auto doc = IsfLibrary::instance().doc(id);
        return doc && !doc->isEffect() ? isfParamDescs(*doc) : std::vector<media::ParamDesc>{};
    };
    node->info.create = [id](int w, int h) -> std::unique_ptr<media::ISource> {
        return std::make_unique<IsfSource>(id, w, h);
    };
    node->info.label = [name](const media::ParamValues&) { return name; };
    const GeneratorInfo* result = &node->info;
    nodes.emplace(id, std::move(node));
    return result;
}

} // namespace bsl::fx
