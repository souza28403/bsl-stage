#include "plugin/PluginTemplates.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace bsl::plugin {

namespace {

using Json = nlohmann::json;

const std::set<std::string>& reservedWords()
{
    static const std::set<std::string> words = {

        "TIME", "TIMEDELTA", "DATE", "FRAMEINDEX", "PASSINDEX", "RENDERSIZE", "inputImage", "main",

        "band", "rgb2hsv", "hsv2rgb",

        "float", "int", "uint", "bool", "double", "void", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "bvec2", "bvec3",
        "bvec4", "mat2", "mat3", "mat4", "sampler2D", "const", "uniform", "in", "out", "inout", "if", "else", "for", "while",
        "do", "switch", "case", "default", "break", "continue", "return", "discard", "struct", "true", "false", "layout",
        "precision", "highp", "mediump", "lowp", "attribute", "varying", "centroid", "flat", "smooth", "input", "output",
        "sample", "buffer", "shared", "common", "active", "filter", "image2D", "texture", "half", "fixed", "long", "short",
        "unsigned", "volatile", "extern", "static", "typedef", "union", "enum", "template", "this", "namespace", "using",
        "goto", "inline", "noinline", "public", "packed", "resource", "subroutine", "cast", "asm", "class", "sizeof",

        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "pow", "exp", "log", "exp2",
        "log2", "sqrt", "inversesqrt", "abs", "sign", "floor", "ceil", "fract", "mod", "min", "max", "clamp", "mix", "step",
        "smoothstep", "length", "distance", "dot", "cross", "normalize", "reflect", "refract", "round", "trunc", "texture2D",
        "textureLod", "texelFetch", "transpose", "inverse", "determinant", "any", "all", "not", "lessThan", "greaterThan"};
    return words;
}

std::string foldAscii(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[++i]);
            char base = 0;
            if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) base = 'a';
            else if (d == 0x87 || d == 0xA7) base = 'c';
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) base = 'e';
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) base = 'i';
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) base = 'o';
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) base = 'u';
            if (base)
                out += base;
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool identStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool identChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string fnum(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos)
        s += ".0";
    return s;
}

const char* glslType(ParamKind k)
{
    switch (k) {
    case ParamKind::Float: return "float";
    case ParamKind::Color: return "vec4";
    case ParamKind::Bool:
    case ParamKind::Event: return "bool";
    case ParamKind::Point2D: return "vec2";
    case ParamKind::Long: return "int";
    case ParamKind::Image:
    case ParamKind::Audio: return "sampler2D";
    }
    return "float";
}

const char* kindWord(ParamKind k)
{
    switch (k) {
    case ParamKind::Float: return "número (float)";
    case ParamKind::Color: return "cor";
    case ParamKind::Bool: return "liga/desliga (bool)";
    case ParamKind::Point2D: return "ponto 2D";
    case ParamKind::Image: return "imagem";
    case ParamKind::Event: return "evento (botão)";
    case ParamKind::Audio: return "áudio (espectro)";
    case ParamKind::Long: return "lista de opções";
    }
    return "";
}

ParamSpec pFloat(const char* name, const char* label, double def, double lo, double hi)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Float;
    p.defV = def;
    p.minV = lo;
    p.maxV = hi;
    return p;
}

ParamSpec pColor(const char* name, const char* label, float r, float g, float b)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Color;
    p.defColor[0] = r;
    p.defColor[1] = g;
    p.defColor[2] = b;
    p.defColor[3] = 1.0f;
    return p;
}

ParamSpec pBool(const char* name, const char* label, bool def)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Bool;
    p.defBool = def;
    return p;
}

ParamSpec pPoint(const char* name, const char* label, float x, float y)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Point2D;
    p.defPoint[0] = x;
    p.defPoint[1] = y;
    return p;
}

ParamSpec pEvent(const char* name, const char* label)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Event;
    return p;
}

