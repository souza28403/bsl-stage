#include "core/Composition.h"

#include "core/Actions.h"
#include "app/Log.h"
#include "core/Generators.h"
#include "app/Paths.h"
#include "media/DocInfo.h"
#include "media/ImageSource.h"
#include "media/PdfSource.h"
#include "media/WebSource.h"
#include "media/VideoSource.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace bsl {

namespace {
constexpr int kNewDeckColumns = 8;
}

namespace {

constexpr const char* kParamEnvelope = "envelope";

template <typename Fn>
void paraCadaConjuntoDeParametros(Composition& comp, Fn&& fn)
{
    auto cadeia = [&fn](fx::EffectChain& chain) {
        for (fx::EffectInstance& inst : chain.items)
            fn(inst.params, &inst);
    };
    cadeia(comp.masterEffects);
    for (Layer& layer : comp.layers)
        cadeia(layer.effects);
    for (Deck& deck : comp.decks)
        for (auto& linha : deck.clips)
            for (Clip& clip : linha) {
                cadeia(clip.effects);
                fn(clip.params, nullptr);
            }
}

}

std::vector<int> Composition::envelopesUsados() const
{
    std::set<int> ids;
    auto ler = [&ids](const media::ParamValues& params) {
        const media::Json* v = params.find(kParamEnvelope);
        if (v && v->is_number_integer() && v->get<int>() > 0)
            ids.insert(v->get<int>());
    };
    for (const fx::EffectInstance& inst : masterEffects.items)
        ler(inst.params);
    for (const Layer& layer : layers)
        for (const fx::EffectInstance& inst : layer.effects.items)
            ler(inst.params);
    for (const Deck& deck : decks)
        for (const auto& linha : deck.clips)
            for (const Clip& clip : linha) {
                for (const fx::EffectInstance& inst : clip.effects.items)
                    ler(inst.params);
                ler(clip.params);
            }
    return std::vector<int>(ids.begin(), ids.end());
}

void Composition::remapearEnvelopes(const std::map<int, int>& mapa)
{
    if (mapa.empty())
        return;
    paraCadaConjuntoDeParametros(*this, [&mapa](media::ParamValues& params, fx::EffectInstance* inst) {
        const media::Json* v = params.find(kParamEnvelope);
        if (!v || !v->is_number_integer())
            return;
        const auto it = mapa.find(v->get<int>());
        if (it == mapa.end() || it->second == v->get<int>())
            return;
        if (inst)
            inst->setParam(kParamEnvelope, media::Json(it->second));
        else
            params.set(kParamEnvelope, media::Json(it->second));
    });
}

int Deck::clipCount() const
{
    int n = 0;
    for (const auto& row : clips)
        for (const Clip& c : row)
            n += c.empty() ? 0 : 1;
    return n;
}

Composition Composition::createDefault(int layerCount, int columns)
{
    Composition comp;
    Deck deck;
    deck.name = "Deck 1";
    for (int i = 0; i < layerCount; ++i) {
        Layer layer;
        layer.name = "Camada " + std::to_string(i + 1);
        comp.layers.push_back(std::move(layer));
        auto& row = deck.clips.emplace_back();
        row.resize(static_cast<size_t>(columns));
    }
    comp.decks.push_back(std::move(deck));
    comp.ensureLayerIds();
    return comp;
}

int Composition::findLayerById(int id) const
{
    if (id <= 0)
        return -1;
    for (int i = 0; i < layerCount(); ++i)
        if (layers[static_cast<size_t>(i)].id == id)
            return i;
    return -1;
}

void Composition::ensureLayerIds()
{
    std::set<int> used;
    for (Layer& l : layers) {
        if (l.id > 0 && used.insert(l.id).second)
            nextLayerId = std::max(nextLayerId, l.id + 1);
        else
            l.id = 0;
    }
    for (Layer& l : layers)
        if (l.id == 0)
            l.id = nextLayerId++;
}

