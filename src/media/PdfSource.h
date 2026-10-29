#pragma once

// PDF e documentos do Office (convertidos para PDF no worker). O worker renderiza a pagina atual no tamanho
// do canvas com PDFium e pre-renderiza a anterior e a proxima; aqui ficam os parametros, a navegacao, o
// avanco automatico (tempo ou batidas da composicao) e a transicao em fade.

#include "media/FadeBlender.h"
#include "media/RemoteSource.h"

#include <memory>

namespace bsl::media {

class PdfSource : public RemoteSource
{
public:
    PdfSource(int canvasWidth, int canvasHeight);
    ~PdfSource() override;

    // 'pages' = numero de paginas conhecido (0 = desconhecido): limita o parametro "page"
    static std::vector<ParamDesc> describe(const ParamValues& values, int pages = 0);
    std::vector<ParamDesc> paramDescs() const override { return describe(values_, pageCount()); }

    int pageCount() const;       // 0 enquanto o documento nao foi lido
    int page() const { return requestedPage_; }
    void goToPage(int n);        // 1..N
    void nextPage() { goToPage(requestedPage_ + 1); }
    void prevPage() { goToPage(requestedPage_ - 1); }

    void update(double dt) override;

protected:
    const char* kindName() const override { return "pdf"; }
    WorkerJson buildOpenBody() const override;
    void onOpened() override;
    void onEvent(const WorkerJson& event) override;
    void onParamsChanged() override;
    void onSeek(double seconds) override;
    void onCanvasResized(int w, int h) override;
    void onTempo(double beat, float bpm) override;
    void beforeFrame(const FrameInfo& info) override;
    void afterFrame(const FrameInfo& info) override;
    GLuint outputTexture() const override;

private:
    void advance();
    WorkerJson bgJson() const;

    int requestedPage_ = 1;
    int lastParamPage_ = 1;
    WorkerJson lastBg_;
    double timer_ = 0.0;
    long long lastBeatIndex_ = -1;
    int pages_ = 0;
    std::unique_ptr<FadeBlender> fade_;
    bool fadePending_ = false;
};

} // namespace bsl::media