ParamSpec pAudio(const char* name, const char* label)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Audio;
    return p;
}

ParamSpec pLong(const char* name, const char* label, int def, std::vector<std::string> options)
{
    ParamSpec p;
    p.name = name;
    p.label = label;
    p.kind = ParamKind::Long;
    p.defV = def;
    p.options = std::move(options);
    return p;
}

const char* const kBodyBlankEffect = R"GLSL(
void main()
{
    vec2 uv = isf_FragNormCoord;
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);

    vec4 outColor = src;

    gl_FragColor = outColor;
}
)GLSL";

const char* const kBodyBlankGenerator = R"GLSL(
void main()
{
    vec2 uv = isf_FragNormCoord;

    vec3 color = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(TIME));

    gl_FragColor = vec4(color, 1.0);
}
)GLSL";

const char* const kBodyColorEffect = R"GLSL(
vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);
    vec3 c = src.rgb;

    c += brightness;
    c = (c - 0.5) * contrast + 0.5;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, saturation);
    vec3 hsv = rgb2hsv(clamp(c, 0.0, 1.0));
    hsv.x = fract(hsv.x + hueShift / 360.0);
    c = hsv2rgb(hsv);
    c = mix(c, c * tint.rgb, tintAmount);

    gl_FragColor = vec4(clamp(c, 0.0, 1.0), src.a);
}
)GLSL";

const char* const kBodyColorGenerator = R"GLSL(
void main()
{
    vec2 uv = isf_FragNormCoord;
    float a = radians(angle);

    float t = dot(uv - 0.5, vec2(cos(a), sin(a))) + 0.5;
    t += 0.15 * sin(TIME * speed * 3.0);
    vec3 color = mix(colorA.rgb, colorB.rgb, clamp(t, 0.0, 1.0));
    gl_FragColor = vec4(color, 1.0);
}
)GLSL";

const char* const kBodyUvEffect = R"GLSL(
void main()
{
    vec2 uv = isf_FragNormCoord;
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 c = center / vec2(1920.0, 1080.0);
    vec2 d = (uv - c) * vec2(aspect, 1.0);
    float r = length(d);
    float wave = sin(r * frequency * 6.2831853 - TIME * speed * 2.0);
    vec2 dir = r > 0.0001 ? d / r : vec2(0.0);
    vec2 offset = dir * wave * amount / vec2(aspect, 1.0);
    gl_FragColor = IMG_NORM_PIXEL(inputImage, uv + offset);
}
)GLSL";

const char* const kBodyFeedbackEffect = R"GLSL(

void main()
{
    vec2 uv = isf_FragNormCoord;
    if (PASSINDEX == 0) {
        vec4 src = IMG_THIS_NORM_PIXEL(inputImage);
        vec4 cur = vec4(src.rgb * src.a, src.a);
        float a = radians(rotate);
        float aspect = RENDERSIZE.x / RENDERSIZE.y;
        vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
        p = vec2(cos(a) * p.x - sin(a) * p.y, sin(a) * p.x + cos(a) * p.y) / zoom;
        p = p / vec2(aspect, 1.0) + 0.5;
        vec4 prev = IMG_NORM_PIXEL(trail, p) * decay;
        vec4 o = cur + prev * (1.0 - cur.a);
        if (reset)
            o = cur;
        gl_FragColor = o;
    } else {
        vec4 t = IMG_THIS_NORM_PIXEL(trail);
        float al = min(t.a, 1.0);
        gl_FragColor = al > 0.0 ? vec4(min(t.rgb / al, vec3(1.0)), al) : vec4(0.0);
    }
}
)GLSL";

