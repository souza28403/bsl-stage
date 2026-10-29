#include "ffgl/FfglSource.h"

#include "audio/AudioAnalyzer.h"
#include "ffgl/FfglCatalog.h"

#include <cmath>
#include <map>
#include <mutex>

namespace bsl::ffgl {

FfglSource::FfglSource(std::string typeId, int width, int height)
    : typeId_(std::move(typeId)), width_(width), height_(height)
{
    const EntryPtr entry = Catalog::instance().find(typeId_);
    if (!entry) {
        loadError_ = "Plugin FFGL não encontrado: " + typeId_ + " (veja Ver > Plugins)";
    } else if (entry->info.type == kTypeMixer) {
        loadError_ = "Mixers FFGL ainda não são suportados.";
    } else {
        descs_ = entry->map.descs;
        instance_ = std::make_unique<FfglInstance>(entry);
    }
}

FfglSource::~FfglSource()
{
    close();
}

bool FfglSource::open(const std::filesystem::path& /*path*/, std::string* error)
{
    close();
    if (!instance_) {
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
    instance_->setParams(currentParams());
    update(0.0); // primeiro quadro ja na abertura
    return true;
}

void FfglSource::close()
{
    fbo_.reset();
    pool_.clear();
}

void FfglSource::update(double dt)
{
    if (!fbo_ || !instance_)
        return;
    const double step = clock_.playing ? dt * clock_.speed : 0.0;
    time_ += step;
    fx::EffectContext ctx;
    ctx.time = time_;
    ctx.dt = step;
    ctx.beat = beat_;
    ctx.bpm = bpm_;
    ctx.beatPhase = static_cast<float>(beat_ - std::floor(beat_));
    ctx.barPhase = static_cast<float>(std::fmod(beat_, 4.0) / 4.0);
    ctx.canvasWidth = width_;
    ctx.canvasHeight = height_;
    ctx.frameIndex = frame_++;
    ctx.topOrigin = true; // texturas de fonte: linha 0 = topo
    ctx.pool = &pool_;
    ctx.audio = audio::AudioAnalyzer::instance().snapshot();
    instance_->process(ctx, 0, width_, height_, *fbo_);
    pool_.endFrame();
}

void FfglSource::onParamsChanged()
{
    lastEdit_ = std::chrono::steady_clock::now();
    if (instance_)
        instance_->setParams(currentParams());
}

bool FfglSource::pollParamChanges(media::ParamValues& values)
{
    using namespace std::chrono;
    if (!instance_ || !fbo_)
        return false;
    if (steady_clock::now() - lastEdit_ < milliseconds(400)) // nao briga com o arrastar de um controle
        return false;
    if (!instance_->pollParamChanges(values))
        return false;
    values_ = values; // a fonte ja sabe desses valores
    return true;
}

media::SourceStatus FfglSource::status() const
{
    if (!instance_)
        return {media::SourceStatus::Level::Warning, loadError_};
    return instance_->status();
}

namespace {

struct Node
{
    std::string id;
    std::string name;
    GeneratorInfo info;
};

// Nome mostrado quando o plugin nao esta no catalogo: o arquivo do typeId sem a extensao
std::string nameFromId(const std::string& typeId)
{
    std::string pluginId, file;
    if (!splitTypeId(typeId, pluginId, file))
        return typeId;
    const size_t dot = file.rfind('.');
    return dot == std::string::npos || dot == 0 ? file : file.substr(0, dot);
}

} // namespace

const GeneratorInfo* FfglSource::generatorInfo(const std::string& id)
{
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Node>> nodes;
    std::lock_guard lock(mutex);
    if (const auto it = nodes.find(id); it != nodes.end())
        return &it->second->info;

    auto node = std::make_unique<Node>();
    node->id = id;
    node->name = nameFromId(id);
    node->info.id = node->id.c_str();
    node->info.name = node->name.c_str();
    node->info.live = false;
    node->info.needsSetup = false;
    node->info.describe = [id](const media::ParamValues&) {
        const EntryPtr e = Catalog::instance().find(id);
        return e ? e->map.descs : std::vector<media::ParamDesc>{};
    };
    node->info.create = [id](int w, int h) -> std::unique_ptr<media::ISource> {
        return std::make_unique<FfglSource>(id, w, h);
    };
    node->info.label = [id](const media::ParamValues&) {
        const EntryPtr e = Catalog::instance().find(id);
        return e ? pluginDisplayName(e->info) : nameFromId(id);
    };
    const GeneratorInfo* result = &node->info;
    nodes.emplace(id, std::move(node));
    return result;
}

} // namespace bsl::ffgl
