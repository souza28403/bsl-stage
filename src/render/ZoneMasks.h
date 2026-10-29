#pragma once

// Máscaras de zonas de profundidade publicadas pelo efeito Z-Slice e lidas pelo OutputRenderer (roteamento dos slices).
//
// O Z-Slice de uma camada (ou do master) calcula, a cada quadro, uma textura RGBA8 com o peso de cada zona (R = Frente,
// G = Meio, B = Fundo, A = Zona 4), no espaço do canvas (linha 0 = base, como o FBO do Compositor; 0..1 cobre a imagem
// inteira, sem o fator da escala de prévia). O Compositor chama beginFrame() antes de renderizar; só vale o que foi
// publicado desde então: efeito em bypass, removido ou sem IA pronta deixa a fonte sem máscara e o slice mostra tudo.
//
// Tudo na thread de render (contexto GL atual); o mutex só protege a consulta da interface.

#include <glad/gl.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace bsl {

struct ZoneMask
{
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    int zones = 3; // zonas em uso (3 ou 4)
};

class ZoneMasks
{
public:
    static ZoneMasks& instance();

    // Começo de um quadro do Compositor: o que foi publicado antes deixa de valer até ser publicado de novo
    void beginFrame();
    // 'owner' identifica o efeito (uma publicação por efeito); 'source' = kZoneSourceMaster ou o índice da camada
    void publish(const void* owner, int source, const ZoneMask& mask);
    // O efeito deixou de existir (a textura vai embora com ele)
    void retract(const void* owner);

    // Máscara publicada neste quadro para a fonte (a publicada por último, se houver duas)
    bool find(int source, ZoneMask& out) const;
    bool available(int source) const;

private:
    struct Entry
    {
        const void* owner = nullptr;
        int source = -1;
        ZoneMask mask;
        bool fresh = false;
        std::uint64_t seq = 0;
    };
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::uint64_t seq_ = 0;
};

} // namespace bsl
