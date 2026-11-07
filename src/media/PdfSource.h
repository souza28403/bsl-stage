#pragma once

#include "media/FadeBlender.h"
#include "media/RemoteSource.h"

#include <memory>

namespace bsl::media {

class PdfSource : public RemoteSource
{
public:
    PdfSource(int canvasWidth, int canvasHeight);
    ~PdfSource() override;

    static std::vector<ParamDesc> describe(const ParamValues& values, int pages = 0);
    std::vector<ParamDesc> paramDescs() const override { return describe(values_, pageCount()); }

    int pageCount() const;
    int page() const { return requestedPage_; }
    void goToPage(int n);
    void nextPage() { goToPage(requestedPage_ + 1); }
    void prevPage() { goToPage(requestedPage_ - 1); }
    void firstPage() { goToPage(homePage()); }
    void lastPage() { goToPage(pageCount() > 0 ? pageCount() : requestedPage_); }
    int homePage() const;
    void reloadDocument();

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
    int reopenPage_ = 0;
    int lastParamPage_ = 1;
    WorkerJson lastBg_;
    double timer_ = 0.0;
    long long lastBeatIndex_ = -1;
    int pages_ = 0;
    std::unique_ptr<FadeBlender> fade_;
    bool fadePending_ = false;
};

}