const char* const kBodyPatternGenerator = R"GLSL(
void main()
{
    vec2 uv = isf_FragNormCoord;
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 p = vec2(uv.x * aspect, uv.y) * scale;
    float t = TIME * speed;
    float m = 0.0;
    if (patternType == 0) {
        m = step(0.5, fract(p.x + p.y * 0.5 - t * 0.25));
    } else if (patternType == 1) {
        vec2 q = floor(p + vec2(t * 0.25, 0.0));
        m = mod(q.x + q.y, 2.0);
    } else if (patternType == 2) {
        float d = length(fract(p) - 0.5);
        m = step(d, 0.25 + 0.15 * sin(t));
    } else {
        float d = length(fract(p + vec2(0.0, t * 0.25)) - 0.5);
        m = 1.0 - smoothstep(0.2, 0.3, d);
    }
    gl_FragColor = vec4(mix(colorB.rgb, colorA.rgb, m), 1.0);
}
)GLSL";

const char* const kBodyAudioEffect = R"GLSL(

float band(float x)
{
    return IMG_NORM_PIXEL(spectrum, vec2(x, 0.5)).r;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float bass = (band(0.03) + band(0.08) + band(0.14)) / 3.0 * gain;
    vec2 p = (uv - 0.5) / (1.0 + bass * pulse * 0.15) + 0.5;
    vec4 img = IMG_NORM_PIXEL(inputImage, p);
    vec3 c = img.rgb * (1.0 + bass * pulse);
    float a = img.a;
    if (showBars) {
        float v = band(uv.x) * gain * 0.35;
        float cell = fract(uv.x * 32.0);
        float bar = step(uv.y, v) * step(0.1, cell) * step(cell, 0.9);
        c = mix(c, barColor.rgb, bar * 0.85);
        a = max(a, bar * 0.85);
    }
    gl_FragColor = vec4(c, a);
}
)GLSL";

const char* const kBodyAudioGenerator = R"GLSL(
float band(float x)
{
    return IMG_NORM_PIXEL(spectrum, vec2(x, 0.5)).r;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float v = band((floor(uv.x * 32.0) + 0.5) / 32.0) * gain;
    v = max(v, 0.04 + 0.03 * sin(TIME * 2.0 + uv.x * 12.0));
    float cell = fract(uv.x * 32.0);
    float bar = (1.0 - smoothstep(v - 0.01, v + 0.01, uv.y)) * step(0.1, cell) * step(cell, 0.9);
    vec3 bg = vec3(0.05, 0.05, 0.12) + 0.06 * uv.y;
    vec3 col = mix(colorLow.rgb, colorHigh.rgb, uv.y);
    gl_FragColor = vec4(mix(bg, col, bar), 1.0);
}
)GLSL";

const char* bodyFor(Template t, Kind kind)
{
    const bool fx = kind == Kind::Effect;
    switch (t) {
    case Template::Blank: return fx ? kBodyBlankEffect : kBodyBlankGenerator;
    case Template::ColorAdjust: return fx ? kBodyColorEffect : kBodyColorGenerator;
    case Template::UvDistort: return kBodyUvEffect;
    case Template::Feedback: return kBodyFeedbackEffect;
    case Template::Pattern: return kBodyPatternGenerator;
    case Template::AudioReactive: return fx ? kBodyAudioEffect : kBodyAudioGenerator;
    }
    return kBodyBlankEffect;
}

std::string inline_object(const std::vector<std::pair<std::string, Json>>& fields)
{
    std::string s = "{ ";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i)
            s += ", ";
        s += Json(fields[i].first).dump() + ": " + fields[i].second.dump();
    }
    return s + " }";
}

Json numberJson(double v)
{
    if (v == static_cast<double>(static_cast<long long>(v)) && v > -1e9 && v < 1e9)
        return Json(static_cast<long long>(v));
    return Json(v);
}

