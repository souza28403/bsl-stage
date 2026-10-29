// BSL AudioReactive Typo (plugin FFGL "ARTY"): tipografia cinetica e HUD de transmissao sobre a camera do DJ, reativos ao
// audio. Reescrito do original; a diferenca de implementacao e o texto: o plugin usava GDI (Impact e Arial) e aqui o
// texto vem do TextRaster do app (stb_truetype), com as mesmas fontes quando existem no Windows.

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include "app/Log.h"
#include "media/FontCatalog.h"
#include "media/TextRaster.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

constexpr int kTypoW = 2048;
constexpr int kTypoH = 1024;

// Se o texto do plugin original aparecia de cabeca para baixo no OpenGL (bitmap de cima para baixo enviado sem
// inverter), aqui ele sai em pe: 1 = a textura e amostrada invertida em v. Ver o shader (uTypoFlip).
constexpr int kTypoFlip = 1;

// Port do fragment shader original (GLSL 4.10 -> 3.30). Coordenadas em y para CIMA (glPos), como o OpenGL do plugin;
// a camera e lida em alpha premultiplicado e a saida volta a alpha reto.
const char* const kTypoBody = R"(
uniform sampler2D uTypo;
uniform sampler2D uFb;
uniform int uTypoFlip;
uniform int uMode;
uniform int uBlendMode;
uniform float uTextScale;
uniform float uPosY;
uniform float uBass;
uniform float uHigh;
uniform float uGlitchTrigger;
uniform float uKickShake;
uniform float uRGBSplit;
uniform float uScrollSpeed;
uniform float uRepeat;
uniform float uGhost;
uniform float uBW;
uniform float uScanlines;
uniform float uVignetteGrain;
uniform float uHud;
uniform vec3 uTextColor;
uniform int uMasterInvert;

vec4 typoTex(vec2 q) { return texture(uTypo, vec2(q.x, uTypoFlip == 1 ? 1.0 - q.y : q.y)); }

float seg(vec2 p, vec2 pos, vec2 size)
{
    vec2 d = abs(p - pos) - size;
    return 1.0 - smoothstep(0.0, 0.03, max(d.x, d.y));
}

float drawDigit7Seg(vec2 p, int digit)
{
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 0.0;
    float t = 0.08;
    float len = 0.35;
    float a  = seg(p, vec2(0.5, 0.92), vec2(len, t));
    float b  = seg(p, vec2(0.88, 0.72), vec2(t, len * 0.65));
    float c  = seg(p, vec2(0.88, 0.28), vec2(t, len * 0.65));
    float d_ = seg(p, vec2(0.5, 0.08), vec2(len, t));
    float e  = seg(p, vec2(0.12, 0.28), vec2(t, len * 0.65));
    float f  = seg(p, vec2(0.12, 0.72), vec2(t, len * 0.65));
    float g  = seg(p, vec2(0.5, 0.50), vec2(len, t));
    float res = 0.0;
    if (digit == 0) res = max(max(max(a, b), max(c, d_)), max(e, f));
    else if (digit == 1) res = max(b, c);
    else if (digit == 2) res = max(max(max(a, b), max(g, e)), d_);
    else if (digit == 3) res = max(max(max(a, b), max(g, c)), d_);
    else if (digit == 4) res = max(max(f, g), max(b, c));
    else if (digit == 5) res = max(max(max(a, f), max(g, c)), d_);
    else if (digit == 6) res = max(max(max(a, f), max(e, g)), max(c, d_));
    else if (digit == 7) res = max(max(a, b), c);
    else if (digit == 8) res = max(max(max(a, b), max(c, d_)), max(max(e, f), g));
    else if (digit == 9) res = max(max(max(a, b), max(c, d_)), max(f, g));
    return res;
}

float drawCornerBrackets(vec2 p, vec2 halfBox, float armLen, float thickness)
{
    vec2 q = abs(p) - halfBox;
    float hArm = max(max(q.y, -q.y - thickness), max(q.x - armLen, -q.x));
    float vArm = max(max(q.x, -q.x - thickness), max(q.y - armLen, -q.y));
    return min(hArm, vArm);
}

