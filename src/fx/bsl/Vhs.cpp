#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

struct Glyph
{
    char c;
    std::array<std::uint8_t, 7> rows;
};

constexpr Glyph kFont[] = {
    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},
    {':', {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000}},
    {'.', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'Y', {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'>', {0b10000, 0b11000, 0b11100, 0b11110, 0b11100, 0b11000, 0b10000}},
};

constexpr int kOvW = 64;
constexpr int kOvH = 32;
constexpr int kRowPlay = 0, kRowDate = 10, kRowTime = 20;

void drawText(std::vector<std::uint8_t>& px, int row0, const std::string& text)
{
    int x0 = 0;
    for (char ch : text) {
        for (const Glyph& g : kFont) {
            if (g.c != ch)
                continue;
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 5; ++x)
                    if (g.rows[static_cast<size_t>(y)] & (1 << (4 - x)))
                        px[static_cast<size_t>(row0 + y) * kOvW + static_cast<size_t>(x0 + x)] = 255;
        }
        x0 += 6;
    }
}

struct Preset
{
    float wobble, track, noise, chroma, sat, contrast, lift, vig, scan, drop;
    float tint[3];
};

constexpr Preset kPresets[4] = {
    {0.80f, 0.80f, 0.70f, 0.85f, 0.85f, 0.92f, 0.80f, 0.60f, 0.55f, 0.70f, {1.00f, 1.00f, 0.90f}},
    {0.30f, 0.30f, 0.35f, 0.55f, 1.18f, 1.06f, 0.35f, 0.55f, 0.35f, 0.15f, {1.03f, 1.00f, 0.94f}},
    {1.80f, 1.60f, 1.00f, 1.60f, 1.35f, 1.12f, 0.50f, 0.40f, 0.65f, 1.60f, {1.02f, 0.94f, 1.08f}},
    {0.20f, 0.20f, 0.20f, 0.30f, 1.00f, 0.98f, 0.20f, 0.30f, 0.20f, 0.05f, {1.00f, 1.00f, 1.00f}},
};

const char* const kVhsBody = R"(
uniform sampler2D uOv;
uniform float uWobble, uTrack, uNoise, uChroma, uSat, uContrast, uLift, uVig, uScan, uDrop;
uniform vec3 uTint;
uniform float uIntensity;
uniform float uTrackPos;
uniform float uTrackAmt;
uniform float uStrobe;
uniform float uPlay, uStamp;
uniform float uPxs;

float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }

float ovLine(vec2 px, vec2 origin, int row0, int len)
{
    vec2 l = (px - origin) / uPxs;
    if (l.x < 0.0 || l.y < 0.0 || l.x >= float(len * 6) || l.y >= 7.0)
        return 0.0;
    return texelFetch(uOv, ivec2(int(l.x), row0 + int(l.y)), 0).r;
}

float overlay(vec2 uv)
{
    vec2 px = uv * uSize;
    float mx = 6.0 * uPxs, my = 5.0 * uPxs;
    float m = 0.0;
    if (uPlay > 0.5)
        m = max(m, ovLine(px, vec2(mx, my), 0, 6));
    if (uStamp > 0.5) {
        m = max(m, ovLine(px, vec2(uSize.x - mx - 60.0 * uPxs, uSize.y - my - 17.0 * uPxs), 10, 10));
        m = max(m, ovLine(px, vec2(uSize.x - mx - 48.0 * uPxs, uSize.y - my - 7.0 * uPxs), 20, 8));
    }
    return m;
}

vec3 sceneRgb(vec2 uv)
{
    return mix(samp(uv).rgb, vec3(0.96), overlay(uv));
}

vec3 toYiq(vec3 c) { return vec3(dot(c, vec3(0.299, 0.587, 0.114)), dot(c, vec3(0.596, -0.274, -0.322)), dot(c, vec3(0.211, -0.523, 0.312))); }
vec3 fromYiq(vec3 q) { return vec3(q.x + 0.956 * q.y + 0.621 * q.z, q.x - 0.272 * q.y - 0.647 * q.z, q.x - 1.106 * q.y + 1.703 * q.z); }

