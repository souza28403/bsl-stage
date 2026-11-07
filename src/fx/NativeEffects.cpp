#include "fx/EffectRegistry.h"
#include "fx/FxGl.h"
#include "ui/Icons.h"

#include "app/Log.h"
#include "render/Shader.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace bsl::fx {

namespace {

using media::ParamDesc;
using media::ParamSet;
using media::ParamType;
using namespace media::param;

const char* const kPrelude = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uSize;
uniform int uTop;
uniform float uTime;
uniform float uBeat;
uniform float uBeatPhase;
uniform float uBarPhase;
const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);
vec2 imgPos() { return uTop == 1 ? vUv : vec2(vUv.x, 1.0 - vUv.y); }
vec2 toTex(vec2 p) { return uTop == 1 ? p : vec2(p.x, 1.0 - p.y); }
vec4 samp(vec2 p) { return texture(uTex, toTex(p)); }
vec4 sampC(vec2 p)
{
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
        return vec4(0.0);
    return texture(uTex, toTex(p));
}
)";

const char* const kPercentFmt = "%.0f %%";

std::vector<ParamDesc> descTransform()
{
    return {makeFloat("posX", "Posição X", 0.0f, -200.0f, 200.0f, "%.1f %%"),
            makeFloat("posY", "Posição Y", 0.0f, -200.0f, 200.0f, "%.1f %%"),
            makeFloat("scale", "Escala", 100.0f, 1.0f, 1000.0f, kPercentFmt),
            makeFloat("scaleX", "Escala X", 100.0f, 1.0f, 1000.0f, kPercentFmt),
            makeFloat("scaleY", "Escala Y", 100.0f, 1.0f, 1000.0f, kPercentFmt),
            makeFloat("rotation", "Rotação", 0.0f, -720.0f, 720.0f, "%.1f °"),
            makeFloat("anchorX", "Âncora X", 50.0f, -100.0f, 200.0f, kPercentFmt),
            makeFloat("anchorY", "Âncora Y", 50.0f, -100.0f, 200.0f, kPercentFmt),
            makeFloat("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kPercentFmt)};
}

std::vector<ParamDesc> descColor()
{
    return {makeFloat("brightness", "Brilho", 0.0f, -1.0f, 1.0f),
            makeFloat("contrast", "Contraste", 0.0f, -1.0f, 1.0f),
            makeFloat("saturation", "Saturação", 1.0f, 0.0f, 2.0f),
            makeFloat("hue", "Matiz", 0.0f, -180.0f, 180.0f, "%.0f °"),
            makeFloat("gamma", "Gamma", 1.0f, 0.2f, 3.0f),
            makeFloat("temperature", "Temperatura", 0.0f, -1.0f, 1.0f)};
}

std::vector<ParamDesc> descLevels()
{
    return {makeFloat("inBlack", "Entrada preto", 0.0f, 0.0f, 1.0f),
            makeFloat("inWhite", "Entrada branco", 1.0f, 0.0f, 1.0f),
            makeFloat("gamma", "Gamma", 1.0f, 0.1f, 4.0f),
            makeFloat("outBlack", "Saída preto", 0.0f, 0.0f, 1.0f),
            makeFloat("outWhite", "Saída branco", 1.0f, 0.0f, 1.0f)};
}

std::vector<ParamDesc> descBlur()
{
    return {makeFloat("radius", "Raio", 10.0f, 0.0f, 200.0f, "%.1f px"),
            makeEnum("quality", "Qualidade",
                     {{"low", "Baixa (rápido)"}, {"medium", "Média"}, {"high", "Alta"}}, "medium")};
}

std::vector<ParamDesc> descChroma()
{
    return {makeColor("color", "Cor da chave", {0.0f, 1.0f, 0.0f, 1.0f}),
            makeFloat("tolerance", "Tolerância", 0.35f, 0.0f, 1.0f),
            makeFloat("softness", "Suavidade", 0.10f, 0.0f, 1.0f),
            makeFloat("spill", "Remover spill", 0.5f, 0.0f, 1.0f)};
}

std::vector<ParamDesc> descLuma()
{
    return {makeFloat("threshold", "Limite", 0.5f, 0.0f, 1.0f), makeFloat("softness", "Suavidade", 0.1f, 0.0f, 0.5f),
            makeBool("invert", "Remover claros", false)};
}

std::vector<ParamDesc> descMask()
{
    std::vector<ParamDesc> d;
    d.push_back(makeEnum("shape", "Forma", {{"rect", "Retângulo"}, {"ellipse", "Elipse"}}, "rect"));
    d.push_back(makeFloat("centerX", "Centro X", 50.0f, -100.0f, 200.0f, kPercentFmt));
    d.push_back(makeFloat("centerY", "Centro Y", 50.0f, -100.0f, 200.0f, kPercentFmt));
    d.push_back(makeFloat("width", "Largura", 60.0f, 0.0f, 400.0f, kPercentFmt));
    d.push_back(makeFloat("height", "Altura", 60.0f, 0.0f, 400.0f, kPercentFmt));
    d.push_back(makeFloat("feather", "Suavizar borda", 0.0f, 0.0f, 100.0f, "%.1f %%"));
    d.push_back(makeBool("invert", "Inverter", false));
    d.back().tooltip = "Mantém o que está fora da forma";
    return d;
}

std::vector<ParamDesc> descCrop()
{
    return {makeEnum("unit", "Unidade", {{"px", "Pixels"}, {"percent", "Porcentagem"}}, "percent"),
            makeFloat("left", "Esquerda", 0.0f, 0.0f, 8192.0f, "%.1f"),
            makeFloat("top", "Topo", 0.0f, 0.0f, 8192.0f, "%.1f"),
            makeFloat("right", "Direita", 0.0f, 0.0f, 8192.0f, "%.1f"),
            makeFloat("bottom", "Base", 0.0f, 0.0f, 8192.0f, "%.1f")};
}

std::vector<ParamDesc> descMirror()
{
    return {makeEnum("mode", "Modo",
                     {{"h", "Horizontal"}, {"v", "Vertical"}, {"quad", "Quatro quadrantes"}}, "h"),
            makeBool("flip", "Usar o outro lado", false)};
}

std::vector<ParamDesc> descKaleido()
{
    return {makeInt("segments", "Segmentos", 6, 2, 32), makeFloat("rotation", "Rotação", 0.0f, -360.0f, 360.0f, "%.0f °"),
            makeFloat("zoom", "Zoom", 1.0f, 0.25f, 4.0f)};
}

std::vector<ParamDesc> descPixelate()
{
    std::vector<ParamDesc> d;
    d.push_back(makeFloat("size", "Tamanho do pixel", 16.0f, 1.0f, 256.0f, "%.0f px"));
    d.push_back(makeBool("led", "Aspecto de painel LED", false));
    d.back().tooltip = "Cada pixel vira um ponto redondo com espaço escuro entre eles";
    d.push_back(makeFloat("gap", "Espaço entre pontos", 0.3f, 0.0f, 0.9f));
    d.back().enabledIf = "led";
    return d;
}

std::vector<ParamDesc> descStrobe()
{
    std::vector<ParamDesc> d;
    d.push_back(makeEnum("mode", "Modo", {{"sync", "Sincronizado ao BPM"}, {"free", "Livre (Hz)"}}, "sync"));
    d.push_back(makeEnum("division", "Divisão",
                         {{"0.25", "1/4 (um flash a cada 4 batidas)"},
                          {"0.5", "1/2 (um flash a cada 2 batidas)"},
                          {"1", "1 (um por batida)"},
                          {"2", "2 (dois por batida)"},
                          {"4", "4 (quatro por batida)"}},
                         "1"));
    d.back().enabledIf = "mode=sync";
    d.push_back(makeFloat("hz", "Frequência", 4.0f, 0.1f, 30.0f, "%.1f Hz"));
    d.back().enabledIf = "mode=free";
    d.push_back(makeFloat("duty", "Tempo aceso (duty)", 0.5f, 0.02f, 0.98f));
    return d;
}

std::vector<ParamDesc> descInvert()
{
    return {makeFloat("amount", "Quantidade", 1.0f, 0.0f, 1.0f)};
}

std::vector<ParamDesc> descVignette()
{
    return {makeFloat("amount", "Intensidade", 0.6f, 0.0f, 1.0f), makeFloat("radius", "Raio", 0.6f, 0.0f, 1.5f),
            makeFloat("softness", "Suavidade", 0.5f, 0.01f, 1.5f)};
}

std::vector<ParamDesc> descRgbSplit()
{
    return {makeFloat("amount", "Distância", 8.0f, 0.0f, 100.0f, "%.1f px"),
            makeFloat("angle", "Ângulo", 0.0f, -180.0f, 180.0f, "%.0f °")};
}

const char* const kGlslTransform = R"(
void main()
{
    vec2 px = imgPos() * uSize;
    vec2 anchor = vec2(u_anchorX, u_anchorY) * 0.01 * uSize;
    vec2 d = px - anchor - vec2(u_posX, u_posY) * 0.01 * uSize;
    float r = radians(u_rotation);
    float c = cos(r), s = sin(r);
    vec2 q = vec2(c * d.x + s * d.y, -s * d.x + c * d.y);
    vec2 sc = max(vec2(u_scale * u_scaleX, u_scale * u_scaleY) * 0.0001, vec2(1.0e-4));
    vec2 src = (anchor + q / sc) / uSize;
    vec4 col = sampC(src);
    col.a *= u_opacity * 0.01;
    fragColor = col;
}
)";

const char* const kGlslColor = R"(
void main()
{
    vec4 c = samp(imgPos());
    vec3 rgb = pow(max(c.rgb, vec3(0.0)), vec3(1.0 / max(u_gamma, 0.01)));
    rgb += u_brightness;
    float k = tan((clamp(u_contrast, -0.99, 0.99) + 1.0) * 0.78539816);
    rgb = (rgb - 0.5) * k + 0.5;
    rgb *= vec3(1.0 + 0.3 * u_temperature, 1.0 + 0.04 * u_temperature, 1.0 - 0.3 * u_temperature);
    float a = radians(u_hue);
    vec3 ax = vec3(0.57735027);
    rgb = rgb * cos(a) + cross(ax, rgb) * sin(a) + ax * dot(ax, rgb) * (1.0 - cos(a));
    float l = dot(rgb, kLuma);
    rgb = mix(vec3(l), rgb, u_saturation);
    fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);
}
)";

