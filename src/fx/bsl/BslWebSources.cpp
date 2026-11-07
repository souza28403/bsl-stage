#include "fx/bsl/BslPreview.h"
#include "fx/bsl/BslSources.h"
#include "fx/bsl/WebStudio.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "media/ControlPanel.h"
#include "media/WebSource.h"
#include "net/MiniHttp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>

namespace bsl::fx::bsllight {

namespace {

using media::Json;
using media::ParamDesc;
using media::ParamSet;
using media::ParamValues;
using media::WorkerJson;
using namespace media::param;

std::string base64Encode(const std::string& in)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back(tbl[v & 63]);
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string base64Decode(const std::string& in)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+' || c == '-')
            return 62;
        if (c == '/' || c == '_')
            return 63;
        return -1;
    };
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    for (char c : in) {
        const int v = val(c);
        if (v < 0)
            continue;
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::filesystem::path pathFromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

const char* mimeForImage(const std::string& ext)
{
    if (ext == ".png")
        return "image/png";
    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";
    if (ext == ".webp")
        return "image/webp";
    if (ext == ".svg")
        return "image/svg+xml";
    if (ext == ".gif")
        return "image/gif";
    return "application/octet-stream";
}

std::string imageDataUrl(const std::string& pathUtf8)
{
    if (pathUtf8.empty())
        return {};
    const std::filesystem::path file = pathFromUtf8(pathUtf8);
    std::string bytes;
    if (!net::readWholeFile(file, bytes, 12u << 20))
        return {};
    std::string ext = paths::toUtf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::string("data:") + mimeForImage(ext) + ";base64," + base64Encode(bytes);
}

std::string jsString(const std::string& s)
{
    return Json(s).dump(-1, ' ', true, Json::error_handler_t::replace);
}

std::string whenReady(const std::string& condition, const std::string& body)
{
    return "(function w(n){if(" + condition + "){" + body + "}else if(n<150)setTimeout(function(){w(n+1)},100);})(0);";
}

std::filesystem::path assetFile(const wchar_t* dir, const wchar_t* name)
{
    return paths::assetsDir() / L"bsl" / dir / name;
}

std::string lowerCopy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

class BslWebSource final : public media::WebSource, public media::IControlPanel
{
public:
    BslWebSource(int w, int h) : media::WebSource(w, h) {}
    ~BslWebSource() override { stopRemote(); }

    std::vector<ParamDesc> paramDescs() const override { return describeWebBackgrounds(values_); }

    bool openControlServer(std::string& url, std::string* error) override
    {
        panelWanted_ = true;
        lastTry_ = {};
        syncRemote();
        std::lock_guard lk(mutex_);
        if (!serverRunning_) {
            if (error)
                *error = remoteError_.empty() ? "não foi possível ligar o servidor local" : remoteError_;
            return false;
        }
        url = "http://127.0.0.1:" + std::to_string(remotePortLive_) + "/";
        return true;
    }
    std::string controlUrl() const override
    {
        std::lock_guard lk(mutex_);
        return serverRunning_ ? "http://127.0.0.1:" + std::to_string(remotePortLive_) + "/" : std::string();
    }
    bool controlExposedToNetwork() const override
    {
        std::lock_guard lk(mutex_);
        return serverRunning_ && !serverLoopback_;
    }

    void update(double dt) override
    {
        media::WebSource::update(dt);
        if (!isOpen()) {
            stopRemote();
            return;
        }
        syncRemote();
        if (server_.running() && preview_.wanted() && texture() != 0 && width() > 0)
            preview_.capture(texture(), width(), height(), true);
    }

    bool pollParamChanges(ParamValues& values) override
    {
        PendingTake take;
        {
            std::lock_guard lk(mutex_);
            take.state = std::move(pendingState_);
            pendingState_ = Json();
            take.js.swap(pendingJs_);
            take.logo = std::move(pendingLogo_);
            take.hasLogo = hasPendingLogo_;
            pendingLogo_.clear();
            hasPendingLogo_ = false;
            take.reload = pendingReload_;
            pendingReload_ = false;
            take.clicks.swap(pendingClicks_);
        }
        bool changed = false;
        if (take.state.is_object())
            changed = applyStudioState(take.state, values) || changed;
        if (take.hasLogo)
            changed = adoptRemoteLogo(take.logo, values) || changed;
        if (pendingModeLogo_) {
            pendingModeLogo_ = false;
            const Json* mode = values.find("text_mode");
            if (!mode || *mode != Json("logo")) {
                values.set("text_mode", "logo");
                changed = true;
            }
        }
        for (const std::string& js : take.js)
            sendToWorker(WorkerJson{{"t", "eval"}, {"js", js}});
        if (take.reload)
            requestReload();
        for (const auto& c : take.clicks)
            clickAt(c.first, c.second);
        if (changed) {
            values_ = values;
            applyParams(false);
            pushOpen();
            flushOutbox();
        }
        return changed;
    }

    media::SourceStatus status() const override
    {
        media::SourceStatus s = media::WebSource::status();
        if (!s.text.empty() && s.level != media::SourceStatus::Level::Info)
            return s;
        std::lock_guard lk(mutex_);
        if (!remoteError_.empty())
            return {media::SourceStatus::Level::Warning, "Controle remoto: " + remoteError_};
        if (!s.text.empty())
            return s;
        if (serverRunning_ && serverLoopback_)
            return {media::SourceStatus::Level::Info,
                    "Painel de controle em http://127.0.0.1:" + std::to_string(remotePortLive_) + "/ (só neste computador)"};
        if (serverRunning_)
            return {media::SourceStatus::Level::Info,
                    "Controle remoto ligado: http://localhost:" + std::to_string(remotePortLive_) +
                        "/ (no celular ou tablet, use o IP deste computador nessa porta)"};
        return {};
    }

protected:
    std::string currentUrl() const override
    {
        return fileUrl(assetFile(L"web_bg", L"index.html"));
    }

    WorkerJson paramsJson() const override
    {
        const ParamSet p = currentParams();
        return WorkerJson{{"zoom", 1.0},
                          {"fps", p.s("fps") == "30" ? 30 : 60},
                          {"transparent", p.b("transparent")},
                          {"reloadMinutes", 0},
                          {"muted", true},
                          {"css", pageCss(p)}};
    }

    std::string pageCss(const ParamSet& p) const
    {
        if (p.b("transparent"))
            return "html,body,#root,#root>div{background:transparent !important}";
        return "";
    }

    WorkerJson buildOpenBody() const override
    {
        WorkerJson j = media::WebSource::buildOpenBody();
        WorkerJson init = WorkerJson::array();
        for (const auto& [key, js] : sticky_)
            init.push_back(WorkerJson{{"key", key}, {"js", js}});
        j["init"] = std::move(init);
        return j;
    }

    void onOpened() override
    {
        media::WebSource::onOpened();
        sticky_.clear();
        outbox_.clear();
        lastStateStr_.clear();
        lastLogoPath_.clear();
        applyParams(true);
    }

    void onParamsChanged() override
    {
        if (!isOpen())
            return;
        applyParams(false);
        media::WebSource::onParamsChanged();
        flushOutbox();
    }

private:
    struct PendingTake
    {
        Json state;
        std::vector<std::string> js;
        std::string logo;
        bool hasLogo = false;
        bool reload = false;
        std::vector<std::pair<float, float>> clicks;
    };

    void setSticky(const std::string& key, const std::string& js)
    {
        bool found = false;
        for (auto& entry : sticky_)
            if (entry.first == key) {
                entry.second = js;
                found = true;
            }
        if (!found)
            sticky_.emplace_back(key, js);
        outbox_.push_back(WorkerJson{{"t", "eval"}, {"key", key}, {"js", js}});
    }

    void sendOnce(const std::string& js) { outbox_.push_back(WorkerJson{{"t", "eval"}, {"js", js}}); }

    void flushOutbox()
    {
        if (!isOpen()) {
            outbox_.clear();
            return;
        }
        for (WorkerJson& m : outbox_)
            sendToWorker(std::move(m));
        outbox_.clear();
    }

    void applyParams(bool opening)
    {
        const ParamSet p = currentParams();
        remoteWanted_ = p.b("remote");
        remotePort_ = std::clamp(p.i("remotePort"), 1024, 65535);
        applyBackgrounds(p);
        if (opening)
            outbox_.clear();
    }

    void applyBackgrounds(const ParamSet& p)
    {
        const Json state = buildStudioState(p);
        const std::string text = state.dump(-1, ' ', true, Json::error_handler_t::replace);
        if (text != lastStateStr_) {
            lastStateStr_ = text;
            setSticky("state", whenReady("window.applyState", "window.applyState(" + text + ");"));
            std::lock_guard lk(mutex_);
            snapshotState_ = state;
            ++rev_;
        }
        const std::string logo = p.s("logoFile");
        if (logo != lastLogoPath_) {
            if (!lastLogoPath_.empty() || !logo.empty())
                pendingModeLogo_ = pendingModeLogo_ || (!logo.empty() && logoFromUser_);
            lastLogoPath_ = logo;
            const std::string url = imageDataUrl(logo);
            setSticky("logo", whenReady("window.updateLogo", "window.updateLogo(" + jsString(url.empty() ? std::string("none") : url) + ");"));
            std::lock_guard lk(mutex_);
            snapshotLogo_ = url;
            ++logoRev_;
        }
        logoFromUser_ = true;
    }

    void clickAt(float nx, float ny)
    {
        const int x = static_cast<int>(std::lround(std::clamp(nx, 0.0f, 1.0f) * static_cast<float>(std::max(width(), 1))));
        const int y = static_cast<int>(std::lround(std::clamp(ny, 0.0f, 1.0f) * static_cast<float>(std::max(height(), 1))));
        sendMouse("move", x, y, 0, 0, 0);
        sendMouse("down", x, y, 0, 1, 0);
        sendMouse("up", x, y, 0, 1, 0);
    }

    bool adoptRemoteLogo(const std::string& val, ParamValues& values)
    {
        if (val.empty() || val == "none") {
            const Json* cur = values.find("logoFile");
            if (cur && cur->is_string() && !cur->get<std::string>().empty()) {
                logoFromUser_ = false;
                values.set("logoFile", "");
                return true;
            }
            return false;
        }
        std::string ext = ".png";
        std::string payload = val;
        if (val.rfind("data:", 0) == 0) {
            const size_t comma = val.find(',');
            const std::string head = comma == std::string::npos ? val : val.substr(0, comma);
            payload = comma == std::string::npos ? std::string() : val.substr(comma + 1);
            if (head.find("jpeg") != std::string::npos)
                ext = ".jpg";
            else if (head.find("webp") != std::string::npos)
                ext = ".webp";
            else if (head.find("svg") != std::string::npos)
                ext = ".svg";
        }
        const std::string bytes = base64Decode(payload);
        if (bytes.empty())
            return false;
        std::error_code ec;
        const std::filesystem::path dir = paths::appDataDir() / L"web-logos";
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path file = dir / (L"logo-remoto-" + std::to_wstring(std::hash<std::string>{}(bytes) & 0xFFFFFFu) + std::wstring(ext.begin(), ext.end()));
        {
            std::ofstream out(file, std::ios::binary);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!out)
                return false;
        }
        logoFromUser_ = false;
        values.set("logoFile", paths::toUtf8(file));
        values.set("text_mode", "logo");
        return true;
    }

    void stopRemote()
    {
        server_.stop();
        std::lock_guard lk(mutex_);
        serverRunning_ = false;
    }

    void syncRemote()
    {
        const bool wanted = remoteWanted_ || panelWanted_;
        if (!wanted) {
            if (server_.running())
                stopRemote();
            std::lock_guard lk(mutex_);
            remoteError_.clear();
            return;
        }
        const bool loopback = !remoteWanted_;
        if (server_.running() && server_.port() == remotePort_ && serverLoopback_ == loopback)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (server_.running())
            stopRemote();
        else if (now - lastTry_ < std::chrono::seconds(2))
            return;
        lastTry_ = now;
        std::string error;
        if (server_.start(remotePort_, [this](const net::HttpRequest& rq, net::HttpResponse& rs) { handle(rq, rs); }, &error,
                          loopback)) {
            std::lock_guard lk(mutex_);
            serverRunning_ = true;
            serverLoopback_ = loopback;
            remotePortLive_ = remotePort_;
            remoteError_.clear();
            log::info(std::string("WebBackgrounds: ") + (loopback ? "painel de controle (só neste computador) em http://127.0.0.1:"
                                                                  : "controle remoto pela rede em http://localhost:") +
                      std::to_string(remotePort_) + "/");
        } else {
            std::lock_guard lk(mutex_);
            remoteError_ = error;
        }
    }

    void serveFile(const std::filesystem::path& file, const char* mime, net::HttpResponse& rs)
    {
        std::string bytes;
        if (!net::readWholeFile(file, bytes)) {
            rs.text("arquivo não encontrado: " + paths::toUtf8(file.filename()), 404);
            return;
        }
        rs.status = 200;
        rs.contentType = mime;
        rs.body = std::move(bytes);
    }

    void queueJs(const std::string& js)
    {
        std::lock_guard lk(mutex_);
        pendingJs_.push_back(js);
    }
    void mergeState(const Json& patch)
    {
        std::lock_guard lk(mutex_);
        if (!pendingState_.is_object())
            pendingState_ = Json::object();
        pendingState_.merge_patch(patch);
        ++rev_;
    }

    std::string currentBgId()
    {
        std::lock_guard lk(mutex_);
        if (snapshotState_.is_object() && snapshotState_.contains("bg") && snapshotState_["bg"].contains("id"))
            return snapshotState_["bg"]["id"].get<std::string>();
        return "Silk";
    }

    void legacyAction(const std::string& cmd, const std::string& val, const std::string& param)
    {
        auto num = [](const std::string& s) { return std::atof(s.c_str()); };
        if (cmd == "state") {
            const Json st = Json::parse(val, nullptr, false);
            if (st.is_object())
                mergeState(st);
        } else if (cmd == "bg") {
            mergeState(Json{{"bg", {{"id", val}}}});
        } else if (cmd == "config") {
            std::vector<std::string> parts;
            size_t pos = 0;
            while (pos <= val.size()) {
                const size_t comma = val.find(',', pos);
                parts.push_back(val.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
                if (comma == std::string::npos)
                    break;
                pos = comma + 1;
            }
            if (parts.size() >= 3)
                mergeState(Json{{"palette", {{"id", "custom"}, {"colors", Json::array({parts[0], parts[1]})}}}, {"bg", {{"speed", num(parts[2])}}}});
        } else if (cmd == "bg_param") {
            mergeState(Json{{"bg", {{"params", {{currentBgId(), {{param, num(val)}}}}}}}});
        } else if (cmd == "logo_url") {
            std::lock_guard lk(mutex_);
            pendingLogo_ = val;
            hasPendingLogo_ = true;
        } else if (cmd == "logo_text") {
            mergeState(Json{{"text", {{"content", val == "none" ? std::string() : val}}}});
        } else if (cmd == "logo_effect") {
            static const std::map<std::string, std::string> legacy = {{"none", "none"}, {"float", "float"}, {"glow", "breathe"}, {"glitch", "glitch"},
                                                                       {"tilt", "sway"}, {"pulse", "pulse"},  {"spin", "none"},   {"bounce", "none"},
                                                                       {"flicker", "flicker"}, {"wave", "wave"}};
            const auto it = legacy.find(val);
            mergeState(Json{{"text", {{"loop", it == legacy.end() ? "none" : it->second}}}});
        } else if (cmd == "logo_size") {
            mergeState(Json{{"text", {{"size", num(val)}}}});
        } else if (cmd == "logo_entrance") {
            mergeState(Json{{"text", {{"in", val}}}});
        } else if (cmd == "logo_toggle") {
            mergeState(Json{{"text", {{"visible", val == "1" || val == "true"}}}});
        } else if (cmd == "logo_time_anim") {
            mergeState(Json{{"text", {{"inDuration", num(val)}, {"outDuration", num(val)}}}});
        } else if (cmd == "logo_time_hold") {
            mergeState(Json{{"text", {{"auto", {{"hold", num(val)}, {"enabled", true}}}}}});
        } else if (cmd == "logo_time_off") {
            mergeState(Json{{"text", {{"auto", {{"off", num(val)}, {"enabled", true}}}}}});
        }
    }

    bool commonRoute(const net::HttpRequest& rq, net::HttpResponse& rs)
    {
        const std::string& path = rq.path;
        const auto q = net::parseForm(rq.query);
        auto ok = [&] { rs.json("{\"status\":\"ok\"}"); };
        if (path == "/api/screen") {
            std::string jpeg;
            if (!preview_.jpeg(jpeg)) {
                rs.json("{\"status\":\"no_frame\"}");
                return true;
            }
            rs.status = 200;
            rs.contentType = "image/jpeg";
            rs.body = std::move(jpeg);
            return true;
        }
        if (path == "/api/reload") {
            std::lock_guard lk(mutex_);
            pendingReload_ = true;
            ok();
            return true;
        }
        if (path == "/api/click") {
            const auto x = q.find("x"), y = q.find("y");
            if (x != q.end() && y != q.end()) {
                std::lock_guard lk(mutex_);
                pendingClicks_.emplace_back(static_cast<float>(std::atof(x->second.c_str())), static_cast<float>(std::atof(y->second.c_str())));
            }
            ok();
            return true;
        }
        const char* videos = "document.querySelectorAll('video').forEach(function(v){";
        if (path == "/api/play") {
            queueJs(std::string(videos) + "v.play();});");
            ok();
            return true;
        }
        if (path == "/api/pause") {
            queueJs(std::string(videos) + "v.pause();});");
            ok();
            return true;
        }
        if (path == "/api/restart") {
            queueJs(std::string(videos) + "v.currentTime=0;v.play();});");
            ok();
            return true;
        }
        if (path == "/api/mute") {
            queueJs(std::string(videos) + "v.muted=!v.muted;});");
            ok();
            return true;
        }
        if (path == "/api/volume") {
            const auto v = q.find("val");
            const double vol = v == q.end() ? 1.0 : std::clamp(std::atof(v->second.c_str()), 0.0, 1.0);
            queueJs(std::string(videos) + "v.volume=" + std::to_string(vol) + ";});");
            ok();
            return true;
        }
        return false;
    }

    void handle(const net::HttpRequest& rq, net::HttpResponse& rs)
    {
        const std::string& path = rq.path;
        const bool get = rq.method == "GET";
        if (get && (path == "/" || path == "/index.html" || path.rfind("/control", 0) == 0 || path.rfind("/index", 0) == 0)) {
            serveFile(assetFile(L"web_bg", L"control.html"), "text/html; charset=utf-8", rs);
            return;
        }
        if (get && path.rfind("/app", 0) == 0) {
            serveFile(assetFile(L"web_bg", L"index.html"), "text/html; charset=utf-8", rs);
            return;
        }
        if (path == "/api/state") {
            Json out = Json::object();
            {
                std::lock_guard lk(mutex_);
                out["rev"] = rev_.load();
                out["logoRev"] = logoRev_.load();
                out["state"] = snapshotState_.is_object() ? snapshotState_ : Json();
            }
            rs.json(out.dump());
            return;
        }
        if (path == "/api/logo") {
            std::lock_guard lk(mutex_);
            rs.text(snapshotLogo_);
            return;
        }
        if (path == "/api/action") {
            auto form = net::parseForm(rq.query);
            if (rq.method == "POST")
                for (auto& [k, v] : net::parseForm(rq.body))
                    form[k] = v;
            legacyAction(form["cmd"], form["val"], form["param"]);
            rs.json("{\"status\":\"ok\",\"rev\":" + std::to_string(rev_.load()) + "}");
            return;
        }
        if (commonRoute(rq, rs))
            return;
        rs.json("{\"status\":\"not_found\"}");
        return;
    }

    std::vector<std::pair<std::string, std::string>> sticky_;
    std::vector<WorkerJson> outbox_;
    std::string lastStateStr_, lastLogoPath_;
    bool pendingModeLogo_ = false;
    bool logoFromUser_ = true;
    bool remoteWanted_ = false;
    bool panelWanted_ = false;
    int remotePort_ = 0;
    std::chrono::steady_clock::time_point lastTry_{};

    mutable std::mutex mutex_;
    Json pendingState_;
    std::vector<std::string> pendingJs_;
    std::vector<std::pair<float, float>> pendingClicks_;
    std::string pendingLogo_;
    bool hasPendingLogo_ = false;
    bool pendingReload_ = false;
    Json snapshotState_;
    std::string snapshotLogo_;
    std::atomic<std::uint64_t> rev_{1}, logoRev_{0};
    std::string remoteError_;
    bool serverRunning_ = false;
    bool serverLoopback_ = true;
    int remotePortLive_ = 0;

    net::HttpServer server_;
    PreviewCapture preview_;
};

void addRemoteParams(std::vector<ParamDesc>& d, int port)
{
    d.push_back(makeBool("remote", "Controle remoto pela rede", false, "Controle remoto"));
    d.back().tooltip = "Abre o painel de controle web para os outros aparelhos da rede (tablet, celular). Desligado por "
                       "padrão: nada fica aberto na rede. O botão \"Abrir painel de controle\" funciona sem isto e só "
                       "atende neste computador (127.0.0.1)";
    d.push_back(makeInt("remotePort", "Porta", port, 1024, 65535, "%d", "Controle remoto"));
    d.back().tooltip = "Porta HTTP do painel (a do plugin original era " + std::to_string(port) +
                       "). O Windows pode pedir permissão de rede na primeira vez que a rede é aberta";
}

void addPageParams(std::vector<ParamDesc>& d)
{
    d.push_back(makeInt("width", "Largura (0 = canvas)", 0, 0, 8192, "%d px", "Página"));
    d.back().tooltip = "Tamanho em que a página é desenhada; 0 acompanha o canvas";
    d.push_back(makeInt("height", "Altura (0 = canvas)", 0, 0, 8192, "%d px", "Página"));
    d.back().tooltip = d[d.size() - 2].tooltip;
    d.push_back(makeEnum("fps", "FPS", {{"30", "30 fps"}, {"60", "60 fps"}}, "60", "Página"));
    d.back().tooltip = "Quadros por segundo da página (o plugin original usava 60)";
    {
        d.push_back(makeBool("transparent", "Fundo transparente", false, "Página"));
        d.back().tooltip = "Fundo da página transparente (o plugin original desenhava sobre preto). Útil com o fundo \"nenhum\", texto ou logo";
    }
}

}

std::vector<ParamDesc> describeWebBackgrounds(const ParamValues& values)
{
    std::vector<ParamDesc> d = describeStudioParams(values);
    addRemoteParams(d, 8081);
    addPageParams(d);
    return d;
}

std::unique_ptr<media::ISource> makeWebBackgrounds(int canvasW, int canvasH)
{
    return std::make_unique<BslWebSource>(canvasW, canvasH);
}

bool bslWebAvailable()
{
    return media::WebSource::available();
}

}
