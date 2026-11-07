#include "media/WebSource.h"

#include "media/WorkerProcess.h"

#include <algorithm>
#include <cctype>

namespace bsl::media {

namespace {

std::string utf8(const std::filesystem::path& p)
{
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

}

WebSource::WebSource(int canvasWidth, int canvasHeight) : RemoteSource(canvasWidth, canvasHeight) {}

WebSource::~WebSource() = default;

std::vector<ParamDesc> WebSource::describeFile(const ParamValues&)
{
    using namespace param;
    std::vector<ParamDesc> d;
    ParamDesc w = makeInt("width", "Largura (0 = canvas)", 0, 0, 8192, "%d px", "Página");
    w.tooltip = "Tamanho em que a página é desenhada; 0 acompanha o canvas";
    ParamDesc h = makeInt("height", "Altura (0 = canvas)", 0, 0, 8192, "%d px", "Página");
    h.tooltip = w.tooltip;
    d.push_back(std::move(w));
    d.push_back(std::move(h));
    d.push_back(makeFloat("zoom", "Zoom", 1.0f, 0.25f, 5.0f, "%.2fx", "Página"));
    d.push_back(makeEnum("fps", "FPS", {{"30", "30 fps"}, {"60", "60 fps"}}, "30", "Página"));
    d.push_back(makeBool("transparent", "Fundo transparente", false, "Página"));
    ParamDesc reload = makeInt("reloadMinutes", "Recarregar a cada (min, 0 = nunca)", 0, 0, 1440, "%d min", "Página");
    d.push_back(std::move(reload));
    d.push_back(makeBool("muted", "Áudio mudo", true, "Página"));
    ParamDesc css = makeText("css", "CSS extra", "", true, "Página");
    css.tooltip = "Injetado em toda página carregada (ex.: body { background: transparent !important; })";
    d.push_back(std::move(css));
    return d;
}

std::vector<ParamDesc> WebSource::describe(const ParamValues& values)
{
    using namespace param;
    std::vector<ParamDesc> d;
    ParamDesc url = makeText("url", "URL", "", false, "Página");
    url.maxLength = 2048;
    url.tooltip = "Endereço da página (https://…); sem esquema vira https://";
    d.push_back(std::move(url));
    for (ParamDesc& p : describeFile(values))
        d.push_back(std::move(p));
    return d;
}

std::string WebSource::label(const ParamValues& values)
{
    std::string url;
    if (const Json* v = values.find("url"); v && v->is_string())
        url = v->get<std::string>();
    const size_t scheme = url.find("://");
    if (scheme != std::string::npos)
        url.erase(0, scheme + 3);
    const size_t slash = url.find_first_of("/?#");
    if (slash != std::string::npos)
        url.erase(slash);
    if (url.rfind("www.", 0) == 0)
        url.erase(0, 4);
    return url.empty() ? "Web (URL)" : "Web: " + url;
}

bool WebSource::available()
{
    static const bool ok = [] {
        std::error_code ec;
        return std::filesystem::is_regular_file(
            std::filesystem::path(workerExePath()).parent_path() / L"cef" / L"libcef.dll", ec);
    }();
    return ok;
}

std::string WebSource::fileUrl(const std::filesystem::path& file)
{
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(file, ec);
    const std::string s = utf8(abs);
    std::string out = "file:///";
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::vector<ParamDesc> WebSource::paramDescs() const
{
    return fileMode_ ? describeFile(values_) : describe(values_);
}

std::string WebSource::currentUrl() const
{
    if (fileMode_)
        return fileUrl(path());
    std::string url = currentParams().s("url");
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back())))
        url.pop_back();
    size_t i = 0;
    while (i < url.size() && std::isspace(static_cast<unsigned char>(url[i])))
        ++i;
    url.erase(0, i);
    if (url.empty())
        return "about:blank";
    if (url.find("://") == std::string::npos && url.rfind("about:", 0) != 0 && url.rfind("data:", 0) != 0)
        url = "https://" + url;
    return url;
}

WorkerJson WebSource::paramsJson() const
{
    const ParamSet p = currentParams();
    return WorkerJson{{"zoom", p.f("zoom")},
                      {"fps", p.s("fps") == "60" ? 60 : 30},
                      {"transparent", p.b("transparent")},
                      {"reloadMinutes", p.i("reloadMinutes")},
                      {"muted", p.b("muted")},
                      {"css", p.s("css")}};
}

WorkerJson WebSource::buildOpenBody() const
{
    WorkerJson j;
    j["url"] = currentUrl();
    j["params"] = paramsJson();
    return j;
}

void WebSource::outputSize(int& w, int& h) const
{
    const ParamSet p = currentParams();
    w = p.i("width") > 0 ? p.i("width") : canvasW_;
    h = p.i("height") > 0 ? p.i("height") : canvasH_;
}

void WebSource::onOpened()
{
    fileMode_ = !path().empty();
    lastUrl_ = currentUrl();
    lastParams_ = paramsJson();
    int w = 0, h = 0;
    outputSize(w, h);
    lastW_ = w;
    lastH_ = h;
    alpha_ = currentParams().b("transparent");
}

void WebSource::onParamsChanged()
{
    if (!isOpen())
        return;
    alpha_ = currentParams().b("transparent");
    pushOpen();
    const std::string url = currentUrl();
    if (url != lastUrl_) {
        lastUrl_ = url;
        sendToWorker(WorkerJson{{"t", "url"}, {"url", url}});
    }
    const WorkerJson params = paramsJson();
    if (params != lastParams_) {
        lastParams_ = params;
        sendToWorker(WorkerJson{{"t", "params"}, {"params", params}});
    }
    int w = 0, h = 0;
    outputSize(w, h);
    if (w != lastW_ || h != lastH_) {
        lastW_ = w;
        lastH_ = h;
        sendToWorker(WorkerJson{{"t", "resize"}, {"w", w}, {"h", h}});
    }
}

void WebSource::onCanvasResized(int, int)
{
    if (!isOpen())
        return;
    int w = 0, h = 0;
    outputSize(w, h);
    if (w == lastW_ && h == lastH_)
        return;
    lastW_ = w;
    lastH_ = h;
    pushOpen();
    sendToWorker(WorkerJson{{"t", "resize"}, {"w", w}, {"h", h}});
}

}