void main()
{
    vec2 uv = imgPos();
    vec4 orig = samp(uv);
    float ov0 = overlay(uv);
    const float rows = 240.0;
    float row = floor(uv.y * rows);
    float ft = floor(uTime * 30.0);

    float jitter = hash21(vec2(row, ft)) - 0.5;
    float drift = sin(uv.y * 9.0 + uTime * 1.7) * 0.5 + sin(uv.y * 31.0 - uTime * 3.1) * 0.25;
    float dx = (jitter * 0.0035 + drift * 0.0025 + (hash11(ft) - 0.5) * 0.002) * uWobble;

    float hs = smoothstep(0.955, 1.0, uv.y);
    dx += hs * (0.012 + 0.03 * hash21(vec2(row, ft + 3.0))) * uWobble;

    float d = uv.y - uTrackPos;
    d -= floor(d + 0.5);
    const float bandH = 0.04;
    float band = 1.0 - smoothstep(0.0, bandH, abs(d));
    dx += (hash21(vec2(row, ft + 7.0)) - 0.5) * 0.09 * band * uTrackAmt;
    float edge = smoothstep(0.006, 0.0, abs(d + bandH * 0.9)) * uTrackAmt;

    vec2 suv = vec2(uv.x + dx, uv.y);
    vec3 rgb = sceneRgb(suv);
    vec3 yiq = toYiq(rgb);

    float cw = 0.016 * uChroma;
    float lag = 0.006 * uChroma;
    float cy = mix(uv.y, (floor(uv.y * rows * 0.5) + 0.5) / (rows * 0.5), min(uChroma, 1.0));
    vec2 iq = vec2(0.0);
    for (int k = 0; k < 8; ++k) {
        float t = (float(k) + 0.5) / 8.0;
        vec3 c = toYiq(sceneRgb(vec2(suv.x - lag + (t - 0.5) * 2.0 * cw, cy)));
        iq += c.yz;
    }
    iq /= 8.0;
    yiq = vec3(yiq.x, iq);

    yiq.yz *= uSat * (1.0 - 0.6 * band * uTrackAmt);
    yiq.x = (yiq.x - 0.5) * uContrast + 0.5;
    yiq.x = mix(yiq.x, 0.05 + yiq.x * 0.9, uLift);
    rgb = fromYiq(yiq) * uTint;

    float grain = hash21(uv * uSize + ft * 17.0) - 0.5;
    rgb += grain * 0.18 * uNoise;
    float lineN = hash21(vec2(row, ft + 31.0));
    float streak = step(1.0 - 0.03 * uNoise, lineN) * hash21(vec2(floor(uv.x * 40.0), row + ft));
    rgb += streak * 0.4;
    float snow = step(1.0 - 0.005 * uNoise, hash21(vec2(floor(uv.x * uSize.x * 0.5), row * 7.0 + ft)));
    rgb = mix(rgb, vec3(1.0), snow * 0.85);
    rgb = mix(rgb, vec3(grain + 0.5) * 0.85, band * 0.4 * min(uTrackAmt, 1.5));
    rgb += edge * 0.35;

    float slot = floor(uTime * 12.0);
    float ev = hash21(vec2(row, slot));
    if (ev > 1.0 - 0.012 * uDrop) {
        float x0 = hash21(vec2(row, slot + 5.0)) * 0.9;
        float len = 0.04 + 0.25 * hash21(vec2(row, slot + 9.0));
        float inside = step(x0, uv.x) * step(uv.x, x0 + len);
        vec3 dc = vec3(step(0.45, hash21(vec2(row, slot + 13.0))) * 0.9 + 0.05);
        rgb = mix(rgb, dc, inside * 0.9);
    }

    rgb *= 1.0 - uScan * 0.25 * (0.5 - 0.5 * cos(uv.y * rows * 6.2831853));
    float v = smoothstep(0.35, 0.95, length((uv - 0.5) * vec2(uSize.x / uSize.y, 1.0) * 0.9));
    rgb *= 1.0 - uVig * 0.7 * v;
    rgb += uStrobe;

    rgb = mix(orig.rgb, clamp(rgb, 0.0, 1.0), uIntensity);
    fragColor = vec4(rgb, max(orig.a, ov0 * uIntensity));
}
)";