std::string inputJson(const ParamSpec& p)
{
    std::vector<std::pair<std::string, Json>> f;
    f.push_back({"NAME", p.name});
    if (!p.label.empty() && p.label != p.name)
        f.push_back({"LABEL", p.label});
    switch (p.kind) {
    case ParamKind::Float:
        f.push_back({"TYPE", "float"});
        f.push_back({"DEFAULT", numberJson(p.defV)});
        f.push_back({"MIN", numberJson(p.minV)});
        f.push_back({"MAX", numberJson(p.maxV)});
        break;
    case ParamKind::Color:
        f.push_back({"TYPE", "color"});
        f.push_back({"DEFAULT", Json::array({numberJson(p.defColor[0]), numberJson(p.defColor[1]), numberJson(p.defColor[2]),
                                             numberJson(p.defColor[3])})});
        break;
    case ParamKind::Bool:
        f.push_back({"TYPE", "bool"});
        f.push_back({"DEFAULT", p.defBool});
        break;
    case ParamKind::Point2D:
        f.push_back({"TYPE", "point2D"});
        f.push_back({"DEFAULT", Json::array({numberJson(p.defPoint[0]), numberJson(p.defPoint[1])})});
        f.push_back({"MIN", Json::array({0, 0})});
        f.push_back({"MAX", Json::array({1920, 1080})});
        break;
    case ParamKind::Event:
        f.push_back({"TYPE", "event"});
        break;
    case ParamKind::Audio:
        f.push_back({"TYPE", "audioFFT"});
        break;
    case ParamKind::Long: {
        f.push_back({"TYPE", "long"});
        f.push_back({"DEFAULT", numberJson(p.defV)});
        Json values = Json::array();
        Json labels = Json::array();
        for (size_t i = 0; i < p.options.size(); ++i) {
            values.push_back(static_cast<long long>(i));
            labels.push_back(p.options[i]);
        }
        f.push_back({"VALUES", values});
        f.push_back({"LABELS", labels});
        break;
    }
    case ParamKind::Image:
        break;
    }
    return inline_object(f);
}

std::string fallbackLine(const ParamSpec& p)
{
    std::string rhs;
    switch (p.kind) {
    case ParamKind::Float: rhs = fnum(p.defV); break;
    case ParamKind::Color:
        rhs = "vec4(" + fnum(p.defColor[0]) + ", " + fnum(p.defColor[1]) + ", " + fnum(p.defColor[2]) + ", " + fnum(p.defColor[3]) + ")";
        break;
    case ParamKind::Bool: rhs = p.defBool ? "true" : "false"; break;
    case ParamKind::Event: rhs = "false"; break;
    case ParamKind::Point2D: rhs = "vec2(" + fnum(p.defPoint[0]) + ", " + fnum(p.defPoint[1]) + ")"; break;
    case ParamKind::Long: rhs = std::to_string(static_cast<int>(p.defV)); break;
    default: return {};
    }
    return std::string("const ") + glslType(p.kind) + " " + p.name + " = " + rhs + ";\n";
}

const ParamSpec* findParam(const std::vector<ParamSpec>& v, const std::string& name)
{
    for (const ParamSpec& p : v)
        if (p.name == name)
            return &p;
    return nullptr;
}

}

const char* templateLabel(Template t, Kind kind)
{
    switch (t) {
    case Template::Blank: return "Em branco";
    case Template::ColorAdjust: return kind == Kind::Effect ? "Cor / ajuste" : "Cor / degradê";
    case Template::UvDistort: return "Distorção (UV)";
    case Template::Feedback: return "Feedback (rastro)";
    case Template::Pattern: return "Gerador de padrão";
    case Template::AudioReactive: return "Reativo a áudio";
    }
    return "";
}

const char* templateHint(Template t, Kind kind)
{
    switch (t) {
    case Template::Blank: return kind == Kind::Effect ? "Só repete a imagem. Você escreve o resto." : "Um degradê animado para você trocar.";
    case Template::ColorAdjust:
        return kind == Kind::Effect ? "Brilho, contraste, saturação, matiz e tingimento." : "Degradê entre duas cores, com ângulo e movimento.";
    case Template::UvDistort: return "Ondas que deformam a imagem a partir de um ponto.";
    case Template::Feedback: return "Rastro que fica na tela, gira e cresce (usa passes persistentes).";
    case Template::Pattern: return "Listras, xadrez, círculos ou pontos em duas cores.";
    case Template::AudioReactive:
        return kind == Kind::Effect ? "A imagem bate no ritmo dos graves e mostra barras do espectro." : "Barras que dançam com o som do PC ou do microfone.";
    }
    return "";
}