const char* const kGlslLevels = R"(
void main()
{
    vec4 c = samp(imgPos());
    vec3 rgb = clamp((c.rgb - u_inBlack) / max(u_inWhite - u_inBlack, 1.0e-4), 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / max(u_gamma, 0.01)));
    rgb = mix(vec3(u_outBlack), vec3(u_outWhite), rgb);
    fragColor = vec4(rgb, c.a);
}
)";

const char* const kGlslChroma = R"(
vec2 cbcr(vec3 c)
{
    return vec2(-0.169 * c.r - 0.331 * c.g + 0.5 * c.b, 0.5 * c.r - 0.419 * c.g - 0.081 * c.b);
}
void main()
{
    vec4 c = samp(imgPos());
    float d = length(cbcr(c.rgb) - cbcr(u_color.rgb));
    float keep = smoothstep(u_tolerance, u_tolerance + max(u_softness, 1.0e-4), d);
    float nearKey = 1.0 - smoothstep(u_tolerance, u_tolerance + u_softness + 0.3, d);
    vec3 rgb = mix(c.rgb, vec3(dot(c.rgb, kLuma)), u_spill * nearKey);
    fragColor = vec4(rgb, c.a * keep);
}
)";

const char* const kGlslLuma = R"(
void main()
{
    vec4 c = samp(imgPos());
    float l = dot(c.rgb, kLuma);
    float a = smoothstep(u_threshold - u_softness, u_threshold + u_softness + 1.0e-4, l);
    if (u_invert)
        a = 1.0 - a;
    fragColor = vec4(c.rgb, c.a * a);
}
)";

