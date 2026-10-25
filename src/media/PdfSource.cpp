#include "media/PdfSource.h"

#include "app/Log.h"
#include "media/DocInfo.h"

#include <algorithm>
#include <cmath>

namespace bsl::media {

namespace {

std::string utf8Path(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

PdfSource::PdfSource(int canvasWidth, int canvasHeight) : RemoteSource(canvasWidth, canvasHeight) {}

PdfSource::~PdfSource() = default;

std::vector<ParamDesc> PdfSource::describe(const ParamValues&, int pages)
{
    using namespace param;
    std::vector<ParamDesc> d;
    ParamDesc page = makeInt("page", "Página", 1, 1, pages > 0 ? pages : 9999, "%d", "Documento");
    page.tooltip = pages > 0 ? "Página mostrada (1 a " + std::to_string(pages) + ")"
                             : "Página mostrada (o total é lido do arquivo quando o clip é disparado)";
    d.push_back(std::move(page));
    d.push_back(makeColor("bg", "Cor de fundo", {0.0f, 0.0f, 0.0f, 1.0f}, "Documento"));

    d.push_back(makeEnum("autoMode", "Avanço automático",
                         {{"off", "Desligado"}, {"seconds", "A cada X segundos"}, {"beats", "A cada X batidas"}}, "off",
                         "Avanço"));
    ParamDesc secs = makeFloat("autoSeconds", "Segundos", 5.0f, 0.5f, 600.0f, "%.1f s", "Avanço");
    secs.enabledIf = "autoMode=seconds";
    d.push_back(std::move(secs));
    ParamDesc beats = makeInt("autoBeats", "Batidas", 4, 1, 256, "%d", "Avanço");
    beats.enabledIf = "autoMode=beats";
    beats.tooltip = "Usa o BPM da composição";
    d.push_back(std::move(beats));
    d.push_back(makeBool("loopEnd", "Voltar à primeira no fim", true, "Avanço"));

    d.push_back(makeEnum("transition", "Transição", {{"cut", "Corte"}, {"fade", "Fade"}}, "cut", "Transição"));
    ParamDesc fadeSecs = makeFloat("fadeSeconds", "Duração do fade", 0.5f, 0.05f, 5.0f, "%.2f s", "Transição");
    fadeSecs.enabledIf = "transition=fade";
    d.push_back(std::move(fadeSecs));
    return d;
}

int PdfSource::pageCount() const
{
    if (pages_ > 0)
        return pages_;
    return docPageCount(path());
}

WorkerJson PdfSource::bgJson() const
{
    const Color c = currentParams().c("bg");
    return WorkerJson::array({c.r, c.g, c.b, c.a});
}

WorkerJson PdfSource::buildOpenBody() const
{
    WorkerJson j;
    j["path"] = utf8Path(path());
    j["params"] = WorkerJson{{"bg", bgJson()}, {"page", requestedPage_}};
    return j;
}

void PdfSource::onOpened()
{
    pages_ = 0;
    const ParamSet p = currentParams();
    requestedPage_ = std::max(1, p.i("page"));
    lastParamPage_ = requestedPage_;
    lastBg_ = bgJson();
    timer_ = 0.0;
    lastBeatIndex_ = -1;
    alpha_ = lastBg_[3].get<float>() < 0.999f;
}

void PdfSource::onEvent(const WorkerJson& ev)
{
    if (ev.value("t", std::string()) != "doc")
        return;
    pages_ = ev.value("pages", 0);
    setDocPageCount(path(), pages_);
    if (pages_ > 0 && requestedPage_ > pages_) {
        requestedPage_ = pages_;
        pushOpen();
        sendToWorker(WorkerJson{{"t", "page"}, {"n", requestedPage_}});
    }
}

void PdfSource::goToPage(int n)
{
    const int total = pageCount();
    n = std::max(1, total > 0 ? std::min(n, total) : n);
    if (n == requestedPage_)
        return;
    requestedPage_ = n;
    timer_ = 0.0;
    if (isOpen()) {
        pushOpen();
        sendToWorker(WorkerJson{{"t", "page"}, {"n", n}});
    }
}

void PdfSource::onParamsChanged()
{
    if (!isOpen())
        return;
    const ParamSet p = currentParams();
    if (p.i("page") != lastParamPage_) {
        lastParamPage_ = p.i("page");
        goToPage(lastParamPage_);
    }
    const WorkerJson bg = bgJson();
    if (bg != lastBg_) {
        lastBg_ = bg;
        alpha_ = bg[3].get<float>() < 0.999f;
        pushOpen();
        sendToWorker(WorkerJson{{"t", "params"}, {"params", WorkerJson{{"bg", bg}}}});
    }
}

// Disparar de novo o clip ativo (Reiniciar) volta para a página do clip e zera o avanço automático
void PdfSource::onSeek(double)
{
    if (!isOpen())
        return;
    timer_ = 0.0;
    lastBeatIndex_ = -1;
    goToPage(std::max(1, currentParams().i("page")));
}

void PdfSource::onCanvasResized(int w, int h)
{
    if (!isOpen())
        return;
    pushOpen(); // o worker relancado ja abre no tamanho novo
    sendToWorker(WorkerJson{{"t", "resize"}, {"w", w}, {"h", h}});
}

void PdfSource::onTempo(double beat, float /*bpm*/)
{
    if (!isOpen() || !isPlaying() || pageCount() <= 0)
        return;
    const ParamSet p = currentParams();
    if (p.s("autoMode") != "beats")
        return;
    const int every = std::max(1, p.i("autoBeats"));
    const long long idx = static_cast<long long>(std::floor(beat / every));
    if (lastBeatIndex_ < 0) {
        lastBeatIndex_ = idx;
        return;
    }
    if (idx != lastBeatIndex_) {
        lastBeatIndex_ = idx;
        advance();
    }
}

void PdfSource::advance()
{
    const int total = pageCount();
    if (total <= 0)
        return;
    int n = requestedPage_ + 1;
    if (n > total) {
        if (!currentParams().b("loopEnd"))
            return;
        n = 1;
    }
    goToPage(n);
}

void PdfSource::beforeFrame(const FrameInfo& info)
{
    fadePending_ = false;
    if (info.first || info.page == shownPage() || rawTexture() == 0)
        return;
    if (info.width != width() || info.height != height())
        return;
    const ParamSet p = currentParams();
    if (p.s("transition") != "fade")
        return;
    if (!fade_)
        fade_ = std::make_unique<FadeBlender>();
    fade_->snapshot(rawTexture(), width(), height());
    fadePending_ = true;
}

void PdfSource::afterFrame(const FrameInfo&)
{
    if (fadePending_ && fade_) {
        fadePending_ = false;
        fade_->begin(rawTexture(), currentParams().f("fadeSeconds"));
    }
}

GLuint PdfSource::outputTexture() const
{
    if (fade_ && fade_->active())
        return fade_->output();
    return rawTexture();
}

void PdfSource::update(double dt)
{
    RemoteSource::update(dt);
    if (!isOpen())
        return;
    if (fade_ && fade_->active())
        fade_->update(dt, rawTexture());
    if (isPlaying() && pageCount() > 0) {
        const ParamSet p = currentParams();
        if (p.s("autoMode") == "seconds") {
            timer_ += dt;
            if (timer_ >= static_cast<double>(p.f("autoSeconds"))) {
                timer_ = 0.0;
                advance();
            }
        } else {
            timer_ = 0.0;
        }
    }
}

} // namespace bsl::media
