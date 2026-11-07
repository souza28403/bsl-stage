#include "output/NdiOutput.h"

#include "app/Log.h"
#include "ndi/NdiRuntime.h"
#include "render/Fbo.h"
#include "render/GlStateGuard.h"
#include "render/Shader.h"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace bsl {

namespace {

using Clock = std::chrono::steady_clock;

const char* const kVertex = R"(#version 330 core
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kFragment = R"(#version 330 core
out vec4 fragColor;
uniform sampler2D uSrc;
uniform int uMode;
uniform ivec2 uImage;

vec3 toYcc(vec3 c)
{
    const float kr = 0.2126;
    const float kb = 0.0722;
    float y = kr * c.r + (1.0 - kr - kb) * c.g + kb * c.b;
    float cb = (c.b - y) / (2.0 * (1.0 - kb));
    float cr = (c.r - y) / (2.0 * (1.0 - kr));
    return vec3(16.0 / 255.0 + y * (219.0 / 255.0),
                128.0 / 255.0 + cb * (224.0 / 255.0),
                128.0 / 255.0 + cr * (224.0 / 255.0));
}

void main()
{
    ivec2 f = ivec2(gl_FragCoord.xy);
    int srcRow = uImage.y - 1 - f.y;
    if (uMode == 0) {
        fragColor = texelFetch(uSrc, ivec2(f.x, srcRow), 0);
        return;
    }
    vec3 a = toYcc(clamp(texelFetch(uSrc, ivec2(2 * f.x, srcRow), 0).rgb, 0.0, 1.0));
    vec3 b = toYcc(clamp(texelFetch(uSrc, ivec2(2 * f.x + 1, srcRow), 0).rgb, 0.0, 1.0));
    fragColor = vec4(0.5 * (a.y + b.y), a.x, 0.5 * (a.z + b.z), b.x);
}
)";

void frameRateRational(float fps, int& n, int& d)
{
    if (!(fps > 0.0f))
        fps = 30.0f;
    struct Known { float fps; int n, d; };
    static const Known known[] = {{23.976f, 24000, 1001}, {29.97f, 30000, 1001}, {47.952f, 48000, 1001},
                                  {59.94f, 60000, 1001}, {119.88f, 120000, 1001}};
    for (const Known& k : known)
        if (std::fabs(fps - k.fps) < 0.01f) {
            n = k.n;
            d = k.d;
            return;
        }
    n = static_cast<int>(std::lround(fps * 1000.0f));
    d = 1000;
    const int g = std::gcd(n, d);
    n /= g;
    d /= g;
}

}

struct NdiOutput::Impl
{
    NDIlib_send_instance_t sender = nullptr;
    std::string created;
    std::string actual;

    std::unique_ptr<Shader> shader;
    unsigned int vao = 0;
    std::unique_ptr<Fbo> conv;
    unsigned int pbo[2] = {0, 0};
    bool pending[2] = {false, false};
    int cur = 0;
    int imageW = 0, imageH = 0;
    int outW = 0;
    bool resBgra = false;
    size_t pboBytes = 0;

    std::vector<unsigned char> cpu[2];
    int cpuIndex = 0;

    Clock::time_point due{};
    bool haveDue = false;
    Clock::time_point lastConnCheck{};
};

NdiOutput::NdiOutput(ndi::Runtime* runtime) : impl_(std::make_unique<Impl>()), runtime_(runtime) {}

NdiOutput::~NdiOutput()
{
    release();
}

void NdiOutput::setName(const std::string& name)
{
    if (name == name_)
        return;
    release();
    name_ = name;
}

void NdiOutput::setBgra(bool bgra)
{
    bgra_ = bgra;
}

void NdiOutput::setFrameRate(float fps)
{
    fps_ = fps;
}

bool NdiOutput::runtimeAvailable() const
{
    ndi::Runtime& rt = runtime_ ? *runtime_ : ndi::Runtime::instance();
    return rt.available();
}

std::string NdiOutput::actualName() const
{
    return impl_->actual;
}

void NdiOutput::release()
{
    Impl& s = *impl_;
    if (s.sender) {
        const NDIlib_v5_t* api = (runtime_ ? *runtime_ : ndi::Runtime::instance()).api();
        if (api) {
            api->send_send_video_async_v2(s.sender, nullptr);
            api->send_destroy(s.sender);
        }
    }
    s.sender = nullptr;
    s.actual.clear();
    s.created.clear();
    for (unsigned int& b : s.pbo)
        if (b) {
            glDeleteBuffers(1, &b);
            b = 0;
        }
    s.pending[0] = s.pending[1] = false;
    s.conv.reset();
    s.shader.reset();
    if (s.vao) {
        glDeleteVertexArrays(1, &s.vao);
        s.vao = 0;
    }
    s.imageW = s.imageH = s.outW = 0;
    s.pboBytes = 0;
    s.haveDue = false;
    connections_ = 0;
    framesSent_ = 0;
}