bool templateAvailable(Template t, Kind kind)
{
    switch (t) {
    case Template::Blank:
    case Template::ColorAdjust:
    case Template::AudioReactive: return true;
    case Template::UvDistort:
    case Template::Feedback: return kind == Kind::Effect;
    case Template::Pattern: return kind == Kind::Generator;
    }
    return false;
}

std::vector<ParamSpec> defaultParams(Template t, Kind kind)
{
    std::vector<ParamSpec> v;
    switch (t) {
    case Template::Blank:
        break;
    case Template::ColorAdjust:
        if (kind == Kind::Effect) {
            v.push_back(pFloat("brightness", "Brilho", 0.0, -1.0, 1.0));
            v.push_back(pFloat("contrast", "Contraste", 1.0, 0.0, 2.0));
            v.push_back(pFloat("saturation", "Saturação", 1.0, 0.0, 2.0));
            v.push_back(pFloat("hueShift", "Matiz (graus)", 0.0, -180.0, 180.0));
            v.push_back(pColor("tint", "Cor do tingimento", 1.0f, 0.6f, 0.2f));
            v.push_back(pFloat("tintAmount", "Quantidade do tingimento", 0.0, 0.0, 1.0));
        } else {
            v.push_back(pColor("colorA", "Cor A", 0.1f, 0.4f, 1.0f));
            v.push_back(pColor("colorB", "Cor B", 1.0f, 0.2f, 0.5f));
            v.push_back(pFloat("angle", "Ângulo (graus)", 0.0, -180.0, 180.0));
            v.push_back(pFloat("speed", "Velocidade", 0.3, -2.0, 2.0));
        }
        break;
    case Template::UvDistort:
        v.push_back(pFloat("amount", "Intensidade", 0.03, 0.0, 0.2));
        v.push_back(pFloat("frequency", "Frequência", 6.0, 0.5, 40.0));
        v.push_back(pFloat("speed", "Velocidade", 1.0, -5.0, 5.0));
        v.push_back(pPoint("center", "Centro (px em 1920x1080)", 960.0f, 540.0f));
        break;
    case Template::Feedback:
        v.push_back(pFloat("decay", "Persistência", 0.92, 0.0, 0.995));
        v.push_back(pFloat("zoom", "Zoom do rastro", 1.01, 0.9, 1.1));
        v.push_back(pFloat("rotate", "Rotação do rastro (graus)", 0.5, -10.0, 10.0));
        v.push_back(pEvent("reset", "Limpar rastro"));
        break;
    case Template::Pattern:
        v.push_back(pLong("patternType", "Padrão", 0, {"Listras", "Xadrez", "Círculos", "Pontos"}));
        v.push_back(pFloat("scale", "Repetições", 8.0, 1.0, 40.0));
        v.push_back(pFloat("speed", "Velocidade", 1.0, -5.0, 5.0));
        v.push_back(pColor("colorA", "Cor A", 1.0f, 1.0f, 1.0f));
        v.push_back(pColor("colorB", "Cor B", 0.05f, 0.05f, 0.2f));
        break;
    case Template::AudioReactive:
        v.push_back(pAudio("spectrum", "Espectro do áudio"));
        v.push_back(pFloat("gain", "Ganho do áudio", 1.0, 0.0, 4.0));
        if (kind == Kind::Effect) {
            v.push_back(pFloat("pulse", "Intensidade do pulso", 0.5, 0.0, 1.0));
            v.push_back(pColor("barColor", "Cor das barras", 1.0f, 0.3f, 0.2f));
            v.push_back(pBool("showBars", "Mostrar barras", true));
        } else {
            v.push_back(pColor("colorLow", "Cor embaixo", 0.1f, 0.6f, 1.0f));
            v.push_back(pColor("colorHigh", "Cor em cima", 1.0f, 0.2f, 0.6f));
        }
        break;
    }
    return v;
}