void Composition::addLayer()
{
    Layer layer;
    layer.name = "Camada " + std::to_string(layers.size() + 1);
    layer.id = nextLayerId++;
    layers.push_back(std::move(layer));
    for (Deck& deck : decks) {
        const size_t columns = static_cast<size_t>(deck.columnCount());
        deck.clips.emplace_back().resize(columns);
    }
}

int Composition::addLayerForSlice(Slice& slice)
{
    addLayer();
    Layer& layer = layers.back();
    if (!slice.name.empty())
        layer.name = slice.name;
    layer.sliceOnly = true;
    slice.source = kSliceFromLayer;
    slice.layerId = layer.id;
    return layerCount() - 1;
}

void Composition::addColumn()
{
    if (!validDeck(currentDeck))
        return;
    for (auto& row : decks[static_cast<size_t>(currentDeck)].clips)
        row.emplace_back();
}

int Composition::addDeck()
{
    Deck deck;
    deck.name = "Deck " + std::to_string(decks.size() + 1);
    deck.clips.resize(layers.size());
    for (auto& row : deck.clips)
        row.resize(kNewDeckColumns);
    decks.push_back(std::move(deck));
    return static_cast<int>(decks.size()) - 1;
}

void Composition::removeDeck(int index)
{
    if (!validDeck(index) || deckCount() <= 1)
        return;
    for (int l = 0; l < layerCount(); ++l)
        if (layers[static_cast<size_t>(l)].activeDeck == index)
            closeActive(l);
    decks.erase(decks.begin() + index);
    for (Layer& l : layers)
        if (l.activeDeck > index)
            --l.activeDeck;
    if (currentDeck > index)
        --currentDeck;
    if (currentDeck >= deckCount())
        currentDeck = deckCount() - 1;
}

void Composition::closeActive(int layer)
{
    if (layer < 0 || layer >= layerCount())
        return;
    Layer& l = layers[static_cast<size_t>(layer)];
    if (validCell(l.activeDeck, layer, l.activeColumn)) {
        Clip& c = clipAt(l.activeDeck, layer, l.activeColumn);
        c.source.reset();
        c.resumeSeconds = 0.0;
    }
    l.activeColumn = -1;
    l.activeDeck = -1;
}

void Composition::deactivateLayer(int layer)
{
    if (layer < 0 || layer >= layerCount())
        return;
    Layer& l = layers[static_cast<size_t>(layer)];
    if (validCell(l.activeDeck, layer, l.activeColumn)) {
        Clip& c = clipAt(l.activeDeck, layer, l.activeColumn);
        if (c.source && c.replay == ReplayMode::Resume) {
            if (c.keepsAliveInBackground() && c.source->isOpen()) {
                c.source->pause();
                c.parked = std::move(c.source);
                c.parkedStamp = ++parkCounter_;
                c.parkedAccum = 0.0;
                c.resumeSeconds = 0.0;
            } else if (c.type == Clip::Type::File && c.kind == media::MediaKind::Video) {
                c.resumeSeconds = c.source->position();
                c.source.reset();
            } else {
                c.source.reset();
            }
        } else {
            c.source.reset();
            c.resumeSeconds = 0.0;
        }
    }
    l.activeColumn = -1;
    l.activeDeck = -1;
    trimBackground();
}

void Composition::stopLayer(int layer)
{
    if (layer < 0 || layer >= layerCount())
        return;
    closeActive(layer);
    for (Deck& deck : decks)
        for (Clip& c : deck.clips[static_cast<size_t>(layer)]) {
            c.parked.reset();
            c.resumeSeconds = 0.0;
        }
}

void Composition::stopAll()
{
    for (int i = 0; i < layerCount(); ++i)
        stopLayer(i);
    paused = false;
}

int Composition::backgroundCount() const
{
    int n = 0;
    for (const Deck& deck : decks)
        for (const auto& row : deck.clips)
            for (const Clip& c : row)
                n += c.parked ? 1 : 0;
    return n;
}