float drawCrosshair(vec2 p, float size, float thickness)
{
    float hLine = max(abs(p.y) - thickness * 0.5, max(abs(p.x) - size, -abs(p.x) + size * 0.25));
    float vLine = max(abs(p.x) - thickness * 0.5, max(abs(p.y) - size, -abs(p.y) + size * 0.25));
    return min(hLine, vLine);
}

vec4 drawVUMeter(vec2 uvCoord, vec2 origin, vec2 size, float level)
{
    vec2 p = uvCoord - origin;
    if (p.x < 0.0 || p.x > size.x || p.y < 0.0 || p.y > size.y) return vec4(0.0);
    float normY = p.y / size.y;
    float numSegments = 12.0;
    float segIdx = floor(normY * numSegments);
    float segFract = fract(normY * numSegments);
    if (segFract > 0.8) return vec4(0.0);
    float segLevel = segIdx / numSegments;
    if (segLevel > level) return vec4(0.15, 0.15, 0.18, 0.35);
    vec3 col;
    if (segLevel < 0.6) col = vec3(0.1, 1.0, 0.3);
    else if (segLevel < 0.82) col = vec3(1.0, 0.85, 0.1);
    else col = vec3(1.0, 0.15, 0.15);
    return vec4(col, 0.95);
}

void main()
{
    vec2 normCoord = glPos();
    vec2 uv = normCoord;
    float AspectRatio = uSize.x / max(uSize.y, 1.0);
    float Time = uTime;

    // 1. Tremor no kick
    float shakeAmt = uKickShake * uBass * 0.035;
    vec2 shakeOffset = vec2(sin(Time * 48.0) * cos(Time * 31.0), cos(Time * 43.0) * sin(Time * 29.0)) * shakeAmt;
    vec2 camUV = uv + shakeOffset;

    // 2. Fatias de glitch nos agudos
    float sliceY = floor(normCoord.y * 36.0);
    float sliceRand = fract(sin(sliceY * 78.233 + floor(Time * 14.0) * 45.12) * 43758.5453);
    float glitchActive = step(1.0 - (uHigh * 0.75 + uGlitchTrigger * 0.6), sliceRand);
    float sliceOffset = (sliceRand - 0.5) * 0.12 * glitchActive;
    camUV.x += sliceOffset;

    // 3. Aberracao cromatica
    float splitAmt = (uRGBSplit * 0.02 + uHigh * 0.035 + uBass * 0.015) * (1.0 + glitchActive * 1.5);
    vec2 splitDir = vec2(splitAmt, splitAmt * 0.25);
    float camR = sampGl(camUV + splitDir).r;
    float camG = sampGl(camUV).g;
    float camB = sampGl(camUV - splitDir).b;
    float camA = sampGl(camUV).a;
    vec3 camCol = vec3(camR, camG, camB);

    // 4. P&B rave
    float luma = dot(camCol, vec3(0.299, 0.587, 0.114));
    float bwRave = smoothstep(0.10, 0.85, luma);
    bwRave = pow(bwRave, 1.15) * 1.1;
    camCol = mix(camCol, vec3(bwRave), clamp(uBW, 0.0, 1.0));

    // 5. Scanlines, grao e vinheta
    float sl = sin(normCoord.y * uSize.y * 3.14159265);
    camCol *= (1.0 - (0.5 + 0.5 * sl) * uScanlines * 0.35);
    float grain = (fract(sin(dot(normCoord * uSize + Time, vec2(12.9898, 78.233))) * 43758.5453) - 0.5);
    camCol += grain * uVignetteGrain * 0.15;
    vec2 vigCoord = (normCoord - 0.5) * vec2(AspectRatio, 1.0);
    float vig = smoothstep(0.85, 0.35, length(vigCoord));
    camCol *= mix(1.0, vig, uVignetteGrain * 0.7);

    // 6. Tipografia (4 modos)
    vec4 typoSample = vec4(0.0);
    if (uMode == 0) {
        float bassPunch = uBass * 0.35;
        float effScale = max(uTextScale * (1.0 + bassPunch), 0.05);
        vec2 typoUV = (normCoord - vec2(0.5, uPosY)) / effScale + vec2(0.5, 0.5);
        if (typoUV.x >= 0.0 && typoUV.x <= 1.0 && typoUV.y >= 0.0 && typoUV.y <= 1.0)
            typoSample = typoTex(typoUV);
    } else if (uMode == 1) {
        float rows = floor(clamp(uRepeat, 2.0, 10.0));
        float rowIdx = floor(normCoord.y * rows);
        float rowDir = mod(rowIdx, 2.0) == 0.0 ? 1.0 : -1.0;
        float scroll = (Time * (uScrollSpeed * 0.8 + 0.1) + uBass * 0.12) * rowDir;
        vec2 ribbonUV;
        ribbonUV.x = fract(normCoord.x * (uTextScale * 1.5) + scroll + rowIdx * 0.23);
        ribbonUV.y = fract(normCoord.y * rows);
        // Foco na parte do texto principal (em negrito) da textura
        ribbonUV.y = ribbonUV.y * 0.6 + (uTypoFlip == 1 ? 0.35 : 0.05);
        typoSample = typoTex(ribbonUV);
    } else if (uMode == 2) {
        vec2 gUV = normCoord;
        if (glitchActive > 0.5)
            gUV.x += sliceOffset * 2.2;
        vec2 glitchTypoUV = (gUV - vec2(0.5, uPosY)) / uTextScale + vec2(0.5, 0.5);
        if (glitchTypoUV.x >= 0.0 && glitchTypoUV.x <= 1.0 && glitchTypoUV.y >= 0.0 && glitchTypoUV.y <= 1.0)
            typoSample = typoTex(glitchTypoUV);
        // Matriz tipografica / halftone
        vec2 gridRes = vec2(50.0 * AspectRatio, 50.0);
        vec2 cellUV = fract(normCoord * gridRes);
        vec2 cellIdx = floor(normCoord * gridRes);
        vec3 cellCam = sampGl((cellIdx + 0.5) / gridRes).rgb;
        float cellLuma = dot(cellCam, vec3(0.299, 0.587, 0.114));
        float dotSize = cellLuma * (0.35 + uBass * 0.35);
        float cellDot = step(length(cellUV - 0.5), dotSize);
        typoSample = max(typoSample, vec4(vec3(cellDot * 0.8), cellDot * 0.8));
    } else {
        float maskScale = max(uTextScale * 1.8 * (1.0 + uBass * 0.25), 0.1);
        vec2 maskUV = (normCoord - vec2(0.5, uPosY)) / maskScale + vec2(0.5, 0.5);
        if (maskUV.x >= 0.0 && maskUV.x <= 1.0 && maskUV.y >= 0.0 && maskUV.y <= 1.0)
            typoSample = typoTex(maskUV);
    }

    vec3 coloredTypo = typoSample.rgb * uTextColor;
    float typoAlpha = typoSample.a;

    // 7. Mesclagem
    vec3 finalCol = camCol;
    if (uBlendMode == 0) {
        finalCol = abs(camCol - coloredTypo * typoAlpha);
    } else if (uBlendMode == 1) {
        finalCol = mix(camCol, coloredTypo, typoAlpha);
    } else if (uBlendMode == 2) {
        finalCol = camCol + coloredTypo * typoAlpha * (1.2 + uBass * 2.5);
    } else {
        vec3 bgTint = mix(vec3(0.02), abs(1.0 - camCol) * 0.3, uBW);
        finalCol = mix(bgTint, camCol, typoAlpha);
    }

    // 8. HUD (viewfinder, REC, timecode, VU)
    if (uHud > 0.02) {
        float hudAlpha = 0.0;
        vec3 hudColor = vec3(0.92, 0.94, 0.98);

        vec2 pAspect = (normCoord - 0.5) * vec2(AspectRatio, 1.0);
        vec2 halfBox = vec2(0.5 * AspectRatio - 0.07 * AspectRatio, 0.43);
        float bracket = drawCornerBrackets(pAspect, halfBox, 0.06, 0.0022);
        hudAlpha = max(hudAlpha, 1.0 - smoothstep(0.0, 0.002, bracket));

        float crosshair = drawCrosshair(pAspect, 0.025, 0.0018);
        hudAlpha = max(hudAlpha, 1.0 - smoothstep(0.0, 0.002, crosshair));

        vec2 recPos = vec2(0.06, 0.93);
        vec2 pRec = (normCoord - recPos) * vec2(AspectRatio, 1.0);
        float dRec = length(pRec) - 0.0065;
        float recBlink = step(0.0, sin(Time * 4.2));
        float recMask = (1.0 - smoothstep(0.0, 0.0015, dRec)) * recBlink;

        vec4 vuL = drawVUMeter(normCoord, vec2(0.06, 0.07), vec2(0.012, 0.13), uBass);
        vec4 vuR = drawVUMeter(normCoord, vec2(0.076, 0.07), vec2(0.012, 0.13), uHigh);

        float tcAlpha = 0.0;
        vec2 tcBoxOrigin = vec2(0.80, 0.915);
        vec2 tcBoxSize = vec2(0.14, 0.035);
        vec2 pTc = normCoord - tcBoxOrigin;
        if (pTc.x >= 0.0 && pTc.x <= tcBoxSize.x && pTc.y >= 0.0 && pTc.y <= tcBoxSize.y) {
            float totalSec = Time;
            int hh = int(mod(totalSec / 3600.0, 24.0));
            int mm = int(mod(totalSec / 60.0, 60.0));
            int ss = int(mod(totalSec, 60.0));
            int ff = int(mod(totalSec * 30.0, 30.0));
            int digits[8];
            digits[0] = hh / 10;
            digits[1] = hh % 10;
            digits[2] = mm / 10;
            digits[3] = mm % 10;
            digits[4] = ss / 10;
            digits[5] = ss % 10;
            digits[6] = ff / 10;
            digits[7] = ff % 10;
            float digitWidth = tcBoxSize.x / 11.0;
            float digitIdx = floor(pTc.x / digitWidth);
            int d = -1;
            if (digitIdx == 0.0) d = digits[0];
            else if (digitIdx == 1.0) d = digits[1];
            else if (digitIdx == 3.0) d = digits[2];
            else if (digitIdx == 4.0) d = digits[3];
            else if (digitIdx == 6.0) d = digits[4];
            else if (digitIdx == 7.0) d = digits[5];
            else if (digitIdx == 9.0) d = digits[6];
            else if (digitIdx == 10.0) d = digits[7];
            if (d >= 0) {
                vec2 digitUV = vec2(fract(pTc.x / digitWidth), pTc.y / tcBoxSize.y);
                tcAlpha = drawDigit7Seg(digitUV, d);
            } else if (digitIdx == 2.0 || digitIdx == 5.0 || digitIdx == 8.0) {
                vec2 dotUV = vec2(fract(pTc.x / digitWidth), pTc.y / tcBoxSize.y);
                float d1 = length((dotUV - vec2(0.5, 0.70)) * vec2(1.0, 2.0)) - 0.08;
                float d2 = length((dotUV - vec2(0.5, 0.30)) * vec2(1.0, 2.0)) - 0.08;
                tcAlpha = max(1.0 - smoothstep(0.0, 0.05, d1), 1.0 - smoothstep(0.0, 0.05, d2));
            }
        }

        finalCol = mix(finalCol, hudColor, hudAlpha * uHud);
        if (recMask > 0.0) finalCol = mix(finalCol, vec3(1.0, 0.1, 0.1), recMask * uHud);
        if (vuL.a > 0.0) finalCol = mix(finalCol, vuL.rgb, vuL.a * uHud);
        if (vuR.a > 0.0) finalCol = mix(finalCol, vuR.rgb, vuR.a * uHud);
        if (tcAlpha > 0.0) finalCol = mix(finalCol, vec3(0.95, 0.97, 1.0), tcAlpha * uHud);
    }

    // 9. Inversao geral
    if (uMasterInvert == 1)
        finalCol = 1.0 - finalCol;

    // 10. Ghost motion blur (feedback)
    vec4 fb = texture(uFb, toTex(imgPos()));
    fb.rgb *= fb.a;
    finalCol = mix(finalCol, fb.rgb, uGhost);

    fragColor = toStraight(finalCol, camA);
}
)";

