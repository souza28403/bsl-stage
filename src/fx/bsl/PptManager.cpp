// BSL PPT Manager (plugin FFGL "BSPT"): carrega um PPTX/PPT/PDF e mostra um slide por vez, com anterior/proximo, indice
// do slide e recarregar. No BSL Stage ele e a fonte PDF/Office do app (PdfSource) apresentada como fonte "bsl:ppt-manager"
// com o parametro "Documento", como no plugin:
//  - a conversao roda no processo auxiliar (PowerPoint por COM ou LibreOffice, depois PDFium) em vez de gerar JPGs em
//    %TEMP% dentro de SetTextParameter, que travava o Resolume enquanto convertia;
//  - o avanco automatico (segundos ou batidas), a volta ao primeiro slide, a cor de fundo e a transicao em fade sao do
//    PdfSource (o plugin so tinha anterior, proximo, indice e recarregar).

#include "fx/bsl/BslSources.h"

#include "app/Log.h"
#include "media/DocInfo.h"
#include "media/PdfSource.h"

#include <map>

namespace bsl::fx::bsllight {

namespace {

using media::Json;
using media::ParamDesc;
using media::ParamSet;
using media::ParamValues;
using namespace media::param;

std::filesystem::path docPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

int counterOf(const ParamSet& p, const char* id)
{
    const Json v = p.raw(id);
    return v.is_number() ? v.get<int>() : 0;
}

class PptManagerSource final : public media::PdfSource
{
public:
    PptManagerSource(int w, int h) : media::PdfSource(w, h) {}

    std::vector<ParamDesc> paramDescs() const override { return describePptManager(values_, pageCount()); }

    // O documento vem do parametro (o plugin tinha "Document (PPTX/PDF)"); sem ele a fonte nao abre
    bool open(const std::filesystem::path& /*unused*/, std::string* error = nullptr) override
    {
        const std::string doc = currentParams().s("document");
        if (doc.empty()) {
            if (error)
                *error = "escolha o documento (PPTX, PPT ou PDF) nos parâmetros do clip";
            return false;
        }
        openedDoc_ = doc;
        return media::PdfSource::open(docPath(doc), error);
    }

protected:
    void onOpened() override
    {
        media::PdfSource::onOpened();
        const ParamSet p = currentParams();
        prev_ = counterOf(p, "prev");
        next_ = counterOf(p, "next");
        reload_ = counterOf(p, "reload");
    }

    void onParamsChanged() override
    {
        if (isOpen()) {
            const ParamSet p = currentParams();
            if (p.s("document") != openedDoc_) { // outro documento: reabre
                std::string error;
                close();
                if (!open({}, &error))
                    log::warn("BSL PPT Manager: " + error);
                return;
            }
            const int prev = counterOf(p, "prev"), next = counterOf(p, "next"), reload = counterOf(p, "reload");
            const bool doPrev = prev != prev_, doNext = next != next_, doReload = reload != reload_;
            prev_ = prev;
            next_ = next;
            reload_ = reload;
            if (doReload) { // refaz a conversao (o cache vale pela data do arquivo; um documento salvo de novo e reconvertido)
                std::string error;
                close();
                if (!open({}, &error))
                    log::warn("BSL PPT Manager: " + error);
                return;
            }
            if (doPrev)
                prevPage();
            if (doNext)
                nextPage();
        }
        media::PdfSource::onParamsChanged();
    }

private:
    std::string openedDoc_;
    int prev_ = 0, next_ = 0, reload_ = 0;
};

} // namespace

std::vector<ParamDesc> describePptManager(const ParamValues& values, int pages)
{
    std::vector<ParamDesc> d;
    d.push_back(makeFile("document", "Documento (PPTX/PDF)", "Apresentações e PDF (*.pptx;*.ppt;*.odp;*.pdf)|*.pptx;*.ppt;*.odp;*.pdf", "Documento"));
    d.back().tooltip = "Apresentação do PowerPoint (PPTX, PPT), do LibreOffice (ODP) ou PDF. A conversão acontece em segundo plano, sem travar a saída";
    d.push_back(makeCustom("prev", "Slide anterior", "event", Json(0), "Documento"));
    d.push_back(makeCustom("next", "Próximo slide", "event", Json(0), "Documento"));
    d.push_back(makeCustom("reload", "Recarregar documento", "event", Json(0), "Documento"));
    d.back().tooltip = "Converte o documento de novo (use depois de salvar a apresentação)";

    int total = pages;
    if (total <= 0) {
        std::string doc;
        if (const Json* v = values.find("document"); v && v->is_string())
            doc = v->get<std::string>();
        if (!doc.empty())
            total = media::docPageCount(docPath(doc));
    }
    for (ParamDesc& p : media::PdfSource::describe(values, total)) {
        if (p.id == "page") {
            p.label = "Slide";
            p.tooltip = total > 0 ? "Slide mostrado (1 a " + std::to_string(total) + ")"
                                  : "Slide mostrado (o total é lido do arquivo quando o clip é disparado)";
        }
        d.push_back(std::move(p));
    }
    return d;
}

std::unique_ptr<media::ISource> makePptManager(int canvasW, int canvasH)
{
    return std::make_unique<PptManagerSource>(canvasW, canvasH);
}

} // namespace bsl::fx::bsllight
