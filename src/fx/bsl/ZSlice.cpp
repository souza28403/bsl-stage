#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "output/OutputSetup.h"
#include "render/ZoneMasks.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

const char* const kZoneGlsl = R"(
uniform vec4 uZT;
uniform int uZones;
vec4 zoneWeights(float d)
{
    float f = max(uZT.w, 1.0e-4);
    float c1 = smoothstep(uZT.x - f, uZT.x + f, d);
    float c2 = smoothstep(uZT.y - f, uZT.y + f, d);
    float c3 = uZones > 3 ? smoothstep(uZT.z - f, uZT.z + f, d) : 0.0;
    return vec4(c1, c2 - c1, uZones > 3 ? c3 - c2 : 1.0 - c2, uZones > 3 ? 1.0 - c3 : 0.0);
}
)";

const char* const kZSliceBody = R"(
uniform sampler2D uDepth;
uniform int uView;
uniform int uSolo;
uniform float uTint;

void main()
{
    vec2 uv = imgPos();
    vec4 src = samp(uv);
    vec4 zw = zoneWeights(texture(uDepth, uv).r);
    if (uView == 1) {
        float w = uSolo == 0 ? zw.x : (uSolo == 1 ? zw.y : (uSolo == 2 ? zw.z : zw.w));
        fragColor = vec4(src.rgb, src.a * w);
        return;
    }
    vec3 col = zw.x * vec3(1.00, 0.25, 0.22) + zw.y * vec3(0.28, 0.90, 0.35) + zw.z * vec3(0.25, 0.52, 1.00)
             + zw.w * vec3(0.92, 0.30, 0.95);
    float lum = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    vec3 tinted = col * (0.30 + 0.85 * lum);
    fragColor = vec4(mix(src.rgb, tinted, uTint), src.a);
}
)";

const char* const kMaskHeader = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uDepth;
)";
const char* const kMaskBody = R"(
void main()
{
    float d = texture(uDepth, vec2(vUv.x, 1.0 - vUv.y)).r;
    fragColor = zoneWeights(d);
}
)";

class ZSliceEffect final : public AiEffect
{
public:
    ZSliceEffect() : AiEffect({kZSliceId, "BSL Z-Slice LED Mapper", "Mapeamento"}, describeZSlice()) {}
    ~ZSliceEffect() override { ZoneMasks::instance().retract(this); }

    media::SourceStatus status() const override
    {
        const media::SourceStatus base = AiEffect::status();
        if (base.level != media::SourceStatus::Level::Ok || note_.empty())
            return base;
        return {media::SourceStatus::Level::Info, note_};
    }

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kZoneGlsl + kZSliceBody; }
    unsigned aiNeed() const override { return ai::kNeedDepth; }

    void prepareAi(const EffectContext& ctx, int w, int h, const ai::AiFrame& frame) override
    {
        const ParamSet& p = params();
        depthTex_ = frame.depth;
        zones_ = enumIndex(p, "zones") == 1 ? 4 : 3;
        t1_ = p.f("t1") * 0.01f;
        t2_ = std::min(p.f("t2") * 0.01f, t1_);
        t3_ = std::min(p.f("t3") * 0.01f, t2_);
        halfFeather_ = p.f("feather") * 0.005f;
        view_ = enumIndex(p, "view");
        solo_ = enumIndex(p, "solo");

        const bool routes = ctx.chain == ChainKind::Layer || ctx.chain == ChainKind::Master;
        if (ctx.chain == ChainKind::Clip)
            note_ = "Para rotear os slices, use o Z-Slice na cadeia da camada ou do master";
        else
            note_.clear();
        if (routes && !ctx.preview && !ctx.topOrigin) {
            if (renderMask(w, h)) {
                const int source = ctx.chain == ChainKind::Master ? kZoneSourceMaster : ctx.layerIndex;
                ZoneMasks::instance().publish(this, source,
                                              {mask_->texture(), mask_->width(), mask_->height(), zones_});
            }
        }
        if (view_ == 0)
            passThroughThisFrame();
    }

    void render(const EffectContext&, int, int) override
    {
        Shader& s = shader();
        bindTexture(1, depthTex_);
        s.setInt("uDepth", 1);
        setZoneUniforms(s);
        s.setInt("uView", view_);
        s.setInt("uSolo", solo_);
        s.setFloat("uTint", params().f("tint") * 0.01f);
    }