// --- Texto -------------------------------------------------------------------------------------------------------

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Embaralha caracteres com glifos "cyber" (mesma formula do plugin, agora por caractere Unicode)
std::string scrambleString(const std::string& input, float intensity, unsigned seed)
{
    if (intensity < 0.05f)
        return input;
    static const char kGlyphs[] = "X#_/?!10@$%*&<>[+]-=:";
    const std::vector<char32_t> cps = media::decodeUtf8(input);
    std::string out;
    for (size_t i = 0; i < cps.size(); ++i) {
        char32_t cp = cps[i];
        if (cp != U' ') {
            const unsigned h = (seed * 1664525u + static_cast<unsigned>(i) * 1013904223u) ^ 0x5bf03635u;
            const float rnd = static_cast<float>(h & 0xFFFFu) / 65535.0f;
            if (rnd < intensity)
                cp = static_cast<char32_t>(kGlyphs[(h >> 16) % (sizeof(kGlyphs) - 1)]);
        }
        appendUtf8(out, cp);
    }
    return out;
}

std::filesystem::path fontOr(const char* wanted)
{
    media::FontCatalog& fonts = media::FontCatalog::instance();
    std::filesystem::path p = fonts.resolve(wanted);
    if (p.empty())
        p = fonts.resolve(fonts.defaultFile());
    return p;
}