void Composition::trimBackground()
{
    const int limit = std::max(0, maxBackgroundSources);
    int count = backgroundCount();
    while (count > limit) {
        Clip* oldest = nullptr;
        for (Deck& deck : decks)
            for (auto& row : deck.clips)
                for (Clip& c : row)
                    if (c.parked && (!oldest || c.parkedStamp < oldest->parkedStamp))
                        oldest = &c;
        if (!oldest)
            break;
        log::warn("Limite de fontes em segundo plano (" + std::to_string(limit) + "): \"" + oldest->name +
                  "\" foi fechada; ao disparar de novo ela abre do início.");
        oldest->parked.reset();
        --count;
    }
}

void Composition::updateBackground(double dt)
{
    constexpr double kPeriod = 0.25;
    for (Deck& deck : decks)
        for (auto& row : deck.clips)
            for (Clip& c : row) {
                if (!c.parked)
                    continue;
                c.parkedAccum += dt;
                if (c.parkedAccum < kPeriod)
                    continue;
                const double elapsed = c.parkedAccum;
                c.parkedAccum = 0.0;
                c.parked->setCanvasSize(width, height);
                c.parked->update(elapsed);
                c.parked->pollParamChanges(c.params);
            }
}

void Composition::pauseAll()
{
    for (int i = 0; i < layerCount(); ++i)
        if (Clip* c = activeClip(i); c && c->isOpen())
            c->setPlaying(false);
    paused = true;
}

void Composition::resumeAll()
{
    for (int i = 0; i < layerCount(); ++i)
        if (Clip* c = activeClip(i); c && c->isOpen())
            c->setPlaying(true);
    paused = false;
}

int Composition::pageAction(PageAction action, int layer)
{
    int hit = 0;
    for (int i = 0; i < layerCount(); ++i) {
        if (layer >= 0 && i != layer)
            continue;
        Clip* c = activeClip(i);
        if (!c || !c->isOpen() || !c->hasPageNav())
            continue;
        auto* pdf = dynamic_cast<media::PdfSource*>(c->source.get());
        if (!pdf)
            continue;
        switch (action) {
        case PageAction::Next: pdf->nextPage(); break;
        case PageAction::Prev: pdf->prevPage(); break;
        case PageAction::First: pdf->firstPage(); break;
        case PageAction::Last: pdf->lastPage(); break;
        case PageAction::Reload: pdf->reloadDocument(); break;
        }
        if (action != PageAction::Reload)
            c->setParam("page", pdf->page());
        ++hit;
    }
    return hit;
}

namespace {

int docNext(Composition& c) { return c.pageAction(Composition::PageAction::Next); }
int docPrev(Composition& c) { return c.pageAction(Composition::PageAction::Prev); }
int docFirst(Composition& c) { return c.pageAction(Composition::PageAction::First); }
int docLast(Composition& c) { return c.pageAction(Composition::PageAction::Last); }
int docReload(Composition& c) { return c.pageAction(Composition::PageAction::Reload); }

const ActionDef kActions[] = {
    {"doc.next", "Próximo slide", "Documentos", &docNext},
    {"doc.prev", "Slide anterior", "Documentos", &docPrev},
    {"doc.first", "Voltar ao início (slide inicial)", "Documentos", &docFirst},
    {"doc.last", "Último slide", "Documentos", &docLast},
    {"doc.reload", "Recarregar documento", "Documentos", &docReload},
};

}

std::span<const ActionDef> allActions()
{
    return kActions;
}

const ActionDef* findAction(const std::string& id)
{
    for (const ActionDef& a : kActions)
        if (id == a.id)
            return &a;
    return nullptr;
}

bool runAction(Composition& comp, const std::string& id)
{
    const ActionDef* a = findAction(id);
    return a && a->run(comp) > 0;
}

