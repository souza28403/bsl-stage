#include "media/TextSource.h"

#include "app/Log.h"
#include "media/FontCatalog.h"
#include "render/Fbo.h"
#include "render/Shader.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bsl::media {

namespace {

const char* const kVertexSource = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kFragmentSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uStrip;
uniform vec2 uOut;
uniform vec2 uStripSize;
uniform vec2 uOffset;
void main()
{
    vec2 px = vUv * uOut + uOffset;
    vec4 c = texture(uStrip, px / uStripSize);
    vec3 rgb = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
    fragColor = vec4(rgb, c.a);
}
)";

const std::vector<ParamOption> kHAlign = {{"left", "Esquerda"}, {"center", "Centro"}, {"right", "Direita"}};
const std::vector<ParamOption> kVAlign = {{"top", "Topo"}, {"middle", "Meio"}, {"bottom", "Base"}};
const std::vector<ParamOption> kMarquee = {
    {"none", "Desligado"}, {"horizontal", "Horizontal"}, {"vertical", "Vertical"}};
const std::vector<ParamOption> kBgMode = {{"box", "Caixa ajustada ao texto"}, {"full", "Tela cheia"}};

TextHAlign hAlignOf(const std::string& s)
{
    return s == "left" ? TextHAlign::Left : (s == "right" ? TextHAlign::Right : TextHAlign::Center);
}
TextVAlign vAlignOf(const std::string& s)
{
    return s == "top" ? TextVAlign::Top : (s == "bottom" ? TextVAlign::Bottom : TextVAlign::Middle);
}
TextMarquee marqueeOf(const std::string& s)
{
    return s == "horizontal" ? TextMarquee::Horizontal : (s == "vertical" ? TextMarquee::Vertical : TextMarquee::None);
}

}

TextSource::TextSource(int width, int height) : width_(width), height_(height) {}

TextSource::~TextSource()
{
    close();
}

std::vector<ParamDesc> TextSource::describe(const ParamValues&)
{
    using namespace param;
    static const std::string defaultFont = FontCatalog::instance().defaultFile();

    std::vector<ParamDesc> d;
    d.push_back(makeText("text", "Texto", "Seu texto aqui", true, "Conteúdo"));
    d.push_back(makeFont("font", "Fonte", defaultFont));
    d.push_back(makeInt("size", "Tamanho", 96, 8, 600, "%d px"));
    d.push_back(makeColor("color", "Cor", {1.0f, 1.0f, 1.0f, 1.0f}));
    d.push_back(makeEnum("hAlign", "Alinh. horizontal", kHAlign, "center"));
    d.push_back(makeEnum("vAlign", "Alinh. vertical", kVAlign, "middle"));
    d.push_back(makeInt("margin", "Margem", 40, 0, 500, "%d px"));
    d.push_back(makeBool("wrap", "Quebra automática", true));
    d.push_back(makeFloat("lineSpacing", "Entrelinha", 1.0f, 0.5f, 3.0f, "%.2fx"));

    d.push_back(makeBool("outline", "Contorno", false, "Estilo"));
    d.push_back(makeColor("outlineColor", "Cor do contorno", {0.0f, 0.0f, 0.0f, 1.0f}));
    d.back().enabledIf = "outline";
    d.push_back(makeFloat("outlineWidth", "Espessura", 4.0f, 0.5f, 40.0f, "%.1f px"));
    d.back().enabledIf = "outline";

    d.push_back(makeBool("shadow", "Sombra", false));
    d.push_back(makeColor("shadowColor", "Cor da sombra", {0.0f, 0.0f, 0.0f, 0.7f}));
    d.back().enabledIf = "shadow";
    d.push_back(makeInt("shadowX", "Sombra X", 4, -100, 100, "%d px"));
    d.back().enabledIf = "shadow";
    d.push_back(makeInt("shadowY", "Sombra Y", 4, -100, 100, "%d px"));
    d.back().enabledIf = "shadow";
    d.push_back(makeInt("shadowBlur", "Suavização", 4, 0, 40, "%d px"));
    d.back().enabledIf = "shadow";

    d.push_back(makeBool("bg", "Fundo", false));
    d.push_back(makeColor("bgColor", "Cor do fundo", {0.0f, 0.0f, 0.0f, 1.0f}));
    d.back().enabledIf = "bg";
    d.push_back(makeFloat("bgOpacity", "Opacidade do fundo", 0.6f, 0.0f, 1.0f, "%.2f"));
    d.back().enabledIf = "bg";
    d.push_back(makeEnum("bgMode", "Tipo de fundo", kBgMode, "box"));
    d.back().enabledIf = "bg";
    d.push_back(makeInt("bgPadding", "Folga da caixa", 24, 0, 300, "%d px"));
    d.back().enabledIf = "bg";

    d.push_back(makeEnum("marquee", "Letreiro", kMarquee, "none", "Letreiro"));
    d.push_back(makeFloat("marqueeSpeed", "Velocidade", 200.0f, -1500.0f, 1500.0f, "%.0f px/s"));
    d.back().enabledIf = "marquee!=none";
    d.back().tooltip = "Positivo: o texto anda para a esquerda (horizontal) ou para cima (vertical). Negativo inverte.";
    d.push_back(makeInt("marqueeGap", "Espaço entre repetições", 0, 0, 5000, "%d px"));
    d.back().enabledIf = "marquee!=none";
    d.back().tooltip = "0 = o tamanho da tela: o texto some por completo antes de voltar.";
    return d;
}