// Uma linha de texto branca centralizada no retangulo (x, y, w, h) do bitmap RGBA (alpha = cobertura)
void drawLine(std::vector<unsigned char>& rgba, const std::string& text, const std::filesystem::path& font, float sizePx,
              float tracking, int x, int y, int w, int h)
{
    if (text.empty() || font.empty())
        return;
    media::TextStyle st;
    st.text = text;
    st.fontPath = font;
    st.sizePx = sizePx;
    st.color = {1.0f, 1.0f, 1.0f, 1.0f};
    st.hAlign = media::TextHAlign::Center;
    st.vAlign = media::TextVAlign::Middle;
    st.margin = 0;
    st.wrap = false;
    st.letterSpacing = tracking;
    media::TextBitmap bmp;
    if (!media::rasterizeText(st, w, h, 4096, bmp) || bmp.width != w || bmp.height != h)
        return;
    for (int row = 0; row < h; ++row) {
        const unsigned char* src = &bmp.rgba[static_cast<size_t>(row) * static_cast<size_t>(w) * 4];
        unsigned char* dst = &rgba[(static_cast<size_t>(y + row) * kTypoW + static_cast<size_t>(x)) * 4];
        for (int col = 0; col < w; ++col)
            dst[col * 4 + 3] = std::max(dst[col * 4 + 3], src[col * 4 + 3]);
    }
}

