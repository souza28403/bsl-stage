#include "media/NdiSource.h"

#include "app/Log.h"
#include "media/Frame.h"
#include "media/YuvConverter.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiRuntime.h"
#include "render/Fbo.h"
#include "render/GlStateGuard.h"
#include "render/Shader.h"

#include <algorithm>
#include <thread>

namespace bsl::media {

using Clock = std::chrono::steady_clock;

struct NdiSource::Recv
{
    const NDIlib_v5_t* api = nullptr;
    NDIlib_recv_instance_t inst = nullptr;
};

namespace {

constexpr auto kSilenceLimit = std::chrono::seconds(3);
constexpr auto kReconnectInterval = std::chrono::seconds(10);

const char* const kVertex = R"(#version 330 core
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// UYVY (bytes U Y0 V Y1) visto como textura RGBA de metade da largura -> RGBA. Linha 0 = topo da imagem (v=0),
// como no resto das fontes. A crominância é interpolada entre os pares vizinhos (amostra alinhada à esquerda).
// Faixa limitada e BT.709 em qualquer altura: é a matriz que o decodificador do runtime do NDI usa
// (conferido no bslstage_ndittest, de 32 a 1080 linhas).
const char* const kFragment = R"(#version 330 core
out vec4 fragColor;
uniform sampler2D uPacked;
uniform ivec2 uSize;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 t = texelFetch(uPacked, ivec2(p.x >> 1, p.y), 0);
    float y = ((p.x & 1) == 0) ? t.g : t.a;
    vec2 uv = vec2((float(p.x) + 1.0) / float(uSize.x), (float(p.y) + 0.5) / float(uSize.y));
    vec4 c = texture(uPacked, uv);
    y = (y - 16.0 / 255.0) * (255.0 / 219.0);
    float cb = (c.r - 128.0 / 255.0) * (255.0 / 224.0);
    float cr = (c.b - 128.0 / 255.0) * (255.0 / 224.0);
    vec3 rgb = vec3(y + 1.5748 * cr, y - 0.187324 * cb - 0.468124 * cr, y + 1.8556 * cb);
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

void setupTexture(GLuint tex, GLint filter)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Sobe 'rows' linhas de 'rowBytes' bytes (stride arbitrário) para a textura RGBA8 já vinculada. GL_UNPACK_ROW_LENGTH
// só aceita múltiplos de 4 bytes aqui; o resto cai para linha a linha.
void uploadRows(GLuint tex, int& texW, int& texH, int w, int h, GLenum format, const unsigned char* data, int stride)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool resize = texW != w || texH != h;
    if (resize) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, format, GL_UNSIGNED_BYTE, nullptr);
        texW = w;
        texH = h;
    }
    if (stride % 4 == 0) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, data);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        for (int y = 0; y < h; ++y)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, w, 1, format, GL_UNSIGNED_BYTE, data + static_cast<size_t>(y) * static_cast<size_t>(stride));
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

// Destruir o receptor pode demorar (espera as threads do NDI): numa thread solta, para não travar a UI
void retire(const NDIlib_v5_t* api, NDIlib_recv_instance_t inst)
{
    if (!api || !inst)
        return;
    std::thread([api, inst] { api->recv_destroy(inst); }).detach();
}

std::string shortName(std::string name, size_t maxChars)
{
    if (name.size() <= maxChars)
        return name;
    size_t cut = maxChars - 1;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80)
        --cut;
    return name.substr(0, cut) + "…";
}

} // namespace

NdiSource::NdiSource(ndi::Runtime* runtime) : runtime_(runtime) {}

NdiSource::~NdiSource()
{
    close();
}

std::vector<ParamDesc> NdiSource::describe(const ParamValues& /*values*/)
{
    using namespace param;
    std::vector<ParamDesc> d;
    d.push_back(makeCustom("source", "Fonte NDI", "ndi-source", std::string(), "Entrada"));
    d.push_back(makeBool("lowLatency", "Baixa latência", false));
    d.back().tooltip = "Mostra sempre o quadro mais novo e descarta os atrasados. Menos atraso, movimento um pouco menos suave.";
    d.push_back(makeBool("lowBandwidth", "Banda baixa", false));
    d.back().tooltip = "Largura de banda baixa: pede ao emissor o fluxo de baixa qualidade (proxy). Ajuda em PC fraco ou rede lenta.";
    return d;
}

std::string NdiSource::label(const ParamValues& values)
{
    std::string name;
    if (const Json* v = values.find("source"); v && v->is_string())
        name = v->get<std::string>();
    if (name.empty())
        return "NDI";
    return "NDI: " + shortName(name, 28);
}

GLuint NdiSource::texture() const
{
    return (usingFbo_ && fbo_) ? fbo_->texture() : rgbaTex_;
}

bool NdiSource::open(const std::filesystem::path& /*path*/, std::string* /*error*/)
{
    close();
    clock_ = ClipClock{};
    open_ = true;
    paramsDirty_ = false;
    frames_ = 0;
    everLive_ = false;
    showBlack();
    connect();
    return true;
}