const char* const kGlslMask = R"(
void main()
{
    vec4 c = samp(imgPos());
    vec2 px = imgPos() * uSize;
    vec2 ctr = vec2(u_centerX, u_centerY) * 0.01 * uSize;
    vec2 half_ = max(vec2(u_width, u_height) * 0.005 * uSize, vec2(0.5));
    float sd;
    if (u_shape == 1) {
        sd = (length((px - ctr) / half_) - 1.0) * min(half_.x, half_.y);
    } else {
        vec2 q = abs(px - ctr) - half_;
        sd = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0);
    }
    float f = max(u_feather * 0.01 * min(uSize.x, uSize.y), 1.0);
    float m = smoothstep(0.5 * f, -0.5 * f, sd);
    if (u_invert)
        m = 1.0 - m;
    fragColor = vec4(c.rgb, c.a * m);
}
)";

const char* const kGlslCrop = R"(
void main()
{
    vec4 c = samp(imgPos());
    vec2 px = imgPos() * uSize;
    vec4 e = vec4(u_left, u_top, u_right, u_bottom);
    if (u_unit == 1)
        e *= vec4(uSize.x, uSize.y, uSize.x, uSize.y) * 0.01;
    bool inside = px.x >= e.x && px.x <= uSize.x - e.z && px.y >= e.y && px.y <= uSize.y - e.w;
    fragColor = inside ? c : vec4(0.0);
}
)";

