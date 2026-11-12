#pragma once

#include "control/KeyBinding.h"
#include "core/Clip.h"
#include "core/Layer.h"
#include "dmx/DmxMap.h"
#include "media/VideoDecoder.h"
#include "output/PixelMap.h"
#include "seq/Envelope.h"
#include "seq/SeqModel.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bsl {

struct Deck
{
    std::string name;
    std::vector<std::vector<Clip>> clips;

    int columnCount() const { return clips.empty() ? 0 : static_cast<int>(clips.front().size()); }
    int clipCount() const;
};

struct Composition
{
    int width = 1920;
    int height = 1080;
    float fps = 60.0f;
    float bpm = 120.0f;

    float masterOpacity = 1.0f;
    bool masterBypass = false;
    bool blackout = false;
    bool outputOff() const { return masterBypass || blackout; }
    fx::EffectChain masterEffects;

    bool paused = false;
    double beat = 0.0;

    std::vector<Layer> layers;
    int nextLayerId = 1;
    std::vector<Deck> decks;
    int currentDeck = 0;

    pixelmap::Config pixelMap;

    seq::SequenceSet sequences;
    std::vector<seq::Envelope> envelopes;

    control::KeyBindingSet keys = control::defaultProjectKeys();

    dmx::Config dmx;

    media::VideoDecoderOptions decoderOptions;

    static Composition createDefault(int layerCount = 3, int columns = 8);

    int layerCount() const { return static_cast<int>(layers.size()); }
    int deckCount() const { return static_cast<int>(decks.size()); }
    bool validDeck(int deck) const { return deck >= 0 && deck < deckCount(); }
    int columnCount() const
    {
        return validDeck(currentDeck) ? decks[static_cast<size_t>(currentDeck)].columnCount() : 0;
    }
    bool validCell(int deck, int layer, int column) const
    {
        return validDeck(deck) && layer >= 0 && layer < layerCount() && column >= 0
               && column < decks[static_cast<size_t>(deck)].columnCount();
    }
    bool validCell(int layer, int column) const { return validCell(currentDeck, layer, column); }

    Clip& clipAt(int deck, int layer, int column)
    {
        return decks[static_cast<size_t>(deck)].clips[static_cast<size_t>(layer)][static_cast<size_t>(column)];
    }
    const Clip& clipAt(int deck, int layer, int column) const
    {
        return decks[static_cast<size_t>(deck)].clips[static_cast<size_t>(layer)][static_cast<size_t>(column)];
    }
    Clip& clip(int layer, int column) { return clipAt(currentDeck, layer, column); }
    const Clip& clip(int layer, int column) const { return clipAt(currentDeck, layer, column); }

    bool isActiveCell(int deck, int layer, int column) const
    {
        if (layer < 0 || layer >= layerCount())
            return false;
        const Layer& l = layers[static_cast<size_t>(layer)];
        return l.activeDeck == deck && l.activeColumn == column;
    }

    void addLayer();
    int addLayerForSlice(Slice& slice);
    int findLayerById(int id) const;
    void ensureLayerIds();
    void addColumn();
    int addDeck();
    void removeDeck(int index);

    std::unique_ptr<media::ISource> openSource(const Clip& clip) const;

    bool trigger(int layer, int column);
    void triggerColumn(int column);
    void stopLayer(int layer);
    void stopAll();

    int maxBackgroundSources = 8;
    int backgroundCount() const;
    void trimBackground();

    void pauseAll();
    void resumeAll();

    enum class PageAction { Next, Prev, First, Last, Reload };
    int pageAction(PageAction action, int layer = -1);

    void setClip(int layer, int column, Clip clip);
    void clearClip(int layer, int column);
    int expandPages(int layer, int column);
    void swapClips(int layerA, int columnA, int layerB, int columnB);

    std::vector<int> envelopesUsados() const;
    void remapearEnvelopes(const std::map<int, int>& mapa);

    void update(double dt);

    bool anyActive() const;
    Clip* activeClip(int layer);
    const Clip* activeClip(int layer) const;

    LayerTransform* transformRef(int layer);
    const LayerTransform* transformRef(int layer) const;
    bool resetTransform(int layer);
    int applyTransformPreset(int layer, int preset);
    int layerAtPoint(float px, float py) const;

private:
    void closeActive(int layer);
    void deactivateLayer(int layer);
    void updateBackground(double dt);
    std::uint64_t parkCounter_ = 0;
};

}
