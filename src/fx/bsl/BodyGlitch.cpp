#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

constexpr int kMaxClones = 6;
constexpr int kRingMaxW = 640;

const char* const kRegionGlsl = R"(
uniform sampler2D uMask;
uniform sampler2D uDepth;
uniform float uNear;
uniform float uSoft;
uniform float uMaskCut;
uniform float uUseMask;
float regionAt(vec2 uv)
{
    float soft = max(uSoft, 0.01);
    float near = smoothstep(uNear - soft, uNear + soft, texture(uDepth, uv).r);
    float person = uUseMask > 0.5 ? smoothstep(uMaskCut - soft, uMaskCut + soft, texture(uMask, uv).r) : 1.0;
    return near * person;
}
)";

const char* const kGlitchBody = R"(
uniform sampler2D uFb;
uniform sampler2D uC0, uC1, uC2, uC3, uC4, uC5;
uniform int uMode;
uniform float uAspect;
uniform float uMix;
uniform vec3 uColor;
uniform float uTint;
uniform float uDebug;
uniform float uAmount;
uniform float uMoshAmt, uMoshTol;
uniform int uCloneN;
uniform float uCloneOff, uCloneAlpha;
uniform float uCells, uLevels, uLine, uRelief;
uniform float uSlices, uShift, uGridMode;

vec4 cloneAt(int i, vec2 uv)
{
    if (i == 0) return texture(uC0, uv);
    if (i == 1) return texture(uC1, uv);
    if (i == 2) return texture(uC2, uv);
    if (i == 3) return texture(uC3, uv);
    if (i == 4) return texture(uC4, uv);
    return texture(uC5, uv);
}

float hash21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) { return vec2(hash21(p), hash21(p + 17.31)); }

vec3 rgb2yuv(vec3 c)
{
    return vec3(0.299 * c.r + 0.587 * c.g + 0.114 * c.b, -0.14713 * c.r - 0.28886 * c.g + 0.436 * c.b,
                0.615 * c.r - 0.51499 * c.g - 0.10001 * c.b);
}

vec3 datamosh(vec2 uv, vec3 cam)
{
    vec4 lastP = texture(uFb, toTex(uv));
    if (lastP.a < 0.01)
        return cam;
    vec3 cy = rgb2yuv(cam), ly = rgb2yuv(lastP.rgb);
    vec2 flow = vec2(cy.y - ly.y, cy.z - ly.z);
    vec3 moshed = texture(uFb, toTex(uv - vec2(flow.x, -flow.y) * uMoshAmt * 0.25)).rgb;
    float diff = length(cy - ly);
    return diff < uMoshTol ? moshed : mix(moshed, cam, 0.5);
}

vec3 wireframe(vec2 uv, vec3 cam)
{
    float d = texture(uDepth, uv).r;
    float lv = d * uLevels;
    float contour = 1.0 - smoothstep(0.0, max(fwidth(lv), 1.0e-4) * uLine, abs(fract(lv + 0.5) - 0.5));
    vec2 g = vec2(uv.x * uAspect, uv.y) * uCells + vec2(0.55, 1.0) * d * uRelief * uCells;
    vec2 gd = abs(fract(g + 0.5) - 0.5);
    vec2 fw = max(fwidth(g), vec2(1.0e-4));
    vec2 a = 1.0 - smoothstep(vec2(0.0), fw * uLine, gd);
    float grid = max(a.x, a.y);
    return cam * 0.28 + uColor * clamp(max(grid * 0.9, contour), 0.0, 1.0);
}

vec3 slicer(vec2 uv, vec3 cam)
{
    float t = floor(uTime * 8.0);
    vec2 p = vec2(uv.x * uAspect, uv.y) * uSlices;
    vec2 id = floor(p);
    if (uGridMode < 0.5) {
        vec2 ip = floor(p);
        float best = 1.0e9;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                vec2 n = ip + vec2(float(i), float(j));
                float dd = length(p - (n + hash22(n)));
                if (dd < best) { best = dd; id = n; }
            }
    }
    float on = step(0.4, hash21(id + t * 1.7));
    vec2 off = (hash22(id + t) - 0.5) * 2.0 * uShift * on;
    return vec3(samp(uv + off * 1.25).r, samp(uv + off).g, samp(uv + off * 0.75).b);
}