bool NdiOutput::send(unsigned int texture, int width, int height)
{
    Impl& s = *impl_;
    ndi::Runtime& rt = runtime_ ? *runtime_ : ndi::Runtime::instance();
    if (!rt.available() || texture == 0 || width < 2 || height <= 0)
        return false;
    const NDIlib_v5_t* api = rt.api();

    if (!s.sender) {
        NDIlib_send_create_t c{};
        s.created = name_.empty() ? "BSL Stage" : name_;
        c.p_ndi_name = s.created.c_str();
        c.p_groups = nullptr;
        c.clock_video = false;
        c.clock_audio = false;
        s.sender = api->send_create(&c);
        if (!s.sender)
            return false;
        const NDIlib_source_t* src = api->send_get_source_name ? api->send_get_source_name(s.sender) : nullptr;
        s.actual = (src && src->p_ndi_name) ? src->p_ndi_name : s.created;
        log::info("NDI: sender \"" + s.actual + "\" aberto");
    }

    connections_ = api->send_get_no_connections(s.sender, 0);
    if (connections_ <= 0) {
        s.pending[0] = s.pending[1] = false;
        return true;
    }

    const auto now = Clock::now();
    const double fps = fps_ > 0.0f ? static_cast<double>(fps_) : 30.0;
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
    if (!s.haveDue) {
        s.haveDue = true;
        s.due = now;
    }
    if (now + period * 15 / 100 < s.due)
        return true;
    s.due += period;
    if (s.due < now - period)
        s.due = now;

    GlStateGuard guard;

    const int imageW = bgra_ ? width : (width & ~1);
    const int outW = bgra_ ? imageW : imageW / 2;
    if (!s.conv || s.imageW != imageW || s.imageH != height || s.resBgra != bgra_) {
        try {
            s.conv = std::make_unique<Fbo>(outW, height);
        } catch (const std::exception& e) {
            log::warn(std::string("NDI: FBO de conversão: ") + e.what());
            s.conv.reset();
            return false;
        }
        s.imageW = imageW;
        s.imageH = height;
        s.outW = outW;
        s.resBgra = bgra_;
        s.pboBytes = static_cast<size_t>(outW) * static_cast<size_t>(height) * 4;
        for (unsigned int& b : s.pbo) {
            if (!b)
                glGenBuffers(1, &b);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, b);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(s.pboBytes), nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        s.pending[0] = s.pending[1] = false;
        for (auto& v : s.cpu)
            v.assign(s.pboBytes, 0);
    }
    if (!s.shader) {
        try {
            s.shader = std::make_unique<Shader>(kVertex, kFragment);
        } catch (const std::exception& e) {
            log::warn(std::string("NDI: shader de conversão: ") + e.what());
            return false;
        }
        glGenVertexArrays(1, &s.vao);
    }

    s.conv->bind();
    glViewport(0, 0, outW, height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    s.shader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    s.shader->setInt("uSrc", 0);
    s.shader->setInt("uMode", bgra_ ? 0 : 1);
    glUniform2i(glGetUniformLocation(s.shader->id(), "uImage"), imageW, height);
    glBindVertexArray(s.vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo[s.cur]);
    glReadPixels(0, 0, outW, height, bgra_ ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    s.pending[s.cur] = true;

    const int prev = s.cur ^ 1;
    if (s.pending[prev]) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo[prev]);
        void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(s.pboBytes), GL_MAP_READ_BIT);
        if (mapped) {
            std::vector<unsigned char>& buf = s.cpu[s.cpuIndex];
            std::memcpy(buf.data(), mapped, s.pboBytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

            NDIlib_video_frame_v2_t frame{};
            frame.xres = imageW;
            frame.yres = height;
            frame.FourCC = bgra_ ? NDIlib_FourCC_video_type_BGRA : NDIlib_FourCC_video_type_UYVY;
            frameRateRational(fps_, frame.frame_rate_N, frame.frame_rate_D);
            frame.picture_aspect_ratio = static_cast<float>(imageW) / static_cast<float>(height);
            frame.frame_format_type = NDIlib_frame_format_type_progressive;
            frame.timecode = NDIlib_send_timecode_synthesize;
            frame.p_data = buf.data();
            frame.line_stride_in_bytes = outW * 4;
            api->send_send_video_async_v2(s.sender, &frame);
            s.cpuIndex ^= 1;
            ++framesSent_;
        }
        s.pending[prev] = false;
    }
    s.cur ^= 1;
    return true;
}

}
