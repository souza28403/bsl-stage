#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SliceFeed.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

constexpr int kMaxSlices = 8;

const char* const kSliceBody = R"(
uniform vec4 uRect[8];
uniform vec4 uA[8];
uniform vec4 uB[8];

float sdRoundedBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

vec2 rotate(vec2 v, float a)
{
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c) * v;
}

void main()
{
    vec2 uvPixels = glPos() * uSize;
    vec4 videoSample = sampGl(glPos());
    vec4 finalColor = vec4(0.0);
    float Time = uTime;

    for (int i = 0; i < 8; i++) {
        if (uA[i].x < 0.5 || uRect[i].z < 1.0) continue;
        vec3 color = uB[i].yzw;
        float speed = uB[i].x;
        float mode = uA[i].y;

        vec2 sSize = uRect[i].zw;
        vec2 sCenter = uRect[i].xy + sSize * 0.5;
        sCenter.y = uSize.y - sCenter.y;

        vec2 p = uvPixels - sCenter;
        float maxR = min(sSize.x, sSize.y) * 0.5;
        float r = uA[i].z * maxR;
        vec2 b = sSize * 0.5;
        float d = sdRoundedBox(p, b, r);

        float strokeWidth = uA[i].w;
        float fill = 1.0 - smoothstep(-0.5, 0.5, d);
        float border = 1.0 - smoothstep(strokeWidth * 0.5 - 1.0, strokeWidth * 0.5 + 1.0, abs(d));

        vec4 sliceColor = vec4(0.0);
        if (mode < 0.1) {
            sliceColor = vec4(color, 1.0) * border;
        } else if (mode < 1.1) {
            float angle = Time * speed * 2.0;
            vec2 rotP = rotate(p, angle);
            float grad = rotP.x / (b.x * 2.0) + 0.5;
            sliceColor = vec4(color * grad, 1.0) * border;
        } else if (mode < 2.1) {
            float angle = atan(p.y, p.x);
            float radar = mod(angle + Time * speed * 5.0, 6.28318) / 6.28318;
            sliceColor = vec4(color * radar, radar) * border;
        } else if (mode < 3.1) {
            float angle = atan(p.y, p.x);
            float radar = mod(angle + Time * speed * 5.0, 6.28318) / 6.28318;
            float dash = step(0.8, radar);
            sliceColor = vec4(color * dash, dash) * border;
        } else if (mode < 4.1) {
            float pulse = sin(Time * speed * 10.0) * 0.5 + 0.5;
            sliceColor = vec4(color * pulse, pulse) * fill;
        } else if (mode < 5.1) {
            sliceColor = videoSample * border;
        } else if (mode < 6.1) {
            float n = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
            float sparkle = step(0.9, n) * sin(Time * speed * 20.0 + n * 10.0);
            sliceColor = vec4(color * sparkle, sparkle) * border;
        } else if (mode < 7.1) {
            float pulse = sin(Time * speed * 5.0) * 0.5 + 0.5;
            float softBorder = 1.0 - smoothstep(strokeWidth * 0.1, strokeWidth * 2.0, abs(d));
            sliceColor = vec4(color * pulse, pulse) * softBorder;
        } else if (mode < 8.1) {
            float angle = atan(p.y, p.x);
            float wave = angle + Time * speed * 5.0;
            vec3 rgb = vec3(sin(wave) * 0.5 + 0.5, sin(wave + 2.094) * 0.5 + 0.5, sin(wave + 4.188) * 0.5 + 0.5);
            sliceColor = vec4(rgb, 1.0) * border;
        } else if (mode < 9.1) {
            float angle = atan(p.y, p.x);
            float dash = step(0.5, fract(angle * 10.0 + Time * speed * 5.0));
            sliceColor = vec4(color * dash, dash) * border;
        } else if (mode < 10.1) {
            float jitter = fract(sin(floor(p.y / 10.0) * 100.0 + Time * speed * 10.0) * 43758.5453) * 2.0 - 1.0;
            float glitchBorder = 1.0 - smoothstep(strokeWidth * 0.5 - 1.0, strokeWidth * 0.5 + 1.0, abs(d + jitter * strokeWidth * 0.5));
            sliceColor = vec4(color, 1.0) * glitchBorder;
        }
        finalColor += sliceColor;
    }
    finalColor = clamp(finalColor, 0.0, 1.0);
    fragColor = toStraight(finalColor.rgb, finalColor.a);
}
)";

void hueColor(float hue01, float rgb[3])
{
    const float h = hue01 * 6.0f;
    rgb[0] = std::clamp(std::abs(h - 3.0f) - 1.0f, 0.0f, 1.0f);
    rgb[1] = std::clamp(2.0f - std::abs(h - 2.0f), 0.0f, 1.0f);
    rgb[2] = std::clamp(2.0f - std::abs(h - 4.0f), 0.0f, 1.0f);
}

