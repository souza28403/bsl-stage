#pragma once

// Saida de quadros do worker para o app: textura D3D11 compartilhada (preferida) ou memoria compartilhada.
// Ver media/WorkerProtocol.h para o protocolo.

#include "worker/PipeChannel.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

struct ID3D11Texture2D;

namespace bsl::worker {

class FrameSink
{
public:
    virtual ~FrameSink() = default;

    virtual const char* kind() const = 0; // "texture" ou "shm"
    // (Re)cria os recursos para w x h e avisa o app (mensagem tex/shm)
    virtual bool resize(int w, int h) = 0;
    // Publica um quadro BGRA8 da CPU. 'premultiplied': alpha premultiplicado (CEF OnPaint); o app espera alpha reta.
    // false = nao deu agora (app ainda segura os buffers): tente de novo no proximo ciclo.
    virtual bool submitBgra(const std::uint8_t* bgra, int stride, bool premultiplied, int page) = 0;
    // Publica direto uma textura da GPU (CEF OnAcceleratedPaint). So o TextureSink suporta.
    virtual bool submitSharedHandle(HANDLE sharedHandle, int page, std::string* error)
    {
        (void)sharedHandle;
        (void)page;
        if (error)
            *error = "não suportado";
        return false;
    }
    virtual bool supportsGpuHandle() const { return false; }

    int width() const { return width_; }
    int height() const { return height_; }

protected:
    int width_ = 0;
    int height_ = 0;
};

struct SinkRequest
{
    PipeChannel* channel = nullptr;
    DWORD appPid = 0;
    bool haveLuid = false;
    LONG luidHigh = 0;
    DWORD luidLow = 0;
    std::string transport = "auto"; // "auto", "texture" ou "shm"
};

// Escolhe e cria o sink (textura se possivel, senao memoria compartilhada). Registra a escolha no log do app.
std::unique_ptr<FrameSink> createFrameSink(const SinkRequest& request);
std::unique_ptr<FrameSink> createShmSink(const SinkRequest& request);

} // namespace bsl::worker