std::unique_ptr<media::ISource> Composition::openSource(const Clip& clipToOpen) const
{
    std::unique_ptr<media::ISource> src;
    switch (clipToOpen.type) {
    case Clip::Type::Generator: {
        const GeneratorInfo* info = clipToOpen.generatorInfo();
        if (!info) {
            log::warn("Gerador desconhecido: \"" + clipToOpen.generatorId + "\"");
            return nullptr;
        }
        src = info->create(width, height);
        src->setParams(clipToOpen.params);
        break;
    }
    case Clip::Type::File:
        if (clipToOpen.kind == media::MediaKind::Video) {
            src = std::make_unique<media::VideoSource>(decoderOptions);
        } else if (media::isPagedKind(clipToOpen.kind)) {
            src = std::make_unique<media::PdfSource>(width, height);
            src->setParams(clipToOpen.params);
        } else if (clipToOpen.kind == media::MediaKind::Web) {
            src = std::make_unique<media::WebSource>(width, height);
            src->setParams(clipToOpen.params);
        } else {
            src = std::make_unique<media::ImageSource>();
        }
        break;
    case Clip::Type::Empty:
        return nullptr;
    }

    std::string error;
    if (!src->open(clipToOpen.path, &error)) {
        log::warn("Não foi possível abrir \"" + clipToOpen.name + "\"" + (error.empty() ? "" : ": " + error));
        return nullptr;
    }
    src->setLoopMode(clipToOpen.loop);
    src->setSpeed(clipToOpen.speed);
    return src;
}

bool Composition::trigger(int layer, int column)
{
    if (!validCell(layer, column))
        return false;
    Clip& c = clip(layer, column);
    if (c.empty())
        return false;
    if (c.missing) {
        std::error_code ec;
        if (c.type == Clip::Type::File && std::filesystem::exists(c.path, ec)) {
            c.missing = false;
        } else {
            log::warn(c.missingPlugin ? "Plugin ausente: \"" + c.generatorId + "\" não existe mais nesta versão."
                                      : "Arquivo não encontrado: \"" + c.name + "\"");
            return false;
        }
    }

    Layer& l = layers[static_cast<size_t>(layer)];
    if (l.activeDeck == currentDeck && l.activeColumn == column && c.isOpen()) {
        c.restart();
        if (paused)
            resumeAll();
        return true;
    }

    std::unique_ptr<media::ISource> resumed;
    if (c.parked) {
        if (c.replay == ReplayMode::Resume && c.parked->isOpen())
            resumed = std::move(c.parked);
        c.parked.reset();
        c.parkedStamp = 0;
    }

    deactivateLayer(layer);

    if (resumed)
        c.source = std::move(resumed);
    if (!c.source) {
        std::unique_ptr<media::ISource> src = openSource(c);
        if (!src)
            return false;
        c.source = std::move(src);
        if (c.replay == ReplayMode::Resume && c.resumeSeconds > 0.0 && c.type == Clip::Type::File &&
            c.kind == media::MediaKind::Video && c.resumeSeconds < c.source->duration() - 0.05)
            c.source->seek(c.resumeSeconds);
    }
    c.resumeSeconds = 0.0;
    c.playing = true;
    c.source->play();
    l.activeDeck = currentDeck;
    l.activeColumn = column;
    if (paused)
        resumeAll();
    return true;
}

void Composition::triggerColumn(int column)
{
    for (int layer = 0; layer < layerCount(); ++layer)
        trigger(layer, column);
}

void Composition::setClip(int layer, int column, Clip clipValue)
{
    if (!validCell(layer, column))
        return;
    clearClip(layer, column);
    clip(layer, column) = std::move(clipValue);
}

void Composition::clearClip(int layer, int column)
{
    if (!validCell(layer, column))
        return;
    if (isActiveCell(currentDeck, layer, column))
        stopLayer(layer);
    clip(layer, column) = Clip{};
}