const char* const kGlslMirror = R"(
void main()
{
    vec2 p = imgPos();
    if (u_mode == 0 || u_mode == 2) {
        if (u_flip ? p.x < 0.5 : p.x > 0.5)
            p.x = 1.0 - p.x;
    }
    if (u_mode == 1 || u_mode == 2) {
        if (u_flip ? p.y < 0.5 : p.y > 0.5)
            p.y = 1.0 - p.y;
    }
    fragColor = samp(p);
}
)";

const char* const kGlslKaleido = R"(
void main()
{
    float aspect = uSize.x / uSize.y;
    vec2 p = (imgPos() - 0.5) * vec2(aspect, 1.0);
    float r = length(p);
    float rot = radians(u_rotation);
    float seg = 6.28318530718 / float(int(u_segments + 0.5));
    float a = mod(atan(p.y, p.x) - rot, seg);
    a = abs(a - seg * 0.5) + rot;
    vec2 q = r * vec2(cos(a), sin(a)) / max(u_zoom, 0.01);
    q = vec2(q.x / aspect, q.y) + 0.5;
    q = 1.0 - abs(mod(q, 2.0) - 1.0);
    fragColor = samp(q);
}
)";

const char* const kGlslPixelate = R"(
void main()
{
    float s = max(u_size, 1.0);
    vec2 px = imgPos() * uSize;
    vec2 cell = floor(px / s);
    vec2 center = floor((cell + 0.5) * s) + 0.5;
    vec4 c = samp(min(center, uSize - 0.5) / uSize);
    if (u_led) {
        vec2 f = px / s - cell - 0.5;
        float rad = 0.5 * (1.0 - u_gap);
        float edge = 1.0 / s;
        c.rgb *= 1.0 - smoothstep(rad - edge, rad + edge, length(f));
    }
    fragColor = c;
}
)";

const char* const kGlslStrobe = R"(
void main()
{
    vec4 c = samp(imgPos());
    float phase;
    if (u_mode == 0) {
        float rates[5] = float[5](0.25, 0.5, 1.0, 2.0, 4.0);
        phase = fract(uBeat * rates[clamp(u_division, 0, 4)]);
    } else {
        phase = fract(uTime * u_hz);
    }
    fragColor = phase < u_duty ? c : vec4(0.0);
}
)";