class VhsEffect final : public BslEffect
{
public:
    VhsEffect() : BslEffect({kVhsId, "BSL VHS", "Estilo"}, describeVhs()) {}

    ~VhsEffect() override
    {
        if (ovTex_)
            glDeleteTextures(1, &ovTex_);
    }

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kVhsBody; }

    void prepare(const EffectContext& ctx, int , int h) override
    {
        const ParamSet& p = params();
        updateOverlay(p.b("playText"), p.b("dateStamp"));

        const Preset& pr = kPresets[std::clamp(enumIndex(p, "preset"), 0, 3)];
        auto k = [&](const char* id) { return p.f(id) * 0.01f; };
        wobble_ = pr.wobble * k("wobble");
        track_ = pr.track * k("tracking");
        noise_ = pr.noise * k("noise");
        chroma_ = pr.chroma * k("chroma");
        const float color = k("color");
        sat_ = 1.0f + (pr.sat - 1.0f) * color;
        contrast_ = 1.0f + (pr.contrast - 1.0f) * color;
        lift_ = std::clamp(pr.lift * color, 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i)
            tint_[i] = 1.0f + (pr.tint[i] - 1.0f) * color;
        vig_ = pr.vig * k("vignette");
        scan_ = pr.scan * k("scanlines");
        drop_ = pr.drop * k("dropout");
        intensity_ = std::clamp(p.f("intensity") * 0.01f, 0.0f, 1.0f);
        pxs_ = std::max(1.0f, std::floor(static_cast<float>(h) / 180.0f));

        trackAmt_ = track_;
        strobe_ = 0.0f;
        if (p.b("bpmSync")) {
            const std::string div = p.s("bpmDiv");
            const double beats = div == "4" ? 4.0 : div == "2" ? 2.0 : 1.0;
            const double cycle = std::fmod(std::max(ctx.beat, 0.0), beats) / beats;
            trackPos_ = static_cast<float>(cycle);
            const float pulse = std::exp(-static_cast<float>(cycle) * static_cast<float>(beats) * 5.0f);
            trackAmt_ = track_ * (0.3f + 0.7f * pulse);
            strobe_ = pulse * pulse * 0.25f * std::min(track_, 1.5f);
        } else {
            trackPos_ = static_cast<float>(std::fmod(ctx.time * 0.11, 1.0));
        }
        play_ = p.b("playText") ? 1.0f : 0.0f;
        stamp_ = p.b("dateStamp") ? 1.0f : 0.0f;
    }

    void render(const EffectContext&, int, int) override
    {
        bindTexture(1, ovTex_);
        Shader& s = shader();
        s.setInt("uOv", 1);
        s.setFloat("uWobble", wobble_);
        s.setFloat("uTrack", track_);
        s.setFloat("uNoise", noise_);
        s.setFloat("uChroma", chroma_);
        s.setFloat("uSat", sat_);
        s.setFloat("uContrast", contrast_);
        s.setFloat("uLift", lift_);
        s.setFloat("uVig", vig_);
        s.setFloat("uScan", scan_);
        s.setFloat("uDrop", drop_);
        s.setVec3("uTint", tint_[0], tint_[1], tint_[2]);
        s.setFloat("uIntensity", intensity_);
        s.setFloat("uTrackPos", trackPos_);
        s.setFloat("uTrackAmt", trackAmt_);
        s.setFloat("uStrobe", strobe_);
        s.setFloat("uPlay", play_);
        s.setFloat("uStamp", stamp_);
        s.setFloat("uPxs", pxs_);
    }