void main()
{
    vec2 uv = imgPos();
    vec4 cam = samp(uv);
    float reg = regionAt(uv);
    vec3 g = cam.rgb;

    if (uMode == 0) g = datamosh(uv, cam.rgb);
    else if (uMode == 2) g = wireframe(uv, cam.rgb);
    else if (uMode == 3) g = slicer(uv, cam.rgb);

    if (uMode == 0 || uMode == 3)
        g = mix(g, uColor * (0.35 + dot(g, vec3(0.3333)) * 1.3), uTint);

    float k = reg * uMix * uAmount;
    vec3 rgb = mix(cam.rgb, g, k);

    if (uMode == 1) {
        for (int i = uCloneN - 1; i >= 0; --i) {
            float f = float(i + 1);
            vec4 c = cloneAt(i, toTex(uv - vec2(uCloneOff * f, 0.0)));
            float a = c.a * uMix * uAmount * uCloneAlpha * (1.0 - (f - 1.0) / (float(uCloneN) + 1.0));
            vec3 col = c.rgb / max(c.a, 1.0e-3);
            col = mix(col, uColor * (0.35 + dot(col, vec3(0.3333)) * 1.3), uTint);
            rgb = mix(rgb, col, clamp(a, 0.0, 1.0));
        }
    }
    if (uDebug > 0.5)
        rgb = mix(rgb, vec3(1.0, 0.1, 0.2), reg * 0.5);
    fragColor = vec4(clamp(rgb, 0.0, 1.0), cam.a);
}
)";

const char* const kCaptureBody = R"(
void main()
{
    vec2 uv = imgPos();
    vec4 cam = samp(uv);
    float reg = regionAt(uv);
    fragColor = vec4(cam.rgb * reg, reg);
}
)";

class BodyGlitchEffect final : public AiEffect
{
public:
    BodyGlitchEffect() : AiEffect({kBodyGlitchId, "BSL Cybernetic Body-Parts Glitch", "Glitch"}, describeBodyGlitch()) {}

protected:
    std::string fragmentSource() const override
    {
        return std::string(kBslPrelude) + kRegionGlsl + kGlitchBody;
    }

    unsigned aiNeed() const override
    {
        return params().b("usePerson") ? (ai::kNeedDepth | ai::kNeedPerson) : ai::kNeedDepth;
    }

    void prepareAi(const EffectContext& ctx, int w, int h, const ai::AiFrame& frame) override
    {
        const ParamSet& p = params();
        depthTex_ = frame.depth;
        maskTex_ = frame.mask;
        aspect_ = static_cast<float>(w) / static_cast<float>(std::max(h, 1));
        top_ = ctx.topOrigin;
        mode_ = enumIndex(p, "mode");
        useMask_ = p.b("usePerson") && frame.mask != 0;

        if (mode_ == 0) {
            fb_.ensure(w, h);
        } else {
            fb_.reset();
        }
        if (mode_ == 1) {
            cloneN_ = std::clamp(p.i("cloneCount"), 1, kMaxClones);
            delay_ = std::clamp(p.i("cloneDelay"), 1, 20);
            ensureRing(w, h, cloneN_ * delay_ + 1);
        } else {
            ring_.clear();
            ringLen_ = 0;
        }
    }

    void render(const EffectContext&, int, int) override
    {
        const ParamSet& p = params();
        Shader& s = shader();
        bindTexture(1, maskTex_ ? maskTex_ : depthTex_);
        bindTexture(2, depthTex_);
        bindTexture(3, fb_.texture());
        s.setInt("uMask", 1);
        s.setInt("uDepth", 2);
        s.setInt("uFb", 3);
        setRegionUniforms(s, p);
        s.setInt("uMode", mode_);
        s.setFloat("uAspect", aspect_);
        s.setFloat("uMix", p.f("mix") * 0.01f);
        s.setFloat("uAmount", p.f("amount") * 0.01f);
        const media::Color c = p.c("glitchColor");
        s.setVec3("uColor", c.r, c.g, c.b);
        s.setFloat("uTint", p.f("tint") * 0.01f);
        s.setFloat("uDebug", p.b("showRegion") ? 1.0f : 0.0f);
        s.setFloat("uMoshAmt", p.f("moshAmount") * 0.01f);
        s.setFloat("uMoshTol", 0.02f + p.f("moshDecay") * 0.01f * 0.3f);
        s.setInt("uCloneN", mode_ == 1 ? cloneN_ : 0);
        s.setFloat("uCloneOff", p.f("cloneOffset") * 0.01f);
        s.setFloat("uCloneAlpha", p.f("cloneOpacity") * 0.01f);
        s.setFloat("uCells", p.f("gridCells"));
        s.setFloat("uLevels", p.f("contours"));
        s.setFloat("uLine", p.f("lineWidth"));
        s.setFloat("uRelief", p.f("relief") * 0.01f);
        s.setFloat("uSlices", p.f("sliceCells"));
        s.setFloat("uShift", p.f("sliceShift") * 0.01f);
        s.setFloat("uGridMode", p.b("sliceGrid") ? 1.0f : 0.0f);

        static const char* kNames[kMaxClones] = {"uC0", "uC1", "uC2", "uC3", "uC4", "uC5"};
        for (int i = 0; i < kMaxClones; ++i) {
            GLuint tex = 0;
            if (mode_ == 1 && i < cloneN_ && ringLen_ > 0) {
                const int slot = ((head_ - (i + 1) * delay_) % ringLen_ + ringLen_) % ringLen_;
                tex = ring_[static_cast<size_t>(slot)]->texture();
            }
            bindTexture(4 + i, tex);
            s.setInt(kNames[i], 4 + i);
        }
    }

