#include "core/Composition.h"

#include "app/Log.h"
#include "core/Generators.h"
#include "app/Paths.h"
#include "media/DocInfo.h"
#include "media/ImageSource.h"
#include "media/PdfSource.h"
#include "media/WebSource.h"
#include "media/VideoSource.h"

#include <string>
#include <utility>

namespace bsl {

namespace {
constexpr int kNewDeckColumns = 8;
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
    return comp;
}

void Composition::addLayer()
{
    Layer layer;
    layer.name = "Camada " + std::to_string(layers.size() + 1);
    layers.push_back(std::move(layer));
    for (Deck& deck : decks) {
        const size_t columns = static_cast<size_t>(deck.columnCount());
        deck.clips.emplace_back().resize(columns);
    }
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
            stopLayer(l);
    decks.erase(decks.begin() + index);
    for (Layer& l : layers)
        if (l.activeDeck > index)
            --l.activeDeck;
    if (currentDeck > index)
        --currentDeck;
    if (currentDeck >= deckCount())
        currentDeck = deckCount() - 1;
}

void Composition::stopLayer(int layer)
{
    if (layer < 0 || layer >= layerCount())
        return;
    Layer& l = layers[static_cast<size_t>(layer)];
    if (validCell(l.activeDeck, layer, l.activeColumn))
        clipAt(l.activeDeck, layer, l.activeColumn).source.reset();
    l.activeColumn = -1;
    l.activeDeck = -1;
}

void Composition::stopAll()
{
    for (int i = 0; i < layerCount(); ++i)
        stopLayer(i);
    paused = false;
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
            c.missing = false; // o arquivo voltou ao lugar
        } else {
            log::warn("Arquivo não encontrado: \"" + c.name + "\"");
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

    stopLayer(layer);

    std::unique_ptr<media::ISource> src = openSource(c);
    if (!src)
        return false;

    c.source = std::move(src);
    c.playing = true; // disparar sempre comeca tocando
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
    // Copia o que vale antes de mexer na grade (a referencia acima aponta para a celula que sera substituida)
    const std::filesystem::path file = base.path;
    const media::ParamValues baseParams = base.params;
    const FitMode fit = base.fit;
    const fx::EffectChain baseEffects = base.effects; // cada pagina herda os efeitos do clip original
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

    // Quem estava tocando perde o marcador na posicao antiga (a fonte vai junto com o clip)
    if (activeA) {
        layers[static_cast<size_t>(layerA)].activeColumn = -1;
        layers[static_cast<size_t>(layerA)].activeDeck = -1;
    }
    if (activeB) {
        layers[static_cast<size_t>(layerB)].activeColumn = -1;
        layers[static_cast<size_t>(layerB)].activeDeck = -1;
    }

    std::swap(clip(layerA, columnA), clip(layerB, columnB));

    // O clip de A agora esta em B (e vice-versa): reassume o posto de ativo da camada de destino,
    // fechando o que a camada tocava antes (uma camada toca um clip por vez).
    auto place = [&](int layer, int column) {
        stopLayer(layer);
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
        c->source->pollParamChanges(c->params); // fonte com interface propria (plugin FFGL) que mudou parametros
        c->playing = c->source->isPlaying(); // "Uma vez" termina e pausa sozinho
    }
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

} // namespace bsl
