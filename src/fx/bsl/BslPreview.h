#pragma once

// Previa em JPEG da saida de um efeito/fonte BSL para o painel web remoto (/api/slices/screen, /api/screen). So
// captura quando um painel pediu a previa ha pouco (nada de leitura da GPU sem ninguem olhando), no maximo 10x/s,
// em no maximo 640 px de largura.

#include "fx/FxGl.h"
#include "render/Fbo.h"

#include <glad/gl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

class PreviewCapture
{
public:
    // Thread do servidor: um painel pediu a previa agora
    void touch() { lastRequest_.store(nowMs()); }
    // Thread de GL: ha pedido recente e ja passou o intervalo minimo desde a ultima captura?
    bool wanted();
    // Thread de GL: copia 'tex' (w x h; 'topOrigin' = linha 0 e o topo) reduzida para a previa
    void capture(GLuint tex, int w, int h, bool topOrigin);
    // Thread do servidor: espera ate 'waitMs' por um quadro novo se nao ha nenhum recente; false se nao houver quadro
    bool jpeg(std::string& out, int waitMs = 500);
    bool hasFrame() const { return seq_.load() != 0; }
    std::uint64_t sequence() const { return seq_.load(); }
    void reset();

private:
    static std::int64_t nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::atomic<std::int64_t> lastRequest_{-1000000};
    std::atomic<std::int64_t> lastCapture_{-1000000};
    std::atomic<std::uint64_t> seq_{0};

    std::unique_ptr<Fbo> small_; // so na thread de GL
    TexBlitter blit_;

    std::mutex mutex_;
    std::vector<unsigned char> rgb_; // 3 componentes, linha 0 = topo
    int w_ = 0, h_ = 0;
    std::uint64_t encodedSeq_ = 0;
    std::string jpeg_;
};

} // namespace bsl::fx::bsllight
