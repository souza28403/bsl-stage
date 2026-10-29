#pragma once

// Lista de slices que os efeitos BSL Slice Refiner (2 e 3) desenham. Em vez de ler o AdvancedOutput.xml do Resolume
// (como os plugins FFGL), o app publica aqui os slices do proprio OutputSetup a cada quadro; os efeitos tambem aceitam
// importar um XML do Resolume (parseResolumeSlices).

#include "output/OutputSetup.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

// Retangulo de entrada do slice, em pixels do canvas (origem no canto superior esquerdo)
struct FeedSlice
{
    std::string name;   // "Tela / Slice" (saida do app) ou o nome do <Slice> (XML)
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; // caixa que envolve o retangulo de entrada

    // Identificador estavel "<tela>/<slice>#<indice na tela>" (chave das atribuicoes de grupo do Slice Refiner 3)
    std::string id;
    std::string screen;
    std::string sliceName;
    // Retangulo orientado (centro, meias dimensoes e angulo em graus); sem rotacao = igual a caixa
    float cx = 0.0f, cy = 0.0f, hw = 0.0f, hh = 0.0f, angle = 0.0f;

    bool operator==(const FeedSlice& o) const
    {
        return name == o.name && x == o.x && y == o.y && w == o.w && h == o.h && id == o.id && cx == o.cx && cy == o.cy &&
               hw == o.hw && hh == o.hh && angle == o.angle;
    }
};

class SliceFeed
{
public:
    static SliceFeed& instance();

    // Publica os slices (canvas w x h). So conta como mudanca se algo mudou de fato.
    void set(std::vector<FeedSlice> slices, int canvasW, int canvasH);
    std::vector<FeedSlice> slices() const;
    int canvasWidth() const;
    int canvasHeight() const;
    std::uint64_t version() const;

private:
    mutable std::mutex mutex_;
    std::vector<FeedSlice> slices_;
    int canvasW_ = 1920;
    int canvasH_ = 1080;
    std::uint64_t version_ = 0;
};

// Todos os slices ligados de todas as Screens (na ordem), pelo retangulo de entrada
std::vector<FeedSlice> sliceFeedFromOutput(const OutputSetup& output);

// Slices de um AdvancedOutput.xml do Resolume (caixa que envolve o InputRect de cada <Slice>, e o retangulo orientado).
// 'compW'/'compH' (opcionais) recebem o <CurrentCompositionTextureSize> do XML (0 se nao houver).
std::vector<FeedSlice> parseResolumeSlices(const std::string& xml, int* compW = nullptr, int* compH = nullptr);
// AdvancedOutput.xml padrao do Resolume Arena (Documents\Resolume Arena\Preferences, mesmo com a pasta Documentos
// redirecionada para o OneDrive); pode nao existir
std::string defaultResolumeSliceXmlPath();

// Retangulo orientado a partir dos 4 vertices (v0->v1 e o primeiro lado), normalizado para |angulo| <= 45 graus
// (girar 90 graus troca largura e altura). Mesma conta do BSLSliceRefiner original.
void orientedFromVerts(const float vx[4], const float vy[4], float& cx, float& cy, float& hw, float& hh, float& angleDeg);

} // namespace bsl::fx::bsllight
