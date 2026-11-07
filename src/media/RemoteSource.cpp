#include "media/RemoteSource.h"

#include "app/Log.h"
#include "render/D3D11Interop.h"

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace bsl::media {

namespace {

std::mutex g_forceMutex;
std::string g_forcedTransport = "auto";

std::string forcedTransport()
{
    std::lock_guard lk(g_forceMutex);
    return g_forcedTransport;
}

}

void RemoteSource::forceTransport(const std::string& transport)
{
    std::lock_guard lk(g_forceMutex);
    g_forcedTransport = transport;
}

RemoteSource::RemoteSource(int canvasWidth, int canvasHeight)
{
    canvasW_ = canvasWidth;
    canvasH_ = canvasHeight;
    width_ = canvasWidth;
    height_ = canvasHeight;
}

RemoteSource::~RemoteSource()
{
    RemoteSource::close();
}

WorkerJson RemoteSource::fullOpen() const
{
    WorkerJson j = buildOpenBody();
    int w = canvasW_, h = canvasH_;
    outputSize(w, h);
    j["kind"] = kindName();
    j["w"] = w;
    j["h"] = h;
    return j;
}

bool RemoteSource::open(const std::filesystem::path& file, std::string* error)
{
    close();
    std::error_code ec;
    path_ = file.empty() ? file : std::filesystem::absolute(file, ec);
    if (!path_.empty() && !std::filesystem::is_regular_file(path_, ec)) {
        if (error)
            *error = "arquivo não encontrado";
        return false;
    }
    clock_ = ClipClock{};

    std::string interopError;
    const std::string transport = forcedTransport();
    if (transport != "shm") {
        interop_ = render::D3D11Interop::shared(&interopError);
        if (!interop_)
            log::info("Fonte remota: sem D3D11 (" + interopError + "); usando memória compartilhada");
    }
    onOpened();
    host_.start(fullOpen(), interop_, transport);
    open_ = true;
    return true;
}

void RemoteSource::close()
{
    host_.stop();
    interopTex_.reset();
    interop_.reset();
    if (glTex_) {
        glDeleteTextures(1, &glTex_);
        glTex_ = 0;
    }
    glTexW_ = glTexH_ = 0;
    activeTex_ = 0;
    lastSeq_ = 0;
    anyFrame_ = false;
    open_ = false;
    shownPage_ = 0;
    lastError_.clear();
}

void RemoteSource::update(double )
{
    if (!open_)
        return;
    for (const WorkerJson& ev : host_.drainEvents()) {
        if (ev.value("t", std::string()) == "error")
            lastError_ = ev.value("msg", std::string());
        onEvent(ev);
    }
    host_.acquireNewFrame(lastSeq_, [this](const FrameAccess& fa) { applyFrame(fa); });
}

void RemoteSource::applyFrame(const FrameAccess& fa)
{
    FrameInfo info;
    info.page = fa.page;
    info.width = fa.width;
    info.height = fa.height;
    info.first = !anyFrame_;

    if (fa.kind == FrameAccess::Kind::Texture) {
        if (!interop_)
            return;
        beforeFrame(info);
        if (!interopTex_ || interopTex_->width() != fa.width || interopTex_->height() != fa.height) {
            interopTex_ = interop_->createTexture(fa.width, fa.height, false, DXGI_FORMAT_B8G8R8A8_UNORM, true);
            if (!interopTex_)
                return;
        }
        interopTex_->beginD3D();
        fa.context->CopyResource(interopTex_->d3d(), fa.mailbox);
        interopTex_->endD3D();
        activeTex_ = interopTex_->gl();
    } else {
        auto* hdr = const_cast<wp::ShmHeader*>(fa.shmHeader);
        const LONG latest = hdr->latest;
        if (latest < 0 || latest > 1)
            return;
        const int idx = static_cast<int>(latest);
        const LONG64 s1 = ReadAcquire64(&hdr->seq[idx]);
        if (s1 & 1) {
            lastSeq_ = fa.seq - 1;
            return;
        }
        beforeFrame(info);
        if (!glTex_ || glTexW_ != fa.width || glTexH_ != fa.height) {
            GLint prev = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
            if (!glTex_)
                glGenTextures(1, &glTex_);
            glBindTexture(GL_TEXTURE_2D, glTex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fa.width, fa.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexW_ = fa.width;
            glTexH_ = fa.height;
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
        }
        GLint prevTex = 0, prevAlign = 0, prevRow = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow);
        glBindTexture(GL_TEXTURE_2D, glTex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fa.width, fa.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        wp::shmBuffer(const_cast<void*>(fa.shmBase), fa.width, fa.height, idx));
        glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
        const LONG64 s2 = ReadAcquire64(&hdr->seq[idx]);
        if (s1 != s2)
            lastSeq_ = fa.seq - 1;
        activeTex_ = glTex_;
    }
    width_ = fa.width;
    height_ = fa.height;
    shownPage_ = fa.page;
    anyFrame_ = true;
    ++framesShown_;
    afterFrame(info);
}

SourceStatus RemoteSource::status() const
{
    const HostSnapshot s = host_.snapshot();
    SourceStatus st;
    switch (s.state) {
    case HostState::Restarting:
    case HostState::Failed:
        st.level = SourceStatus::Level::Warning;
        st.text = s.text;
        return st;
    case HostState::Starting:
        if (!anyFrame_) {
            st.level = SourceStatus::Level::Info;
            st.text = s.text;
        }
        return st;
    default:
        break;
    }
    if (!s.text.empty()) {
        st.level = s.warning ? SourceStatus::Level::Warning : SourceStatus::Level::Info;
        st.text = s.text;
    } else if (!lastError_.empty()) {
        st.level = SourceStatus::Level::Warning;
        st.text = lastError_;
    }
    return st;
}

std::string RemoteSource::transportName() const
{
    return host_.snapshot().transport;
}

std::string RemoteSource::transportReason() const
{
    return host_.snapshot().transportWhy;
}

unsigned long RemoteSource::workerPid() const
{
    return host_.snapshot().pid;
}

void RemoteSource::sendMouse(const char* type, int x, int y, int button, int clicks, int mods)
{
    sendToWorker(WorkerJson{{"t", "mouse"}, {"type", type}, {"x", x}, {"y", y}, {"button", button}, {"clicks", clicks},
                            {"mods", mods}});
}

void RemoteSource::sendWheel(int x, int y, float dx, float dy)
{
    sendToWorker(WorkerJson{{"t", "wheel"}, {"x", x}, {"y", y}, {"dx", dx}, {"dy", dy}});
}

void RemoteSource::sendKey(const char* type, int code, unsigned ch, int mods)
{
    sendToWorker(WorkerJson{{"t", "key"}, {"type", type}, {"code", code}, {"ch", ch}, {"mods", mods}});
}

}