class TypoEffect final : public BslEffect
{
public:
    TypoEffect() : BslEffect({kTypoId, "BSL AudioReactive Typo", "Reativo ao áudio"}, describeAudioTypo()) {}

    ~TypoEffect() override
    {
        if (typoTex_)
            glDeleteTextures(1, &typoTex_);
    }

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kTypoBody; }

    void prepare(const EffectContext& ctx, int w, int h) override
    {
        fb_.ensure(w, h);
        const ParamSet& p = params();

        // --- Audio: os envelopes do plugin (sobe na hora, cai a 12/s e 18/s) ---
        const double dt = std::clamp(ctx.dt, 0.001, 0.1);
        const float sens = p.f("sensitivity") * 0.01f;
        const bool manual = p.s("audioSource") == "manual";
        const float extBass = manual ? 0.0f : bandBass(ctx);
        const float extHigh = manual ? 0.0f : bandHigh(ctx);
        const float bassReact = p.f("bassReact") * 0.01f;
        const float highReact = p.f("highReact") * 0.01f;
        const float targetBass = std::clamp((extBass + bassReact) * sens, 0.0f, 2.0f);
        const float targetHigh = std::clamp((extHigh + highReact) * sens, 0.0f, 2.0f);
        bass_ = riseFallFollow(bass_, targetBass, dt, 12.0f);
        high_ = riseFallFollow(high_, targetHigh, dt, 18.0f);

        // Semente do embaralhamento: so muda em transientes de agudo (ou com o gatilho)
        const float glitchTrigger = p.f("glitchTrigger") * 0.01f;
        if (high_ > 0.15f || glitchTrigger > 0.05f)
            scrambleSeed_ = static_cast<unsigned>(std::fmod(ctx.time, 1.0e6) * 18.0);
        updateText(p, high_ * 0.8f + glitchTrigger);
    }

    void render(const EffectContext&, int, int) override
    {
        const ParamSet& p = params();
        const float glitchTrigger = p.f("glitchTrigger") * 0.01f;
        bindTexture(1, typoTex_);
        bindTexture(2, fb_.texture());
        Shader& s = shader();
        s.setInt("uTypo", 1);
        s.setInt("uFb", 2);
        s.setInt("uTypoFlip", kTypoFlip);
        s.setInt("uMode", enumIndex(p, "mode"));
        s.setInt("uBlendMode", enumIndex(p, "blendMode"));
        s.setFloat("uTextScale", p.f("textScale"));
        s.setFloat("uPosY", p.f("posY") * 0.01f);
        s.setFloat("uBass", bass_);
        s.setFloat("uHigh", high_);
        s.setFloat("uGlitchTrigger", glitchTrigger);
        s.setFloat("uKickShake", p.f("kickShake") * 0.01f);
        s.setFloat("uRGBSplit", p.f("rgbSplit") * 0.01f);
        s.setFloat("uScrollSpeed", p.f("scrollSpeed") * 0.01f);
        s.setFloat("uRepeat", static_cast<float>(p.i("repeatCount")));
        s.setFloat("uGhost", p.f("ghostBlur") * 0.01f);
        s.setFloat("uBW", p.f("bwRave") * 0.01f);
        s.setFloat("uScanlines", p.f("scanlines") * 0.01f);
        s.setFloat("uVignetteGrain", p.f("vignetteGrain") * 0.01f);
        s.setFloat("uHud", p.f("hud") * 0.01f);
        const media::Color tc = p.c("textColor");
        s.setVec3("uTextColor", tc.r, tc.g, tc.b);
        s.setInt("uMasterInvert", p.b("masterInvert") ? 1 : 0);
    }

    void afterDraw(const EffectContext&, int w, int h, const Fbo& output) override { fb_.capture(output, w, h); }