const std::vector<std::string>& suggestedCategories()
{
    static const std::vector<std::string> cats = {"Meus plugins", "Cor", "Distorção", "Geometria", "Desfoque", "Estilizar",
                                                  "Glitch", "Feedback", "Utilitários", "Geradores"};
    return cats;
}

bool isReservedIdent(const std::string& name)
{
    if (reservedWords().count(name))
        return true;
    return name.rfind("isf_", 0) == 0 || name.rfind("gl_", 0) == 0 || name.rfind("vv_", 0) == 0 || name.rfind("_", 0) == 0 ||
           name.find("__") != std::string::npos;
}

bool isValidIdent(const std::string& name)
{
    if (name.empty() || name.size() > 40 || !identStart(name[0]))
        return false;
    if (!std::all_of(name.begin(), name.end(), identChar))
        return false;
    return !isReservedIdent(name);
}

std::string identFromLabel(const std::string& label, const std::vector<std::string>& taken)
{
    const std::string ascii = foldAscii(label);
    std::string out;
    bool upperNext = false;
    for (char c : ascii) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            if (out.empty()) {
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            } else {
                out += upperNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            }
            upperNext = false;
        } else {
            upperNext = !out.empty();
        }
    }
    while (!out.empty() && !identStart(out[0]))
        out.erase(out.begin());
    if (!out.empty())
        out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    if (out.size() > 32)
        out.resize(32);
    if (out.empty())
        out = "parametro";
    if (isReservedIdent(out))
        out += "Valor";
    std::string result = out;
    for (int n = 2; std::find(taken.begin(), taken.end(), result) != taken.end(); ++n)
        result = out + std::to_string(n);
    return result;
}

std::string validateSpec(const PluginSpec& spec)
{
    std::string trimmed = spec.name;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    if (trimmed.empty())
        return "Dê um nome ao plugin.";
    if (spec.name.size() > 80)
        return "O nome do plugin está comprido demais (máximo 80 letras).";
    if (!templateAvailable(spec.tmpl, spec.kind))
        return std::string("O modelo \"") + templateLabel(spec.tmpl, spec.kind) +
               (spec.kind == Kind::Effect ? "\" só existe para geradores." : "\" só existe para efeitos.");
    if (spec.params.size() > 32)
        return "Parâmetros demais (máximo 32).";

    std::set<std::string> seen;
    for (const ParamSpec& p : spec.params) {
        if (!isValidIdent(p.name))
            return "Nome de parâmetro inválido: \"" + p.name + "\". Use só letras sem acento, números e _, começando por letra, e não use palavras do GLSL.";
        if (!seen.insert(p.name).second)
            return "Dois parâmetros com o mesmo nome: \"" + p.name + "\".";
        if (p.kind == ParamKind::Float && !(p.maxV > p.minV))
            return "O máximo de \"" + p.name + "\" precisa ser maior que o mínimo.";
        if (p.kind == ParamKind::Long && p.options.size() < 2)
            return "A lista \"" + p.name + "\" precisa de pelo menos duas opções.";
    }

    for (const ParamSpec& need : defaultParams(spec.tmpl, spec.kind)) {
        const ParamSpec* have = findParam(spec.params, need.name);
        if (!have) {
            if (need.kind == ParamKind::Audio)
                return "O modelo \"" + std::string(templateLabel(spec.tmpl, spec.kind)) + "\" precisa do parâmetro de áudio \"" + need.name + "\".";
            continue;
        }
        if (have->kind != need.kind)
            return "No modelo \"" + std::string(templateLabel(spec.tmpl, spec.kind)) + "\", o parâmetro \"" + need.name + "\" precisa ser do tipo " +
                   kindWord(need.kind) + ".";
    }
    return {};
}

