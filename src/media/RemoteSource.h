#pragma once

// Fonte generica cujo conteudo vem de um processo auxiliar (bslstage_worker.exe): PDF, Office e Web.
// Cuida do worker (media/WorkerHost), dos quadros (textura D3D11 compartilhada com fallback de memoria
// compartilhada) e da textura GL. As fontes especificas (PdfSource, WebSource) declaram os parametros e
// montam a mensagem "open".
//
// Nunca bloqueia: open() so dispara o worker; enquanto nao chega quadro, texture() e 0 (o Compositor pula).
// Se o worker cai, o ultimo quadro continua na tela e o status mostra "Reiniciando…".

#include "media/ISource.h"
#include "media/WorkerHost.h"

#include <memory>
#include <string>

namespace bsl::render {
class D3D11Interop;
class InteropTexture;
} // namespace bsl::render

namespace bsl::media {

class RemoteSource : public ISource
{
public:
    RemoteSource(int canvasWidth, int canvasHeight);
    ~RemoteSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override { return outputTexture(); }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return true; }
    bool supportsReverse() const override { return false; }
    SourceStatus status() const override;

    // --- Diagnostico / testes ---
    std::string transportName() const;     // "texture" | "shm" | ""
    std::string transportReason() const;
    unsigned long workerPid() const;
    HostSnapshot hostSnapshot() const { return host_.snapshot(); }
    unsigned long long framesShown() const { return framesShown_; }
    int shownPage() const { return shownPage_; }
    void debugKillWorker() { host_.debugKillWorker(); }
    // Testes: forca o transporte das proximas aberturas ("auto", "texture" ou "shm")
    static void forceTransport(const std::string& transport);

    // --- Interacao (Web): coordenadas em pixels da imagem do worker ---
    void sendMouse(const char* type, int x, int y, int button, int clicks, int mods);
    void sendWheel(int x, int y, float dx, float dy);
    void sendKey(const char* type, int code, unsigned ch, int mods);
    void requestReload() { sendToWorker(WorkerJson{{"t", "reload"}}); }

protected:
    struct FrameInfo
    {
        int page = 0;
        int width = 0;
        int height = 0;
        bool first = false;
    };

    // Tipo de worker ("pdf" ou "web") e corpo da mensagem "open" (path/url, params...). w/h/kind sao preenchidos pela base
    // a partir de canvas/outputSize.
    virtual const char* kindName() const = 0;
    virtual WorkerJson buildOpenBody() const = 0;
    // Tamanho pedido ao worker (padrao = canvas)
    virtual void outputSize(int& w, int& h) const
    {
        w = canvasW_;
        h = canvasH_;
    }
    virtual void onOpened() {}
    virtual void onEvent(const WorkerJson& /*event*/) {}
    // Antes/depois de um quadro novo ser aplicado na textura
    virtual void beforeFrame(const FrameInfo& /*info*/) {}
    virtual void afterFrame(const FrameInfo& /*info*/) {}
    // Textura exibida; as derivadas podem trocar (fade)
    virtual GLuint outputTexture() const { return rawTexture(); }

    GLuint rawTexture() const { return activeTex_; }
    WorkerJson fullOpen() const;
    void pushOpen() { host_.setOpen(fullOpen()); }
    void sendToWorker(WorkerJson msg) { host_.send(std::move(msg)); }
    WorkerHost& host() { return host_; }
    const std::filesystem::path& path() const { return path_; }

    int width_ = 0;
    int height_ = 0;
    bool alpha_ = true;

private:
    void applyFrame(const FrameAccess& fa);

    WorkerHost host_;
    std::shared_ptr<render::D3D11Interop> interop_;
    std::unique_ptr<render::InteropTexture> interopTex_;
    GLuint glTex_ = 0;
    int glTexW_ = 0, glTexH_ = 0;
    GLuint activeTex_ = 0;
    std::uint64_t lastSeq_ = 0;
    unsigned long long framesShown_ = 0;
    int shownPage_ = 0;
    bool open_ = false;
    bool anyFrame_ = false;
    std::filesystem::path path_;
    std::string lastError_;
};

} // namespace bsl::media