int Composition::expandPages(int layer, int column)
{
    if (!validCell(layer, column))
        return 0;
    const Clip& base = clip(layer, column);
    const int pages = media::docPageCount(base.path);
    if (!base.isPaged() || pages < 1 || base.missing)
        return 0;
    const std::filesystem::path file = base.path;
    const media::ParamValues baseParams = base.params;
    const FitMode fit = base.fit;
    const fx::EffectChain baseEffects = base.effects;
    const std::string baseName = paths::toUtf8(file.filename());

    int created = 0;
    for (int i = 1; i <= pages; ++i) {
        Clip c = Clip::fromFile(file);
        if (c.empty())
            break;
        c.params = baseParams;
        c.params.set("page", i);
        c.params.set("autoMode", "off");
        c.params.set("label", baseName + " (p. " + std::to_string(i) + ")");
        c.fit = fit;
        c.effects = baseEffects;
        c.refreshName();
        const int col = column + i - 1;
        while (col >= columnCount())
            addColumn();
        setClip(layer, col, std::move(c));
        ++created;
    }
    log::info("Páginas expandidas: " + std::to_string(created) + " clip(s) a partir da coluna " + std::to_string(column + 1));
    return created;
}

void Composition::swapClips(int layerA, int columnA, int layerB, int columnB)
{
    if (!validCell(layerA, columnA) || !validCell(layerB, columnB))
        return;
    if (layerA == layerB && columnA == columnB)
        return;

    const int deck = currentDeck;
    const bool activeA = isActiveCell(deck, layerA, columnA) && clip(layerA, columnA).isOpen();
    const bool activeB = isActiveCell(deck, layerB, columnB) && clip(layerB, columnB).isOpen();

    if (activeA) {
        layers[static_cast<size_t>(layerA)].activeColumn = -1;
        layers[static_cast<size_t>(layerA)].activeDeck = -1;
    }
    if (activeB) {
        layers[static_cast<size_t>(layerB)].activeColumn = -1;
        layers[static_cast<size_t>(layerB)].activeDeck = -1;
    }

    std::swap(clip(layerA, columnA), clip(layerB, columnB));

    auto place = [&](int layer, int column) {
        deactivateLayer(layer);
        Layer& l = layers[static_cast<size_t>(layer)];
        l.activeDeck = deck;
        l.activeColumn = column;
    };
    if (activeA)
        place(layerB, columnB);
    if (activeB)
        place(layerA, columnA);
}

void Composition::update(double dt)
{
    if (!paused && bpm > 0.0f)
        beat += dt * static_cast<double>(bpm) / 60.0;

    for (int i = 0; i < layerCount(); ++i) {
        Clip* c = activeClip(i);
        if (!c || !c->isOpen())
            continue;
        c->source->setCanvasSize(width, height);
        c->source->setTempo(beat, bpm);
        c->source->update(dt);
        c->source->pollParamChanges(c->params);
        c->playing = c->source->isPlaying();
    }
    updateBackground(dt);
    trimBackground();
}

bool Composition::anyActive() const
{
    for (int i = 0; i < layerCount(); ++i) {
        const Clip* c = activeClip(i);
        if (c && c->isOpen())
            return true;
    }
    return false;
}

Clip* Composition::activeClip(int layer)
{
    if (layer < 0 || layer >= layerCount())
        return nullptr;
    const Layer& l = layers[static_cast<size_t>(layer)];
    if (!validCell(l.activeDeck, layer, l.activeColumn))
        return nullptr;
    return &clipAt(l.activeDeck, layer, l.activeColumn);
}

const Clip* Composition::activeClip(int layer) const
{
    return const_cast<Composition*>(this)->activeClip(layer);
}

LayerTransform* Composition::transformRef(int layer)
{
    if (layer < 0 || layer >= layerCount())
        return nullptr;
    if (Clip* c = activeClip(layer); c && c->ownTransform)
        return &c->transform;
    return &layers[static_cast<size_t>(layer)].transform;
}

const LayerTransform* Composition::transformRef(int layer) const
{
    return const_cast<Composition*>(this)->transformRef(layer);
}