void NdiSource::disconnect()
{
    if (recv_) {
        retire(recv_->api, recv_->inst);
        recv_.reset();
    }
}

void NdiSource::close()
{
    disconnect();
    fbo_.reset();
    shader_.reset();
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    for (GLuint* t : {&rgbaTex_, &packedTex_})
        if (*t) {
            glDeleteTextures(1, t);
            *t = 0;
        }
    rgbaW_ = rgbaH_ = packedW_ = packedH_ = 0;
    usingFbo_ = false;
    open_ = false;
}

void NdiSource::showBlack()
{
    Frame black;
    black.allocate(width_, height_, PixelFormat::RGBA8);
    for (size_t i = 3; i < black.planes[0].size(); i += 4)
        black.planes[0][i] = 255;
    uploadRgbaTexture(rgbaTex_, rgbaW_, rgbaH_, black);
    usingFbo_ = false;
    hasAlpha_ = false;
}

void NdiSource::connect()
{
    disconnect();
    const ParamSet p = currentParams();
    sourceName_ = p.s("source");
    lowBandwidth_ = p.b("lowBandwidth");
    lowLatency_ = p.b("lowLatency");
    info_.clear();
    const auto now = Clock::now();
    connectedAt_ = lastFrame_ = lastCheck_ = now;

    ndi::Runtime& rt = runtime_ ? *runtime_ : ndi::Runtime::instance();
    if (!rt.available()) {
        state_ = State::NoRuntime;
        return;
    }
    if (!runtime_)
        ndi::Finder::shared().start(); // deixa a lista de fontes pronta para o seletor
    if (sourceName_.empty()) {
        state_ = State::NoSource;
        return;
    }

    NDIlib_recv_create_v3_t c{};
    c.source_to_connect_to.p_ndi_name = sourceName_.c_str();
    c.source_to_connect_to.p_url_address = nullptr;
    c.color_format = NDIlib_recv_color_format_UYVY_BGRA; // UYVY quando não há alpha: metade da banda da CPU
    c.bandwidth = lowBandwidth_ ? NDIlib_recv_bandwidth_lowest : NDIlib_recv_bandwidth_highest;
    c.allow_video_fields = false;
    c.p_ndi_recv_name = "BSL Stage";
    NDIlib_recv_instance_t inst = rt.api()->recv_create_v3(&c);
    if (!inst) {
        state_ = State::NoSignal;
        log::warn("NDI: não foi possível criar o receptor para \"" + sourceName_ + "\"");
        return;
    }
    recv_ = std::make_unique<Recv>();
    recv_->api = rt.api();
    recv_->inst = inst;
    state_ = everLive_ ? State::NoSignal : State::Connecting;
    log::info("NDI: conectando a \"" + sourceName_ + "\"");
}

void NdiSource::presentPacked(const unsigned char* data, int stride, int w, int h, unsigned int format, bool opaque)
{
    GlStateGuard guard;
    if (!rgbaTex_) {
        glGenTextures(1, &rgbaTex_);
        setupTexture(rgbaTex_, GL_LINEAR);
        rgbaW_ = rgbaH_ = 0;
    }
    uploadRows(rgbaTex_, rgbaW_, rgbaH_, w, h, format, data, stride);
    // Formatos "X" (BGRX/RGBX): o canal alfa não vale nada, vira opaco
    if (opaque != opaqueSwizzle_) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, opaque ? GL_ONE : GL_ALPHA);
        opaqueSwizzle_ = opaque;
    }
    usingFbo_ = false;
    hasAlpha_ = !opaque;
    width_ = w;
    height_ = h;
}

void NdiSource::presentUyvy(const unsigned char* data, int stride, int w, int h)
{
    const int half = w / 2;
    if (half <= 0)
        return;
    GlStateGuard guard;
    if (!packedTex_) {
        glGenTextures(1, &packedTex_);
        setupTexture(packedTex_, GL_LINEAR);
        packedW_ = packedH_ = 0;
    }
    uploadRows(packedTex_, packedW_, packedH_, half, h, GL_RGBA, data, stride);

    if (!shader_) {
        shader_ = std::make_unique<Shader>(kVertex, kFragment);
        glGenVertexArrays(1, &vao_);
    }
    if (!fbo_ || fbo_->width() != w || fbo_->height() != h)
        fbo_ = std::make_unique<Fbo>(w, h);

    fbo_->bind();
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shader_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, packedTex_);
    shader_->setInt("uPacked", 0);
    glUniform2i(glGetUniformLocation(shader_->id(), "uSize"), w, h);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    usingFbo_ = true;
    hasAlpha_ = false;
    width_ = w;
    height_ = h;
}

