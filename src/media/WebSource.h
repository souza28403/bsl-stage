#pragma once

// Pagina web ao vivo (CEF em modo off-screen no worker). Serve ao gerador "Web (URL)" e aos arquivos
// .html/.htm soltos na grade (abrem como file://). Fonte exclusiva: so uma instancia por clip.

#include "media/RemoteSource.h"

namespace bsl::media {

class WebSource final : public RemoteSource
{
public:
    WebSource(int canvasWidth, int canvasHeight);
    ~WebSource() override;

    // Gerador (com campo URL) e arquivo .html (sem URL)
    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::vector<ParamDesc> describeFile(const ParamValues& values);
    static std::string label(const ParamValues& values);
    // O CEF (cef/libcef.dll ao lado do worker) esta presente? Sem ele a Web fica desabilitada.
    static bool available();
    // file:///C:/pasta/arquivo.html com percent-encoding
    static std::string fileUrl(const std::filesystem::path& file);

    std::vector<ParamDesc> paramDescs() const override;
    bool isLive() const override { return true; }
    // Titulo da pagina, se o worker mandou
    std::string pageTitle() const { return hostSnapshot().title; }
    // Pagina carregada e enviando quadros
    bool hasPageSize() const { return width() > 0; }

protected:
    const char* kindName() const override { return "web"; }
    WorkerJson buildOpenBody() const override;
    void outputSize(int& w, int& h) const override;
    void onOpened() override;
    void onParamsChanged() override;
    void onCanvasResized(int w, int h) override;

private:
    WorkerJson paramsJson() const;
    std::string currentUrl() const;

    bool fileMode_ = false;
    std::string lastUrl_;
    WorkerJson lastParams_;
    int lastW_ = 0, lastH_ = 0;
};

} // namespace bsl::media