class SliceRefinerEffect final : public BslEffect
{
public:
    SliceRefinerEffect() : BslEffect({kSliceRefinerId, "BSL Slice Refiner 2", "Slices"}, describeSliceRefiner()) {}

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kSliceBody; }

    void render(const EffectContext& ctx, int w, int h) override
    {
        const ParamSet& p = params();
        refreshSlices(p, ctx);

        float rect[kMaxSlices * 4] = {};
        float a[kMaxSlices * 4] = {};
        float b[kMaxSlices * 4] = {};
        const float kx = canvasW_ > 0 ? static_cast<float>(w) / static_cast<float>(canvasW_) : 1.0f;
        const float ky = canvasH_ > 0 ? static_cast<float>(h) / static_cast<float>(canvasH_) : 1.0f;
        for (int i = 0; i < kMaxSlices; ++i) {
            const std::string k = "s" + std::to_string(i + 1);
            const bool exists = static_cast<size_t>(i) < slices_.size();
            const bool on = exists && p.b(k + "Enable");
            if (exists) {
                const FeedSlice& s = slices_[static_cast<size_t>(i)];
                rect[i * 4 + 0] = s.x * kx;
                rect[i * 4 + 1] = s.y * ky;
                rect[i * 4 + 2] = s.w * kx;
                rect[i * 4 + 3] = s.h * ky;
            }
            a[i * 4 + 0] = on ? 1.0f : 0.0f;
            a[i * 4 + 1] = static_cast<float>(enumIndex(p, k + "Mode"));
            a[i * 4 + 2] = p.f(k + "Roundness") * 0.01f;
            a[i * 4 + 3] = p.f(k + "Stroke");
            float rgb[3];
            hueColor(p.f(k + "Hue") / 360.0f, rgb);
            b[i * 4 + 0] = p.f(k + "Speed") * 0.01f;
            b[i * 4 + 1] = rgb[0];
            b[i * 4 + 2] = rgb[1];
            b[i * 4 + 3] = rgb[2];
        }
        const GLuint prog = shader().id();
        glUniform4fv(glGetUniformLocation(prog, "uRect"), kMaxSlices, rect);
        glUniform4fv(glGetUniformLocation(prog, "uA"), kMaxSlices, a);
        glUniform4fv(glGetUniformLocation(prog, "uB"), kMaxSlices, b);
    }

private:
    void refreshSlices(const ParamSet& p, const EffectContext& ctx)
    {
        using Clock = std::chrono::steady_clock;
        if (p.s("source") == "xml") {
            std::string path = p.s("xmlFile");
            const auto now = Clock::now();
            if (path != xmlPath_ || now - lastXmlRead_ > std::chrono::seconds(2)) {
                lastXmlRead_ = now;
                std::error_code ec;
                const std::filesystem::path fp{std::u8string(path.begin(), path.end())};
                const auto mtime = std::filesystem::last_write_time(fp, ec);
                if (path != xmlPath_ || (!ec && mtime != xmlTime_)) {
                    xmlPath_ = path;
                    xmlTime_ = ec ? std::filesystem::file_time_type{} : mtime;
                    slices_.clear();
                    std::ifstream in(fp, std::ios::binary);
                    if (in) {
                        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        if (text.size() < (64u << 20))
                            slices_ = parseSliceXml(text);
                    }
                }
            }
            canvasW_ = ctx.canvasWidth;
            canvasH_ = ctx.canvasHeight;
            fromXml_ = true;
            return;
        }
        if (fromXml_) {
            xmlPath_.clear();
            fromXml_ = false;
            feedVersion_ = ~0ull;
        }
        SliceFeed& feed = SliceFeed::instance();
        if (feed.version() != feedVersion_) {
            feedVersion_ = feed.version();
            slices_ = feed.slices();
            canvasW_ = feed.canvasWidth();
            canvasH_ = feed.canvasHeight();
        }
    }

    std::vector<FeedSlice> slices_;
    int canvasW_ = 0, canvasH_ = 0;
    std::uint64_t feedVersion_ = ~0ull;
    bool fromXml_ = false;
    std::string xmlPath_;
    std::filesystem::file_time_type xmlTime_{};
    std::chrono::steady_clock::time_point lastXmlRead_{};
};

}

std::vector<media::ParamDesc> describeSliceRefiner()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("source", "Origem dos slices",
                         {{"output", "Saída do BSL Stage"}, {"xml", "XML de slices"}}, "output", "Slices"));
    d.back().tooltip = "Saída: os slices da aba Saída / Mapeamento do projeto. XML: importa um arquivo XML de slices";
    d.push_back(makeFile("xmlFile", "XML de slices", "XML de slices (*.xml)|*.xml"));
    d.back().enabledIf = "source=xml";
    d.back().tooltip = "Arquivo XML com os slices";

    static const std::vector<media::ParamOption> kModes = {
        {"neon", "Neon sólido"},     {"gradient", "Gradiente"},   {"radar", "Radar"},
        {"chaser", "Perseguidor"},   {"bumper", "Pulsa"}, {"video", "Máscara de vídeo"},
        {"sparkles", "Brilhos"},     {"glow", "Glow pulsante"},   {"rgbwave", "Onda RGB"},
        {"dotted", "Pontilhado"},    {"glitch", "Glitch"}};
    for (int i = 1; i <= kMaxSlices; ++i) {
        const std::string k = "s" + std::to_string(i);
        const std::string n = std::to_string(i);
        d.push_back(makeBool(k + "Enable", "Ligado", true, "Slice " + n));
        d.push_back(makeEnum(k + "Mode", "Modo", kModes, "neon"));
        d.back().enabledIf = k + "Enable";
        d.push_back(makeFloat(k + "Roundness", "Arredondamento", 0.0f, 0.0f, 100.0f, "%.0f %%"));
        d.back().enabledIf = k + "Enable";
        d.push_back(makeFloat(k + "Stroke", "Espessura", 10.0f, 0.0f, 100.0f, "%.0f px"));
        d.back().enabledIf = k + "Enable";
        d.push_back(makeFloat(k + "Speed", "Velocidade", 50.0f, 0.0f, 100.0f, "%.0f %%"));
        d.back().enabledIf = k + "Enable";
        d.push_back(makeFloat(k + "Hue", "Cor (matiz)", 180.0f, 0.0f, 360.0f, "%.0f °"));
        d.back().enabledIf = k + "Enable";
    }
    return d;
}

std::unique_ptr<IEffect> makeSliceRefiner()
{
    return std::make_unique<SliceRefinerEffect>();
}

}