bool Composition::resetTransform(int layer)
{
    LayerTransform* t = transformRef(layer);
    if (!t)
        return false;
    const LayerTransform before = *t;
    *t = LayerTransform{};
    return !(before == *t);
}

int Composition::layerAtPoint(float px, float py) const
{
    bool anySolo = false;
    for (const Layer& l : layers)
        anySolo = anySolo || (l.solo && !l.sliceOnly);
    for (int i = layerCount() - 1; i >= 0; --i) {
        const Layer& l = layers[static_cast<size_t>(i)];
        if (l.sliceOnly || l.bypass || (anySolo && !l.solo) || l.opacity <= 0.0f)
            continue;
        const Clip* c = activeClip(i);
        if (!c || !c->isOpen() || c->source->texture() == 0)
            continue;
        const LayerTransform& t = c->ownTransform ? c->transform : l.transform;
        if (!t.contains(px, py, width, height))
            continue;
        float lx = 0.0f, ly = 0.0f;
        t.toLocal(px, py, width, height, lx, ly);
        const float u = lx / t.width(width), v = ly / t.height(height);
        float sx = 1.0f, sy = 1.0f;
        fitFraction(c->fit, width, height, c->source->width(), c->source->height(), sx, sy);
        if (std::abs(u - 0.5f) > sx * 0.5f || std::abs(v - 0.5f) > sy * 0.5f)
            continue;
        return i;
    }
    return -1;
}

int Composition::applyTransformPreset(int layer, int preset)
{
    if (preset < 0 || preset >= kTransformPresetCount)
        return 0;
    const auto kind = static_cast<TransformPreset>(preset);

    if (kind == TransformPreset::Grid2x2 || kind == TransformPreset::Grid3x3) {
        const int n = kind == TransformPreset::Grid2x2 ? 2 : 3;
        int changed = 0;
        for (int i = 0; i < std::min(layerCount(), n * n); ++i) {
            LayerTransform* t = transformRef(i);
            const LayerTransform next = gridCellTransform(n, n, i % n, i / n, width, height);
            changed += (*t == next) ? 0 : 1;
            *t = next;
        }
        return changed;
    }

    LayerTransform* t = transformRef(layer);
    if (!t)
        return 0;
    LayerTransform next;
    switch (kind) {
    case TransformPreset::Fit:
    case TransformPreset::Fill: {
        const Clip* c = activeClip(layer);
        if (c && c->isOpen() && c->source->width() > 0 && c->source->height() > 0) {
            const int sw = c->source->width(), sh = c->source->height();
            float cx0 = 1.0f, cy0 = 1.0f, cxT = 1.0f, cyT = 1.0f;
            fitFraction(c->fit, width, height, sw, sh, cx0, cy0);
            fitFraction(kind == TransformPreset::Fit ? FitMode::Fit : FitMode::Fill, width, height, sw, sh, cxT, cyT);
            const float bw = static_cast<float>(width) * cxT / cx0, bh = static_cast<float>(height) * cyT / cy0;
            next = LayerTransform::rect((static_cast<float>(width) - bw) * 0.5f, (static_cast<float>(height) - bh) * 0.5f, bw, bh)
                       .settled(width, height);
        }
        break;
    }
    case TransformPreset::LeftHalf: next = gridCellTransform(2, 1, 0, 0, width, height); break;
    case TransformPreset::RightHalf: next = gridCellTransform(2, 1, 1, 0, width, height); break;
    case TransformPreset::Quad1: next = gridCellTransform(2, 2, 0, 0, width, height); break;
    case TransformPreset::Quad2: next = gridCellTransform(2, 2, 1, 0, width, height); break;
    case TransformPreset::Quad3: next = gridCellTransform(2, 2, 0, 1, width, height); break;
    case TransformPreset::Quad4: next = gridCellTransform(2, 2, 1, 1, width, height); break;
    default: break;
    }
    if (*t == next)
        return 0;
    *t = next;
    return 1;
}

}