const char* const kGlslInvert = R"(
void main()
{
    vec4 c = samp(imgPos());
    fragColor = vec4(mix(c.rgb, 1.0 - c.rgb, u_amount), c.a);
}
)";

const char* const kGlslVignette = R"(
void main()
{
    vec4 c = samp(imgPos());
    float d = length((imgPos() - 0.5) * 2.0);
    float v = 1.0 - u_amount * smoothstep(u_radius, u_radius + max(u_softness, 1.0e-3), d);
    fragColor = vec4(c.rgb * v, c.a);
}
)";

const char* const kGlslRgbSplit = R"(
void main()
{
    vec2 p = imgPos();
    float a = radians(u_angle);
    vec2 off = vec2(cos(a), sin(a)) * u_amount / uSize;
    vec4 r = samp(p + off);
    vec4 g = samp(p);
    vec4 b = samp(p - off);
    fragColor = vec4(r.r, g.g, b.b, max(r.a, max(g.a, b.a)));
}
)";

struct Def
{
    const char* id;
    const char* name;
    const char* category;
    const char* icon;
    const char* description;
    std::vector<ParamDesc> (*describe)();
    const char* glsl;
};

const Def kDefs[] = {
    {"native:transform", "Transformar", "Transformação", ICON_MOVE,
     "Posição, escala, rotação, âncora e opacidade", &descTransform, kGlslTransform},
    {"native:color", "Ajuste de cor", "Cor", ICON_SLIDERS, "Brilho, contraste, saturação, matiz, gamma e temperatura",
     &descColor, kGlslColor},
    {"native:levels", "Níveis", "Cor", ICON_CONTRAST, "Pontos de preto e branco de entrada e saída, gamma", &descLevels,
     kGlslLevels},
    {"native:blur", "Desfoque", "Desfoque", ICON_DROPLET, "Desfoque gaussiano em dois passes", &descBlur, nullptr},
    {"native:chroma", "Chroma key", "Chave e máscara", ICON_PIPETTE, "Remove uma cor (tela verde/azul)", &descChroma,
     kGlslChroma},
    {"native:luma", "Luma key", "Chave e máscara", ICON_SUN, "Remove as áreas escuras (ou claras)", &descLuma, kGlslLuma},
    {"native:mask", "Máscara", "Chave e máscara", ICON_SQUARE_DASHED, "Máscara de retângulo ou elipse", &descMask,
     kGlslMask},
    {"native:crop", "Recortar", "Chave e máscara", ICON_CROP, "Corta cada lado em pixels ou porcentagem", &descCrop,
     kGlslCrop},
    {"native:mirror", "Espelho", "Distorção", ICON_FLIP_H, "Espelha horizontal, vertical ou em quatro quadrantes",
     &descMirror, kGlslMirror},
    {"native:kaleido", "Caleidoscópio", "Distorção", ICON_FLOWER, "Repete um segmento da imagem em volta do centro",
     &descKaleido, kGlslKaleido},
    {"native:pixelate", "Pixelizar", "Estilizar", ICON_GRID_2X2, "Blocos grandes; útil para simular painel LED",
     &descPixelate, kGlslPixelate},
    {"native:strobe", "Strobe", "Tempo", ICON_ZAP, "Pisca no ritmo do BPM ou em Hz", &descStrobe, kGlslStrobe},
    {"native:invert", "Inverter", "Cor", ICON_BLEND, "Inverte as cores", &descInvert, kGlslInvert},
    {"native:vignette", "Vinheta", "Estilizar", ICON_CIRCLE_DOT, "Escurece as bordas", &descVignette, kGlslVignette},
    {"native:rgbsplit", "Deslocamento de cor", "Estilizar", ICON_LAYERS, "Separa os canais vermelho e azul (RGB split)",
     &descRgbSplit, kGlslRgbSplit},
};

const Def* findDef(const std::string& id)
{
    for (const Def& d : kDefs)
        if (id == d.id)
            return &d;
    return nullptr;
}

