// BSL Alpha Reactor (plugin FFGL "ALPR"): aura de god rays atras do recorte (alpha) da camera e rastros infinitos por
// feedback, que reagem ao audio (graves nos rastros, agudos na aura). Reescrito do AlphaReactor.cpp original.

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include <algorithm>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

// Port do fragment shader original (GLSL 4.10 -> 3.30). Alpha premultiplicado na matematica; saida em alpha reto.
const char* const kAlphaReactorBody = R"(
uniform sampler2D uFb;
uniform float uAuraIntensity;
uniform float uAuraLength;
uniform float uTrailDecay;
uniform float uTrailZoom;
uniform float uTrailSplit;
uniform float uBass;
uniform float uHigh;

vec4 fbP(vec2 p) { vec4 c = texture(uFb, toTex(p)); c.rgb *= c.a; return c; }

void main()
{
    vec2 uv = imgPos();
    vec4 cam = sampP(uv);

    // God rays: 12 amostras do alpha da entrada em escalas crescentes
    float aura = 0.0;
    if (uAuraIntensity > 0.0) {
        float effAuraLen = uAuraLength * (1.0 + uHigh * 2.0);
        for (int i = 1; i <= 12; i++) {
            float f = float(i) / 12.0;
            float s = 1.0 + f * effAuraLen * 0.2;
            vec2 suv = (uv - 0.5) / s + 0.5;
            aura += samp(suv).a;
        }
        aura /= 12.0;
    }
    vec3 auraCol = vec3(0.0, 0.8, 1.0) * aura * uAuraIntensity * (1.0 + uHigh);

    // Rastros (atras do sujeito): escala reativa aos graves
    vec2 uvc = uv - 0.5;
    float sBase = 1.0 - uTrailZoom * (1.0 + uBass * 1.5);
    vec2 suv = uvc / sBase + 0.5;
    vec4 fb = fbP(suv);

    float split = uTrailSplit * (1.0 + uBass * 2.0);
    if (split > 0.0) {
        fb.r = fbP(uvc / (sBase - split) + 0.5).r;
        fb.b = fbP(uvc / (sBase + split) + 0.5).b;
    }

    fb.rgb *= uTrailDecay;
    fb.a *= uTrailDecay;

    fb.rgb += auraCol * (1.0 - fb.a);
    fb.a += aura * uAuraIntensity;

    // Camera OVER rastros (premultiplicado)
    vec3 rgb = cam.rgb + fb.rgb * (1.0 - cam.a);
    float a = clamp(cam.a + fb.a * (1.0 - cam.a), 0.0, 1.0);
    fragColor = toStraight(rgb, a);
}
)";

class AlphaReactorEffect final : public BslEffect
{
public:
    AlphaReactorEffect() : BslEffect({kAlphaReactorId, "BSL Alpha Reactor", "Reativo ao áudio"}, describeAlphaReactor()) {}

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kAlphaReactorBody; }

    void prepare(const EffectContext& ctx, int w, int h) override
    {
        fb_.ensure(w, h);
        const ParamSet& p = params();

        // Os mesmos envelopes do plugin: alvo = banda * reacao * 2 (ate 2); sobe na hora, cai a 12/s (graves) e 18/s (agudos)
        const double dt = std::clamp(ctx.dt, 0.001, 0.1);
        const float targetBass = std::clamp(bandBass(ctx) * (p.f("bassReact") * 0.01f) * 2.0f, 0.0f, 2.0f);
        const float targetHigh = std::clamp(bandHigh(ctx) * (p.f("highReact") * 0.01f) * 2.0f, 0.0f, 2.0f);
        bass_ = riseFallFollow(bass_, targetBass, dt, 12.0f);
        high_ = riseFallFollow(high_, targetHigh, dt, 18.0f);
    }

    void render(const EffectContext&, int, int) override
    {
        const ParamSet& p = params();
        bindTexture(1, fb_.texture());
        Shader& s = shader();
        s.setInt("uFb", 1);
        s.setFloat("uAuraIntensity", p.f("auraIntensity") * 0.01f);
        s.setFloat("uAuraLength", p.f("auraLength") * 0.01f);
        s.setFloat("uTrailDecay", p.f("trailLength") * 0.01f);
        s.setFloat("uTrailZoom", p.f("trailZoom") * 0.01f);
        s.setFloat("uTrailSplit", p.f("trailSplit") * 0.01f);
        s.setFloat("uBass", bass_);
        s.setFloat("uHigh", high_);
    }

    void afterDraw(const EffectContext&, int w, int h, const Fbo& output) override { fb_.capture(output, w, h); }

private:
    FeedbackBuffer fb_;
    float bass_ = 0.0f;
    float high_ = 0.0f;
};

} // namespace

std::vector<media::ParamDesc> describeAlphaReactor()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeFloat("gain", "Ganho do áudio", 2.1f, 0.1f, 4.1f, "%.2f x", "Áudio"));
    d.back().tooltip = "Multiplica as bandas do analisador (Ver > Áudio) só para este efeito";
    d.push_back(makeFloat("gate", "Noise gate", 0.0075f, 0.0f, 0.15f, "%.4f"));
    d.back().tooltip = "Corta o ruído de fundo: bandas abaixo deste nível viram zero";

    d.push_back(makeFloat("auraIntensity", "Intensidade da aura", 50.0f, 0.0f, 100.0f, "%.0f %%", "Aura (god rays)"));
    d.back().tooltip = "Força dos raios de luz atrás do recorte";
    d.push_back(makeFloat("auraLength", "Alcance da aura", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Até onde os raios se espalham (12 amostras)";

    d.push_back(makeFloat("trailLength", "Persistência do rastro", 78.4f, 0.0f, 98.0f, "%.1f %%", "Rastros infinitos"));
    d.back().tooltip = "Quanto do quadro anterior sobrevive a cada quadro. 0 = sem rastro";
    d.push_back(makeFloat("trailZoom", "Zoom do rastro", 5.0f, 0.0f, 10.0f, "%.2f %%"));
    d.back().tooltip = "Aproxima o rastro a cada quadro; os graves aumentam o efeito";
    d.push_back(makeFloat("trailSplit", "Separação RGB do rastro", 1.0f, 0.0f, 2.0f, "%.2f %%"));
    d.back().tooltip = "Aberração cromática nos ecos";

    d.push_back(makeFloat("bassReact", "Reação aos graves", 50.0f, 0.0f, 100.0f, "%.0f %%", "Reatividade"));
    d.back().tooltip = "Quanto o grave (kick) faz os rastros pulsarem";
    d.push_back(makeFloat("highReact", "Reação aos agudos", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Quanto os agudos (hi-hats, synths) fazem a aura pulsar";
    return d;
}

std::unique_ptr<IEffect> makeAlphaReactor()
{
    return std::make_unique<AlphaReactorEffect>();
}

} // namespace bsl::fx::bsllight