std::string generateIsf(const PluginSpec& spec, const std::vector<std::string>& imageFiles)
{
    const bool effect = spec.kind == Kind::Effect;

    std::string description = spec.description;
    if (description.empty())
        description = std::string(effect ? "Efeito" : "Gerador") + " criado no BSL Stage (modelo: " + templateLabel(spec.tmpl, spec.kind) + ").";
    const std::string credit = spec.author.empty() ? "Criado no BSL Stage" : spec.author;
    std::string category = spec.category.empty() ? "Meus plugins" : spec.category;

    std::vector<std::string> inputs;
    if (effect)
        inputs.push_back(inline_object({{"NAME", "inputImage"}, {"TYPE", "image"}}));
    for (const ParamSpec& p : spec.params)
        if (p.kind != ParamKind::Image)
            inputs.push_back(inputJson(p));

    std::vector<std::string> imported;
    size_t imageIndex = 0;
    for (const ParamSpec& p : spec.params) {
        if (p.kind != ParamKind::Image)
            continue;
        if (imageIndex < imageFiles.size())
            imported.push_back("        " + Json(p.name).dump() + ": " + inline_object({{"PATH", imageFiles[imageIndex]}}));
        ++imageIndex;
    }

    std::string s = "/*{\n";
    s += "    \"DESCRIPTION\": " + Json(description).dump() + ",\n";
    s += "    \"CREDIT\": " + Json(credit).dump() + ",\n";
    s += "    \"ISFVSN\": \"2.0\",\n";
    s += "    \"CATEGORIES\": [" + Json(category).dump() + "],\n";
    s += "    \"INPUTS\": [\n";
    for (size_t i = 0; i < inputs.size(); ++i)
        s += "        " + inputs[i] + (i + 1 < inputs.size() ? "," : "") + "\n";
    s += "    ]";
    if (spec.tmpl == Template::Feedback) {
        s += ",\n    \"PASSES\": [\n        { \"TARGET\": \"trail\", \"PERSISTENT\": true, \"FLOAT\": true },\n        { }\n    ]";
    }
    if (!imported.empty()) {
        s += ",\n    \"IMPORTED\": {\n";
        for (size_t i = 0; i < imported.size(); ++i)
            s += imported[i] + (i + 1 < imported.size() ? ",\n" : "\n");
        s += "    }";
    }
    s += "\n}*/\n\n";

    for (const ParamSpec& need : defaultParams(spec.tmpl, spec.kind))
        if (!findParam(spec.params, need.name))
            s += fallbackLine(need);

    std::string body = bodyFor(spec.tmpl, spec.kind);
    body.erase(std::remove(body.begin(), body.end(), '\r'), body.end());
    if (!body.empty() && body[0] == '\n')
        body.erase(0, 1);
    s += body;

    if (spec.tmpl == Template::Blank) {
        const ParamSpec* firstImage = nullptr;
        for (const ParamSpec& p : spec.params)
            if (p.kind == ParamKind::Image) {
                firstImage = &p;
                break;
            }
        if (firstImage && !imageFiles.empty()) {
            const std::string needle = effect ? "    gl_FragColor = outColor;" : "    gl_FragColor = vec4(color, 1.0);";
            const size_t pos = s.rfind(needle);
            if (pos != std::string::npos) {
                std::string add = "    vec4 overlay = IMG_NORM_PIXEL(" + firstImage->name + ", uv);\n";
                add += effect ? "    outColor = vec4(mix(outColor.rgb, overlay.rgb, overlay.a), max(outColor.a, overlay.a));\n"
                              : "    color = mix(color, overlay.rgb, overlay.a);\n";
                s.insert(pos, add + "\n");
            }
        }
    }
    return s;
}

}