struct UniformValue
{
    ParamType type = ParamType::Float;
    float v[4] = {0, 0, 0, 0};
};

std::vector<UniformValue> readValues(const std::vector<ParamDesc>& descs, const ParamSet& ps)
{
    std::vector<UniformValue> out;
    out.reserve(descs.size());
    for (const ParamDesc& d : descs) {
        UniformValue u;
        u.type = d.type;
        switch (d.type) {
        case ParamType::Float:
        case ParamType::Int:
            u.v[0] = ps.f(d.id);
            break;
        case ParamType::Bool:
            u.v[0] = ps.b(d.id) ? 1.0f : 0.0f;
            break;
        case ParamType::Color: {
            const media::Color c = ps.c(d.id);
            u.v[0] = c.r;
            u.v[1] = c.g;
            u.v[2] = c.b;
            u.v[3] = c.a;
            break;
        }
        case ParamType::Enum: {
            const std::string cur = ps.s(d.id);
            for (size_t i = 0; i < d.options.size(); ++i)
                if (d.options[i].value == cur)
                    u.v[0] = static_cast<float>(i);
            break;
        }
        default:
            break;
        }
        out.push_back(u);
    }
    return out;
}

std::string uniformDecls(const std::vector<ParamDesc>& descs)
{
    std::string s;
    for (const ParamDesc& d : descs) {
        const char* type = "float";
        switch (d.type) {
        case ParamType::Bool: type = "bool"; break;
        case ParamType::Color: type = "vec4"; break;
        case ParamType::Enum: type = "int"; break;
        default: break;
        }
        s += std::string("uniform ") + type + " u_" + d.id + ";\n";
    }
    return s;
}

class ShaderEffect final : public IEffect
{
public:
    explicit ShaderEffect(const Def& def) : def_(def), descs_(def.describe()) {}

    std::string id() const override { return def_.id; }
    std::string name() const override { return def_.name; }
    std::string category() const override { return def_.category; }
    std::vector<ParamDesc> paramDescs() const override { return descs_; }

    void setParams(const ParamSet& params) override
    {
        values_ = readValues(descs_, params);
        haveValues_ = true;
    }

    void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        if (!haveValues_)
            setParams(ParamSet(descs_, media::ParamValues()));
        if (!shader_ && !failed_)
            build();
        if (failed_ || inputTex == 0) {
            if (inputTex != 0)
                blit_.copy(inputTex, w, h, output);
            return;
        }

        bindOutput(output, w, h);
        shader_->use();
        glUniform1i(loc("uTex"), 0);
        glUniform2f(loc("uSize"), static_cast<float>(w), static_cast<float>(h));
        glUniform1i(loc("uTop"), ctx.topOrigin ? 1 : 0);
        glUniform1f(loc("uTime"), static_cast<float>(std::fmod(ctx.time, 86400.0)));
        glUniform1f(loc("uBeat"), static_cast<float>(std::fmod(ctx.beat, 65536.0)));
        glUniform1f(loc("uBeatPhase"), ctx.beatPhase);
        glUniform1f(loc("uBarPhase"), ctx.barPhase);
        for (size_t i = 0; i < descs_.size(); ++i) {
            const GLint l = paramLocs_[i];
            const UniformValue& u = values_[i];
            switch (u.type) {
            case ParamType::Bool:
            case ParamType::Enum:
                glUniform1i(l, static_cast<int>(u.v[0]));
                break;
            case ParamType::Color:
                glUniform4f(l, u.v[0], u.v[1], u.v[2], u.v[3]);
                break;
            default:
                glUniform1f(l, u.v[0]);
                break;
            }
        }
        bindTexture(0, inputTex);
        vao_.draw();
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

    media::SourceStatus status() const override
    {
        if (failed_)
            return {media::SourceStatus::Level::Warning, error_};
        return {};
    }

private:
    GLint loc(const char* name) const { return glGetUniformLocation(shader_->id(), name); }