std::string TextSource::label(const ParamValues& values)
{
    const ParamSet p(describe(values), values);
    std::string t = p.s("text");
    const size_t nl = t.find('\n');
    if (nl != std::string::npos)
        t.resize(nl);
    constexpr size_t kMax = 18;
    size_t count = 0, i = 0;
    while (i < t.size() && count < kMax) {
        ++i;
        while (i < t.size() && (static_cast<unsigned char>(t[i]) & 0xC0) == 0x80)
            ++i;
        ++count;
    }
    const bool cut = i < t.size();
    t.resize(i);
    if (t.empty())
        return "Texto";
    return "Texto: " + t + (cut ? "…" : "");
}

bool TextSource::open(const std::filesystem::path& , std::string* error)
{
    close();
    try {
        fbo_ = std::make_unique<Fbo>(width_, height_);
        shader_ = std::make_unique<Shader>(kVertexSource, kFragmentSource);
    } catch (const std::exception& e) {
        fbo_.reset();
        shader_.reset();
        if (error)
            *error = e.what();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    open_ = true;
    dirty_ = true;
    stripW_ = stripH_ = period_ = 0;

    const LoopMode keepMode = clock_.loop;
    const double keepSpeed = clock_.speed;
    clock_ = ClipClock{};
    clock_.loop = keepMode;
    clock_.speed = keepSpeed;

    rebuild();
    return true;
}

void TextSource::close()
{
    if (strip_) {
        glDeleteTextures(1, &strip_);
        strip_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.reset();
    fbo_.reset();
    open_ = false;
    stripW_ = stripH_ = period_ = 0;
}

GLuint TextSource::texture() const
{
    return fbo_ ? fbo_->texture() : 0;
}

SourceStatus TextSource::status() const
{
    if (fontMissing_)
        return {SourceStatus::Level::Warning, "Fonte não encontrada: " + fontName_ + " (nenhuma fonte substituta disponível)"};
    if (fontFallback_)
        return {SourceStatus::Level::Warning, "Fonte não encontrada: " + fontName_ + " (usando uma substituta)"};
    if (truncated_)
        return {SourceStatus::Level::Info, "Texto longo demais para o letreiro: o final foi cortado."};
    return {};
}

void TextSource::resetScroll()
{
    const int canvasLen = marquee_ == TextMarquee::Vertical ? height_ : width_;
    scroll_ = 0.0;
    if (period_ > canvasLen)
        scroll_ = static_cast<double>(period_ - canvasLen);
    needsDraw_ = true;
}

void TextSource::onSeek(double )
{
    resetScroll();
}

void TextSource::rebuild()
{
    dirty_ = false;
    const ParamSet p = currentParams();

    TextStyle st;
    st.text = p.s("text");
    st.sizePx = static_cast<float>(p.i("size"));
    st.color = p.c("color");
    st.hAlign = hAlignOf(p.s("hAlign"));
    st.vAlign = vAlignOf(p.s("vAlign"));
    st.margin = p.i("margin");
    st.wrap = p.b("wrap");
    st.lineSpacing = p.f("lineSpacing");
    st.outline = p.b("outline");
    st.outlineColor = p.c("outlineColor");
    st.outlineWidth = p.f("outlineWidth");
    st.shadow = p.b("shadow");
    st.shadowColor = p.c("shadowColor");
    st.shadowX = p.i("shadowX");
    st.shadowY = p.i("shadowY");
    st.shadowBlur = p.i("shadowBlur");
    st.background = p.b("bg");
    st.bgColor = p.c("bgColor");
    st.bgOpacity = p.f("bgOpacity");
    st.bgFullScreen = p.s("bgMode") == "full";
    st.bgPadding = p.i("bgPadding");
    st.marquee = marqueeOf(p.s("marquee"));
    st.marqueeGap = p.i("marqueeGap");

    fontName_ = p.s("font");
    st.fontPath = FontCatalog::instance().resolve(fontName_);
    fontFallback_ = false;
    fontMissing_ = false;
    if (st.fontPath.empty()) {
        st.fontPath = FontCatalog::instance().resolve(FontCatalog::instance().defaultFile());
        fontFallback_ = !st.fontPath.empty();
        fontMissing_ = st.fontPath.empty();
    }

    GLint maxTex = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);

    TextBitmap bmp;
    std::string error;
    if (!rasterizeText(st, width_, height_, maxTex, bmp, &error))
        log::warn("Texto: " + error + " (" + fontName_ + ")");
    ++rasterCount_;
    truncated_ = bmp.truncated;

    for (size_t i = 0; i + 3 < bmp.rgba.size(); i += 4) {
        const unsigned a = bmp.rgba[i + 3];
        if (a == 255)
            continue;
        bmp.rgba[i] = static_cast<unsigned char>((bmp.rgba[i] * a + 127) / 255);
        bmp.rgba[i + 1] = static_cast<unsigned char>((bmp.rgba[i + 1] * a + 127) / 255);
        bmp.rgba[i + 2] = static_cast<unsigned char>((bmp.rgba[i + 2] * a + 127) / 255);
    }

    GLint prevTex = 0, prevAlign = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    if (!strip_) {
        glGenTextures(1, &strip_);
        glBindTexture(GL_TEXTURE_2D, strip_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glBindTexture(GL_TEXTURE_2D, strip_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bmp.width, bmp.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bmp.rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    stripW_ = bmp.width;
    stripH_ = bmp.height;

    const TextMarquee oldMarquee = marquee_;
    const int oldPeriod = period_;
    marquee_ = st.marquee;
    period_ = bmp.period;
    speed_ = p.f("marqueeSpeed");
    if (marquee_ != oldMarquee || period_ != oldPeriod)
        resetScroll();
    else if (period_ > 0)
        scroll_ = std::fmod(scroll_, static_cast<double>(period_));
    needsDraw_ = true;
    render();
}

void TextSource::update(double dt)
{
    if (!open_)
        return;
    if (dirty_)
        rebuild();
    if (period_ > 0) {
        if (clock_.playing) {
            scroll_ += static_cast<double>(speed_) * clock_.speed * dt;
            scroll_ = std::fmod(scroll_, static_cast<double>(period_));
            if (scroll_ < 0.0)
                scroll_ += static_cast<double>(period_);
            needsDraw_ = true;
        }
    }
    if (needsDraw_)
        render();
}

void TextSource::render()
{
    if (!fbo_ || !strip_)
        return;
    needsDraw_ = false;

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo = 0, prevTex = 0, prevActive = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);

    fbo_->bind();
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);

    shader_->use();
    shader_->setInt("uStrip", 0);
    shader_->setVec2("uOut", static_cast<float>(width_), static_cast<float>(height_));
    shader_->setVec2("uStripSize", static_cast<float>(stripW_), static_cast<float>(stripH_));
    float ox = 0.0f, oy = 0.0f;
    if (marquee_ == TextMarquee::Horizontal)
        ox = static_cast<float>(scroll_);
    else if (marquee_ == TextMarquee::Vertical)
        oy = static_cast<float>(scroll_);
    shader_->setVec2("uOffset", ox, oy);
    glBindTexture(GL_TEXTURE_2D, strip_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    glActiveTexture(static_cast<GLenum>(prevActive));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (blend)
        glEnable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (depth)
        glEnable(GL_DEPTH_TEST);
}

}