void NdiSource::update(double /*dt*/)
{
    if (!open_)
        return;
    const auto now = Clock::now();

    if (paramsDirty_) {
        paramsDirty_ = false;
        const ParamSet p = currentParams();
        if (p.s("source") != sourceName_ || p.b("lowBandwidth") != lowBandwidth_) {
            connect();
            showBlack();
        } else {
            lowLatency_ = p.b("lowLatency");
        }
    }

    if (!recv_) {
        // Sem receptor (sem runtime, sem fonte escolhida, ou a criação falhou): tenta de novo a cada 10 s
        if (state_ != State::NoRuntime && state_ != State::NoSource && now - lastFrame_ > kReconnectInterval)
            connect();
        return;
    }

    const NDIlib_v5_t* api = recv_->api;
    const NDIlib_recv_instance_t inst = recv_->inst;

    // Quantos quadros consumir agora: baixa latência = sempre o mais novo; normal = um por vez, a menos que a fila
    // esteja crescendo (app mais lento que a fonte), quando salta para o mais novo
    int limit = 1;
    if (lowLatency_) {
        limit = 16;
    } else if (api->recv_get_queue) {
        NDIlib_recv_queue_t q{};
        api->recv_get_queue(inst, &q);
        if (q.video_frames > 3)
            limit = std::min(q.video_frames, 16);
    }

    NDIlib_video_frame_v2_t keep{};
    bool have = false;
    for (int taken = 0, spins = 0; taken < limit && spins < 32; ++spins) {
        NDIlib_video_frame_v2_t f{};
        const NDIlib_frame_type_e type = api->recv_capture_v3(inst, &f, nullptr, nullptr, 0);
        if (type == NDIlib_frame_type_video) {
            if (have)
                api->recv_free_video_v2(inst, &keep);
            keep = f;
            have = true;
            ++taken;
        } else if (type == NDIlib_frame_type_none || type == NDIlib_frame_type_error) {
            break;
        }
    }

    if (have) {
        const int w = keep.xres, h = keep.yres;
        const char* fmt = "?";
        if (w > 1 && h > 0 && keep.p_data && keep.line_stride_in_bytes > 0) {
            switch (keep.FourCC) {
            case NDIlib_FourCC_video_type_UYVY:
            case NDIlib_FourCC_video_type_UYVA:
                presentUyvy(keep.p_data, keep.line_stride_in_bytes, w & ~1, h);
                fmt = "UYVY";
                break;
            case NDIlib_FourCC_video_type_BGRA:
                presentPacked(keep.p_data, keep.line_stride_in_bytes, w, h, GL_BGRA, false);
                fmt = "BGRA";
                break;
            case NDIlib_FourCC_video_type_BGRX:
                presentPacked(keep.p_data, keep.line_stride_in_bytes, w, h, GL_BGRA, true);
                fmt = "BGRX";
                break;
            case NDIlib_FourCC_video_type_RGBA:
                presentPacked(keep.p_data, keep.line_stride_in_bytes, w, h, GL_RGBA, false);
                fmt = "RGBA";
                break;
            case NDIlib_FourCC_video_type_RGBX:
                presentPacked(keep.p_data, keep.line_stride_in_bytes, w, h, GL_RGBA, true);
                fmt = "RGBX";
                break;
            default:
                break; // formato que não pedimos: ignora o quadro
            }
            if (fmt[0] != '?') {
                const double fps = keep.frame_rate_D > 0 ? static_cast<double>(keep.frame_rate_N) / keep.frame_rate_D : 0.0;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%d x %d @ %.2f fps | %s", w, h, fps, fmt);
                info_ = buf;
                ++frames_;
                lastFrame_ = now;
                everLive_ = true;
                if (state_ != State::Live)
                    log::info("NDI: recebendo \"" + sourceName_ + "\" (" + info_ + ")");
                state_ = State::Live;
            }
        }
        api->recv_free_video_v2(inst, &keep);
    } else if (now - lastFrame_ > kSilenceLimit) {
        if (state_ == State::Live) {
            log::warn("NDI: \"" + sourceName_ + "\" parou de enviar; esperando a fonte voltar");
            showBlack();
            state_ = State::NoSignal;
        }
    }

    // Segurança: muito tempo sem nenhum quadro, refaz o receptor (o NDI normalmente reconecta sozinho)
    if (state_ != State::Live && now - lastFrame_ > kReconnectInterval && now - connectedAt_ > kReconnectInterval) {
        connect();
        return;
    }

    // A cada segundo: a fonte ainda aparece na rede? (só para a mensagem de status)
    if (now - lastCheck_ > std::chrono::seconds(1)) {
        lastCheck_ = now;
        if (state_ != State::Live && !runtime_) {
            bool listed = false;
            for (const ndi::SourceInfo& s : ndi::Finder::shared().sources())
                listed = listed || s.name == sourceName_;
            sourceListed_ = listed;
        }
    }
}

SourceStatus NdiSource::status() const
{
    using Level = SourceStatus::Level;
    switch (state_) {
    case State::NoRuntime: return {Level::Warning, ndi::kMissingText};
    case State::NoSource: return {Level::Info, "Escolha uma fonte NDI"};
    case State::Connecting: return {Level::Info, "Conectando a " + sourceName_ + "…"};
    case State::Live: return {Level::Info, "NDI: " + sourceName_ + " | " + info_};
    case State::NoSignal:
        return {Level::Warning, sourceListed_ ? "Sem sinal (reconectando…)" : "Sem sinal: a fonte não aparece na rede"};
    }
    return {};
}

} // namespace bsl::media