private:
    void setZoneUniforms(Shader& s) const
    {
        s.setVec4("uZT", t1_, t2_, t3_, halfFeather_);
        s.setInt("uZones", zones_);
    }

    bool renderMask(int w, int h)
    {
        const int mw = std::clamp(w / 2, 32, 960);
        const int mh = std::max(18, static_cast<int>(std::lround(static_cast<double>(mw) * h / std::max(w, 1))));
        try {
            if (!mask_ || mask_->width() != mw || mask_->height() != mh)
                mask_ = std::make_unique<Fbo>(mw, mh);
            if (!maskShader_) {
                const std::string src = std::string(kMaskHeader) + kZoneGlsl + kMaskBody;
                maskShader_ = std::make_unique<Shader>(kFxVertexSource, src.c_str());
            }
        } catch (const std::exception&) {
            mask_.reset();
            return false;
        }
        bindOutput(*mask_, mw, mh);
        maskShader_->use();
        maskShader_->setInt("uDepth", 0);
        setZoneUniforms(*maskShader_);
        bindTexture(0, depthTex_);
        maskVao_.draw();
        bindTexture(0, 0);
        glUseProgram(0);
        return true;
    }

    GLuint depthTex_ = 0;
    int zones_ = 3;
    float t1_ = 0.66f, t2_ = 0.33f, t3_ = 0.15f, halfFeather_ = 0.04f;
    int view_ = 0, solo_ = 0;
    std::string note_;
    std::unique_ptr<Fbo> mask_;
    std::unique_ptr<Shader> maskShader_;
    ScreenVao maskVao_;
};

}

std::vector<media::ParamDesc> describeZSlice()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("zones", "Número de zonas", {{"3", "3 zonas (Frente, Meio, Fundo)"}, {"4", "4 zonas (+ Zona 4)"}}, "3", "Zonas (IA)"));
    d.back().tooltip = "As zonas vão da mais perto para a mais longe: Frente, Meio, Fundo e, com 4 zonas, a Zona 4 (a mais distante)";
    d.push_back(makeFloat("t1", "Limiar Frente / Meio", 66.0f, 1.0f, 99.0f, "%.0f %%"));
    d.back().tooltip = "Profundidade mínima para ser Frente (100 % = colado na câmera). Os limiares se ajustam sozinhos para ficar em ordem";
    d.push_back(makeFloat("t2", "Limiar Meio / Fundo", 33.0f, 1.0f, 99.0f, "%.0f %%"));
    d.push_back(makeFloat("t3", "Limiar Fundo / Zona 4", 15.0f, 1.0f, 99.0f, "%.0f %%"));
    d.back().enabledIf = "zones=4";
    d.push_back(makeFloat("feather", "Suavização entre zonas", 8.0f, 0.0f, 40.0f, "%.0f %%"));
    d.back().tooltip = "Largura da transição entre zonas vizinhas. 0 = corte seco; mais = a fatia some aos poucos";

    d.push_back(makeEnum("view", "Visualização", {{"off", "Imagem inteira"}, {"solo", "Mostrar uma zona só"}, {"colors", "Zonas coloridas"}}, "off", "Visualização"));
    d.back().tooltip = "O roteamento dos slices funciona em qualquer visualização: aqui você só escolhe o que vê na camada";
    d.push_back(makeEnum("solo", "Zona mostrada", {{"front", "Frente"}, {"middle", "Meio"}, {"back", "Fundo"}, {"zone4", "Zona 4"}}, "front"));
    d.back().enabledIf = "view=solo";
    d.push_back(makeFloat("tint", "Opacidade das cores", 65.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().enabledIf = "view=colors";
    return d;
}

std::unique_ptr<IEffect> makeZSlice()
{
    return std::make_unique<ZSliceEffect>();
}

}