    void afterDraw(const EffectContext&, int w, int h, const Fbo& output) override
    {
        for (int unit = 9; unit >= 4; --unit)
            bindTexture(unit, 0);
        if (mode_ == 0)
            fb_.capture(output, w, h);
        else if (mode_ == 1 && ringLen_ > 0)
            captureClone(w, h);
    }

private:
    void setRegionUniforms(Shader& s, const ParamSet& p)
    {
        s.setFloat("uNear", p.f("near") * 0.01f);
        s.setFloat("uSoft", p.f("soft") * 0.01f);
        s.setFloat("uMaskCut", p.f("maskCut") * 0.01f);
        s.setFloat("uUseMask", useMask_ ? 1.0f : 0.0f);
    }

    void ensureRing(int w, int h, int len)
    {
        const int rw = std::min(w, kRingMaxW);
        const int rh = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * rw / std::max(w, 1))));
        if (ringLen_ == len && !ring_.empty() && ring_[0]->width() == rw && ring_[0]->height() == rh)
            return;
        ring_.clear();
        GLint prevFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
        for (int i = 0; i < len; ++i) {
            ring_.push_back(std::make_unique<Fbo>(rw, rh));
            ring_.back()->bind();
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        ringLen_ = len;
        head_ = 0;
    }

    void captureClone(int , int )
    {
        if (!capShader_) {
            try {
                const std::string src = std::string(kBslPrelude) + kRegionGlsl + kCaptureBody;
                capShader_ = std::make_unique<Shader>(kFxVertexSource, src.c_str());
            } catch (const std::exception&) {
                ringLen_ = 0;
                return;
            }
        }
        head_ = (head_ + 1) % ringLen_;
        Fbo& dst = *ring_[static_cast<size_t>(head_)];
        bindOutput(dst, dst.width(), dst.height());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        capShader_->use();
        capShader_->setInt("uTex", 0);
        capShader_->setVec2("uSize", static_cast<float>(dst.width()), static_cast<float>(dst.height()));
        capShader_->setInt("uTop", top_ ? 1 : 0);
        capShader_->setInt("uMask", 1);
        capShader_->setInt("uDepth", 2);
        setRegionUniforms(*capShader_, params());
        bindTexture(0, inputTexture());
        bindTexture(1, maskTex_ ? maskTex_ : depthTex_);
        bindTexture(2, depthTex_);
        capVao_.draw();
        for (int unit = 2; unit >= 0; --unit)
            bindTexture(unit, 0);
        glUseProgram(0);
    }

    GLuint depthTex_ = 0;
    GLuint maskTex_ = 0;
    float aspect_ = 1.0f;
    bool top_ = true;
    bool useMask_ = true;
    int mode_ = 0;
    int cloneN_ = 0;
    int delay_ = 1;
    FeedbackBuffer fb_;
    std::vector<std::unique_ptr<Fbo>> ring_;
    int ringLen_ = 0;
    int head_ = 0;
    std::unique_ptr<Shader> capShader_;
    ScreenVao capVao_;
};

}