private:
    // Rasteriza as duas linhas quando o texto, o tracking ou o embaralhamento mudam
    void updateText(const ParamSet& p, float glitch)
    {
        const std::string dj = scrambleString(p.s("djText"), glitch, scrambleSeed_);
        const std::string sub = scrambleString(p.s("subText"), glitch, scrambleSeed_ + 1337u);
        const float tracking = p.f("tracking");
        if (typoTex_ && dj == lastDj_ && sub == lastSub_ && std::abs(tracking - lastTracking_) < 0.001f)
            return;
        lastDj_ = dj;
        lastSub_ = sub;
        lastTracking_ = tracking;

        // Bitmap 2048x1024 como o do plugin: nome do DJ (Impact, 200 px) em {20,100,2028,580} e legenda (Arial negrito,
        // 68 px) em {40,620,2008,850}. RGB sempre branco, alpha = cobertura.
        std::vector<unsigned char> rgba(static_cast<size_t>(kTypoW) * kTypoH * 4, 0);
        for (size_t i = 0; i < rgba.size(); i += 4)
            rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
        drawLine(rgba, dj, fontOr("impact.ttf"), 200.0f, tracking, 20, 100, kTypoW - 40, 480);
        drawLine(rgba, sub, fontOr("arialbd.ttf"), 68.0f, tracking, 40, 620, kTypoW - 80, 230);

        if (!typoTex_) {
            glGenTextures(1, &typoTex_);
            glBindTexture(GL_TEXTURE_2D, typoTex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTypoW, kTypoH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        GLint prevAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glBindTexture(GL_TEXTURE_2D, typoTex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTypoW, kTypoH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    FeedbackBuffer fb_;
    GLuint typoTex_ = 0;
    std::string lastDj_, lastSub_;
    float lastTracking_ = -1.0f;
    unsigned scrambleSeed_ = 0;
    float bass_ = 0.0f;
    float high_ = 0.0f;
};

} // namespace

std::vector<media::ParamDesc> describeAudioTypo()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeText("djText", "Texto do DJ", "DJ NAME", false, "Texto"));
    d.push_back(makeText("subText", "Legenda", "LIVE STREAM // 140 BPM"));
    d.push_back(makeFloat("tracking", "Espaçamento das letras", 3.75f, 0.0f, 25.0f, "%.1f px"));
    d.back().tooltip = "Espaço extra entre as letras (tracking)";
    d.push_back(makeFloat("textScale", "Escala do texto", 1.075f, 0.1f, 1.6f, "%.2f x"));
    d.push_back(makeFloat("posY", "Posição Y", 48.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Altura do texto, medida de baixo para cima";

    d.push_back(makeEnum("mode", "Modo",
                         {{"boiler", "Boiler Room HUD"}, {"marquee", "Kinetic Marquee"}, {"glitch", "Glitch Cyber Typo"},
                          {"mask", "Typo Mask Cutout"}},
                         "boiler", "Modo e mesclagem"));
    d.push_back(makeEnum("blendMode", "Mesclagem",
                         {{"difference", "Diferença / Inverter"}, {"overlay", "Sobreposição (alpha)"},
                          {"add", "Neon Glow (soma)"}, {"cutout", "Máscara recortada"}},
                         "difference"));
    d.push_back(makeFloat("scrollSpeed", "Velocidade do letreiro", 40.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Velocidade das faixas do modo Kinetic Marquee";
    d.push_back(makeInt("repeatCount", "Linhas do letreiro", 4, 2, 10, "%d"));
    d.back().tooltip = "Quantidade de faixas no modo Kinetic Marquee";
    d.push_back(makeFloat("bpmSync", "Sincronia com o BPM", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Reservado: igual ao plugin original, ainda sem efeito no desenho";
    d.push_back(makeColor("textColor", "Cor do texto", {1.0f, 1.0f, 1.0f, 1.0f}));

    d.push_back(makeEnum("audioSource", "Fonte do áudio",
                         {{"analyzer", "Analisador do app (Ver > Áudio)"}, {"manual", "Manual (só os controles abaixo)"}},
                         "analyzer", "Áudio"));
    d.back().tooltip = "Manual: sem captura, graves e agudos vêm só de Reação aos graves/agudos";
    d.push_back(makeFloat("gain", "Ganho do áudio", 2.1f, 0.1f, 4.1f, "%.2f x"));
    d.push_back(makeFloat("gate", "Noise gate", 0.0075f, 0.0f, 0.15f, "%.4f"));
    d.push_back(makeFloat("bassReact", "Reação aos graves", 0.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Somado ao grave capturado (no modo Manual, é o próprio nível)";
    d.push_back(makeFloat("highReact", "Reação aos agudos", 0.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Somado ao agudo capturado (no modo Manual, é o próprio nível)";
    d.push_back(makeFloat("sensitivity", "Sensibilidade geral", 100.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("glitchTrigger", "Gatilho de glitch", 0.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Acima de 5 % embaralha o texto e abre fatias de glitch";
    d.push_back(makeFloat("kickShake", "Tremor no kick", 45.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("rgbSplit", "Separação RGB", 35.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("ghostBlur", "Rastro fantasma", 0.0f, 0.0f, 94.0f, "%.0f %%"));

    d.push_back(makeFloat("bwRave", "P&B rave", 85.0f, 0.0f, 100.0f, "%.0f %%", "Câmera"));
    d.back().tooltip = "Preto e branco de alto contraste, estilo Boiler Room";
    d.push_back(makeFloat("scanlines", "Scanlines", 30.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("vignetteGrain", "Vinheta e grão", 40.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("hud", "Elementos de HUD", 100.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "0 = só tipografia e câmera; 100 = HUD completo (cantos, REC, timecode, VU)";
    d.push_back(makeBool("masterInvert", "Inverter tudo", false));
    return d;
}

std::unique_ptr<IEffect> makeAudioTypo()
{
    return std::make_unique<TypoEffect>();
}

} // namespace bsl::fx::bsllight