private:
    void updateOverlay(bool play, bool stamp)
    {
        std::string date = "00.00.0000", time = "00:00:00";
        if (stamp) {
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
            localtime_s(&tm, &t);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
            date = buf;
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
            time = buf;
        }
        const std::string key = std::string(play ? "p" : "-") + (stamp ? date + time : std::string());
        if (ovTex_ && key == ovKey_)
            return;
        ovKey_ = key;
        std::vector<std::uint8_t> px(static_cast<size_t>(kOvW) * kOvH, 0);
        drawText(px, kRowPlay, "PLAY >");
        drawText(px, kRowDate, date);
        drawText(px, kRowTime, time);
        GLint prevAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (!ovTex_) {
            glGenTextures(1, &ovTex_);
            glBindTexture(GL_TEXTURE_2D, ovTex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kOvW, kOvH, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
        } else {
            glBindTexture(GL_TEXTURE_2D, ovTex_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kOvW, kOvH, GL_RED, GL_UNSIGNED_BYTE, px.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    }

    GLuint ovTex_ = 0;
    std::string ovKey_;
    float wobble_ = 0, track_ = 0, noise_ = 0, chroma_ = 0, sat_ = 1, contrast_ = 1, lift_ = 0, vig_ = 0, scan_ = 0, drop_ = 0;
    float tint_[3] = {1, 1, 1};
    float intensity_ = 1, trackPos_ = 0, trackAmt_ = 0, strobe_ = 0, play_ = 0, stamp_ = 0, pxs_ = 1;
};

}

std::vector<media::ParamDesc> describeVhs()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("preset", "Preset",
                         {{"worn", "Fita gasta"}, {"cam90", "Câmera 90s"}, {"glitch", "Glitch forte"}, {"subtle", "Sutil"}},
                         "worn", "Preset"));
    d.back().tooltip = "Ponto de partida de todos os defeitos. Os controles abaixo ajustam o preset em %";
    d.push_back(makeFloat("intensity", "Intensidade", 100.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Mistura do efeito com a imagem original. 0 = só a imagem original";

    auto knob = [&](const char* id, const char* label, const char* tip, const char* group = "") {
        d.push_back(makeFloat(id, label, 100.0f, 0.0f, 200.0f, "%.0f %%", group));
        d.back().tooltip = tip;
    };
    knob("wobble", "Tremor da fita", "Tremor horizontal por linha (tape wobble) e a troca de cabeçote no rodapé. 100 % = como o preset",
         "Defeitos da fita");
    knob("tracking", "Faixa de tracking", "Faixa que rola pela imagem rasgando e chiando, como uma fita mal ajustada");
    knob("noise", "Ruído e chuvisco", "Grão, riscos horizontais e pontos brancos de chuvisco");
    knob("chroma", "Cor vazando", "O croma perde resolução (YIQ) e escorre para a direita da imagem");
    knob("color", "Saturação e contraste", "Cor, contraste e pretos levantados típicos de VHS");
    knob("vignette", "Vinheta", "Escurece os cantos");
    knob("scanlines", "Linhas de scan", "Linhas horizontais de TV de tubo");
    knob("dropout", "Dropout", "Riscos curtos claros ou escuros que aparecem por poucos quadros");

    d.push_back(makeBool("bpmSync", "Tracking no BPM", false, "Sincronia"));
    d.back().tooltip = "A faixa de tracking dá uma volta a cada 1, 2 ou 4 batidas e pisca (strobe) no tempo da batida";
    d.push_back(makeEnum("bpmDiv", "A cada", {{"1", "1 batida"}, {"2", "2 batidas"}, {"4", "1 compasso (4)"}}, "1"));
    d.back().enabledIf = "bpmSync";

    d.push_back(makeBool("playText", "Texto \"PLAY\"", false, "Texto de câmera"));
    d.back().tooltip = "Escreve PLAY e o triângulo no canto superior esquerdo, com fonte de pixels. O texto também sofre o tremor da fita";
    d.push_back(makeBool("dateStamp", "Data e hora", false));
    d.back().tooltip = "Data e hora do PC no canto inferior direito, estilo câmera de mão";
    return d;
}

std::unique_ptr<IEffect> makeVhs()
{
    return std::make_unique<VhsEffect>();
}

}