    void build()
    {
        const std::string frag = std::string(kPrelude) + uniformDecls(descs_) + def_.glsl;
        try {
            shader_ = std::make_unique<Shader>(kFxVertexSource, frag.c_str());
        } catch (const std::exception& e) {
            failed_ = true;
            error_ = std::string("Falha no shader do efeito: ") + e.what();
            log::warn(std::string(def_.name) + ": " + error_);
            return;
        }
        paramLocs_.clear();
        for (const ParamDesc& d : descs_)
            paramLocs_.push_back(glGetUniformLocation(shader_->id(), ("u_" + d.id).c_str()));
    }

    const Def& def_;
    std::vector<ParamDesc> descs_;
    std::vector<UniformValue> values_;
    bool haveValues_ = false;
    std::unique_ptr<Shader> shader_;
    std::vector<GLint> paramLocs_;
    bool failed_ = false;
    std::string error_;
    ScreenVao vao_;
    TexBlitter blit_;
};

const char* const kBlurFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uDir;
uniform float uSigma;
uniform int uN;
uniform int uPre;
uniform int uUnpre;
vec4 fetch(vec2 uv)
{
    vec4 c = texture(uTex, uv);
    if (uPre == 1)
        c.rgb *= c.a;
    return c;
}
void main()
{
    vec4 sum = fetch(vUv);
    float wsum = 1.0;
    if (uN > 0) {
        float k = -0.5 / (uSigma * uSigma);
        for (int i = 1; i <= uN; ++i) {
            float fi = float(i);
            float wgt = exp(k * fi * fi);
            sum += (fetch(vUv + uDir * fi) + fetch(vUv - uDir * fi)) * wgt;
            wsum += 2.0 * wgt;
        }
    }
    sum /= wsum;
    if (uUnpre == 1)
        sum.rgb = sum.a > 1.0e-5 ? sum.rgb / sum.a : vec3(0.0);
    fragColor = sum;
}
)";

class BlurEffect final : public IEffect
{
public:
    BlurEffect() : descs_(descBlur()) {}

    std::string id() const override { return "native:blur"; }
    std::string name() const override { return "Desfoque"; }
    std::string category() const override { return "Desfoque"; }
    std::vector<ParamDesc> paramDescs() const override { return descs_; }

    void setParams(const ParamSet& params) override
    {
        radius_ = params.f("radius");
        const std::string q = params.s("quality");
        maxTaps_ = q == "low" ? 4 : (q == "high" ? 14 : 8);
        haveValues_ = true;
    }

    void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        if (!haveValues_)
            setParams(ParamSet(descs_, media::ParamValues()));
        if (inputTex == 0)
            return;
        if (!shader_ && !failed_)
            build();
        FboPool& pool = ctx.pool ? *ctx.pool : fallbackPool_;
        if (failed_ || radius_ < 0.05f) {
            blit_.copy(inputTex, w, h, output);
            return;
        }

        const float sigma = std::max(radius_ * 0.5f, 0.1f);
        int down = 1;
        while (std::ceil(3.0f * sigma / static_cast<float>(down)) > static_cast<float>(maxTaps_) && down < 64)
            down *= 2;
        if (ctx.eco && down < 2)
            down = 2;
        const float sigmaDown = sigma / static_cast<float>(down);
        const int taps = std::clamp(static_cast<int>(std::ceil(3.0f * sigmaDown)), 1, maxTaps_ + 2);

        GLuint cur = inputTex;
        int cw = w, ch = h;
        bool fromUser = true;
        FboPool::Lease held;

        for (int scale = 1; scale < down;) {
            scale *= 2;
            const int nw = std::max(1, w / scale), nh = std::max(1, h / scale);
            FboPool::Lease next = pool.acquire(nw, nh);
            runPass(cur, cw, ch, *next, 0.0f, 0.0f, 0.0f, 0, fromUser, false);
            fromUser = false;
            held = std::move(next);
            cur = held->texture();
            cw = nw;
            ch = nh;
        }