std::vector<media::ParamDesc> describeBodyGlitch()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("mode", "Modo do glitch",
                         {{"datamosh", "Datamosh"}, {"clones", "Clones atrasados"}, {"wireframe", "Wireframe 3D"}, {"slicer", "Slicer Voronoi / grade"}},
                         "datamosh", "Região (IA)"));
    d.back().tooltip = "O glitch só acontece na região isolada pela IA: pessoa perto da câmera";
    d.push_back(makeFloat("near", "Limiar de \"perto\"", 60.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Profundidade mínima para entrar na região (100 % = colado na câmera). Mãos estendidas e rosto passam primeiro";
    d.push_back(makeBool("usePerson", "Só pessoas (máscara)", true));
    d.back().tooltip = "Liga: região = pessoa E perto (usa o modelo de segmentação). Desligado: tudo que está perto, só com a profundidade";
    d.push_back(makeFloat("maskCut", "Corte da máscara", 50.0f, 5.0f, 95.0f, "%.0f %%"));
    d.back().enabledIf = "usePerson";
    d.push_back(makeFloat("soft", "Suavidade da borda", 10.0f, 1.0f, 40.0f, "%.0f %%"));
    d.push_back(makeBool("showRegion", "Mostrar região", false));
    d.back().tooltip = "Pinta de vermelho a região isolada, para ajustar o limiar";

    d.push_back(makeFloat("amount", "Intensidade", 100.0f, 0.0f, 100.0f, "%.0f %%", "Mistura e cor"));
    d.push_back(makeFloat("mix", "Mistura com o original", 100.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeColor("glitchColor", "Cor do glitch", {0.10f, 1.00f, 0.85f, 1.0f}));
    d.push_back(makeFloat("tint", "Força da cor", 35.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Quanto a cor do glitch tinge a região (no wireframe, a cor das linhas é sempre a do glitch)";

    d.push_back(makeFloat("moshAmount", "Quantidade de mosh", 60.0f, 0.0f, 100.0f, "%.0f %%", "Datamosh"));
    d.back().enabledIf = "mode=datamosh";
    d.push_back(makeFloat("moshDecay", "Persistência do mosh", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().enabledIf = "mode=datamosh";
    d.back().tooltip = "Valores altos demoram a atualizar a imagem e esticam as cores";

    d.push_back(makeInt("cloneCount", "Número de clones", 3, 1, kMaxClones, "%d", "Clones atrasados"));
    d.back().enabledIf = "mode=clones";
    d.push_back(makeInt("cloneDelay", "Atraso entre clones", 6, 1, 20, "%d quadros"));
    d.back().enabledIf = "mode=clones";
    d.push_back(makeFloat("cloneOffset", "Deslocamento", 4.0f, -30.0f, 30.0f, "%.1f %%"));
    d.back().enabledIf = "mode=clones";
    d.back().tooltip = "Quanto cada clone se afasta do anterior, em % da largura";
    d.push_back(makeFloat("cloneOpacity", "Opacidade dos clones", 80.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().enabledIf = "mode=clones";

    d.push_back(makeFloat("gridCells", "Células da grade", 24.0f, 4.0f, 80.0f, "%.0f", "Wireframe 3D"));
    d.back().enabledIf = "mode=wireframe";
    d.push_back(makeFloat("contours", "Curvas de nível", 14.0f, 2.0f, 40.0f, "%.0f"));
    d.back().enabledIf = "mode=wireframe";
    d.push_back(makeFloat("lineWidth", "Espessura das linhas", 1.2f, 0.5f, 4.0f, "%.1f"));
    d.back().enabledIf = "mode=wireframe";
    d.push_back(makeFloat("relief", "Relevo da grade", 40.0f, 0.0f, 150.0f, "%.0f %%"));
    d.back().enabledIf = "mode=wireframe";
    d.back().tooltip = "Quanto a grade entorta conforme a profundidade";

    d.push_back(makeFloat("sliceCells", "Número de células", 10.0f, 3.0f, 60.0f, "%.0f", "Slicer"));
    d.back().enabledIf = "mode=slicer";
    d.push_back(makeFloat("sliceShift", "Deslocamento", 6.0f, 0.0f, 40.0f, "%.1f %%"));
    d.back().enabledIf = "mode=slicer";
    d.push_back(makeBool("sliceGrid", "Grade em vez de Voronoi", false));
    d.back().enabledIf = "mode=slicer";
    return d;
}

std::unique_ptr<IEffect> makeBodyGlitch()
{
    return std::make_unique<BodyGlitchEffect>();
}

}
