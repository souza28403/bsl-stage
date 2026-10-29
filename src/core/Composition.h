#pragma once

#include "core/Clip.h"
#include "core/Layer.h"
#include "media/VideoDecoder.h"
#include "output/PixelMap.h"

#include <memory>
#include <string>
#include <vector>

namespace bsl {

// Uma grade de clips. As camadas sao da Composition; cada deck tem a sua grade.
struct Deck
{
    std::string name;
    std::vector<std::vector<Clip>> clips; // clips[layer][column]

    int columnCount() const { return clips.empty() ? 0 : static_cast<int>(clips.front().size()); }
    int clipCount() const;
};

struct Composition
{
    int width = 1920;
    int height = 1080;
    float fps = 60.0f;
    float bpm = 120.0f;

    // Master: fade do resultado final e bypass geral
    float masterOpacity = 1.0f;
    bool masterBypass = false;
    fx::EffectChain masterEffects; // efeitos do master (depois de todas as camadas, antes do fade)

    // Transporte global. O BPM por ora so alimenta o indicador de beat (sincronia de clips: Fase 5).
    bool paused = false;
    double beat = 0.0; // posicao em batidas desde o ultimo RESYNC

    // Camada 0 = fundo da composicao; a ultima e a do topo (a UI mostra na ordem inversa).
    std::vector<Layer> layers;
    std::vector<Deck> decks;
    int currentDeck = 0; // deck mostrado na grade; as operacoes de celula abaixo valem para ele

    // Pixel Map (modo "Pixel Map"): configuracao e paineis. Vai no projeto; nao mexe na composicao nem na saida.
    pixelmap::Config pixelMap;

    // Opcoes usadas ao abrir videos (o App as ajusta conforme o perfil de desempenho)
    media::VideoDecoderOptions decoderOptions;

    // 3 camadas x 8 colunas de clips vazios, num deck
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

    // A celula (deck, camada, coluna) e o clip tocando na camada?
    bool isActiveCell(int deck, int layer, int column) const
    {
        if (layer < 0 || layer >= layerCount())
            return false;
        const Layer& l = layers[static_cast<size_t>(layer)];
        return l.activeDeck == deck && l.activeColumn == column;
    }

    void addLayer();
    void addColumn();           // no deck atual
    int addDeck();              // devolve o indice
    void removeDeck(int index); // para o que tocava nele; sempre sobra ao menos um deck

    // Cria e abre a fonte do clip (nullptr + aviso no log se nao abrir)
    std::unique_ptr<media::ISource> openSource(const Clip& clip) const;

    // Dispara o clip do deck atual: fecha a fonte anterior da camada e abre a nova do inicio
    // (se ja era o clip ativo, so reinicia). false se a celula e vazia ou a fonte nao abriu.
    bool trigger(int layer, int column);
    void triggerColumn(int column);
    void stopLayer(int layer); // para e libera a fonte da camada
    void stopAll();            // para todas as camadas

    // Transporte global: so atinge as fontes ativas
    void pauseAll();
    void resumeAll();

    // Coloca um clip na celula do deck atual (para o que estava la)
    void setClip(int layer, int column, Clip clip);
    void clearClip(int layer, int column);
    // "Expandir páginas na linha": o clip paginado (PDF/Office) vira um clip por página nas células seguintes da mesma
    // camada, cada um fixo na sua página (sem avanço automático); o clicado vira a página 1. Células ocupadas à frente
    // são substituídas e colunas novas são acrescentadas se faltarem. Precisa do total de páginas (media/DocInfo).
    // Devolve o número de clips criados (0 se não deu).
    int expandPages(int layer, int column);
    // Troca o conteudo de duas celulas do deck atual. Clip tocando continua tocando na nova
    // posicao (a fonte aberta acompanha o clip); nada e reaberto.
    void swapClips(int layerA, int columnA, int layerB, int columnB);

    // Avanca so as fontes ativas e o relogio de beat
    void update(double dt);

    bool anyActive() const;
    Clip* activeClip(int layer);
    const Clip* activeClip(int layer) const;
};

} // namespace bsl