        const bool direct = down == 1;
        FboPool::Lease mid = pool.acquire(cw, ch);
        runPass(cur, cw, ch, *mid, 1.0f / static_cast<float>(cw), 0.0f, sigmaDown, taps, fromUser, false);
        if (direct) {
            runPass(mid->texture(), cw, ch, output, 0.0f, 1.0f / static_cast<float>(ch), sigmaDown, taps, false, true);
        } else {
            FboPool::Lease blurred = pool.acquire(cw, ch);
            runPass(mid->texture(), cw, ch, *blurred, 0.0f, 1.0f / static_cast<float>(ch), sigmaDown, taps, false, false);
            runPass(blurred->texture(), cw, ch, output, 0.0f, 0.0f, 0.0f, 0, false, true);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

    media::SourceStatus status() const override
    {
        if (failed_)
            return {media::SourceStatus::Level::Warning, error_};
        return {};
    }

private:
    void build()
    {
        try {
            shader_ = std::make_unique<Shader>(kFxVertexSource, kBlurFragment);
        } catch (const std::exception& e) {
            failed_ = true;
            error_ = std::string("Falha no shader do desfoque: ") + e.what();
            log::warn(error_);
        }
    }

    void runPass(GLuint src, int , int , const Fbo& dst, float dx, float dy, float sigma, int taps,
                 bool pre, bool unpre)
    {
        bindOutput(dst, dst.width(), dst.height());
        shader_->use();
        const GLuint p = shader_->id();
        glUniform1i(glGetUniformLocation(p, "uTex"), 0);
        glUniform2f(glGetUniformLocation(p, "uDir"), dx, dy);
        glUniform1f(glGetUniformLocation(p, "uSigma"), std::max(sigma, 0.01f));
        glUniform1i(glGetUniformLocation(p, "uN"), taps);
        glUniform1i(glGetUniformLocation(p, "uPre"), pre ? 1 : 0);
        glUniform1i(glGetUniformLocation(p, "uUnpre"), unpre ? 1 : 0);
        bindTexture(0, src);
        vao_.draw();
    }

    std::vector<ParamDesc> descs_;
    float radius_ = 10.0f;
    int maxTaps_ = 8;
    bool haveValues_ = false;
    std::unique_ptr<Shader> shader_;
    bool failed_ = false;
    std::string error_;
    ScreenVao vao_;
    TexBlitter blit_;
    FboPool fallbackPool_;
};

class NativeProvider final : public IEffectProvider
{
public:
    std::string prefix() const override { return "native:"; }

    std::vector<EffectTypeInfo> list() const override
    {
        std::vector<EffectTypeInfo> out;
        for (const Def& d : kDefs) {
            EffectTypeInfo t;
            t.id = d.id;
            t.name = d.name;
            t.category = d.category;
            t.icon = d.icon;
            t.section = EffectSection::Native;
            t.description = d.description;
            out.push_back(std::move(t));
        }
        return out;
    }

    std::optional<EffectTypeInfo> info(const std::string& typeId) const override
    {
        const Def* d = findDef(typeId);
        if (!d)
            return std::nullopt;
        EffectTypeInfo t;
        t.id = d->id;
        t.name = d->name;
        t.category = d->category;
        t.icon = d->icon;
        t.section = EffectSection::Native;
        t.description = d->description;
        return t;
    }

    std::unique_ptr<IEffect> create(const std::string& typeId) const override
    {
        const Def* d = findDef(typeId);
        if (!d)
            return nullptr;
        if (!d->glsl)
            return std::make_unique<BlurEffect>();
        return std::make_unique<ShaderEffect>(*d);
    }

    std::vector<ParamDesc> describe(const std::string& typeId) const override
    {
        const Def* d = findDef(typeId);
        return d ? d->describe() : std::vector<ParamDesc>{};
    }
};

}

std::shared_ptr<IEffectProvider> makeNativeProvider()
{
    return std::make_shared<NativeProvider>();
}

}
