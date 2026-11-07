#include "plugin/IsfCheck.h"

#include "app/Paths.h"
#include "audio/AudioTypes.h"
#include "fx/IEffect.h"
#include "fx/IsfDoc.h"
#include "fx/IsfRenderer.h"
#include "plugin/PluginFiles.h"
#include "render/Fbo.h"
#include "render/TextureLoader.h"

#include <glad/gl.h>
#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

namespace bsl::plugin {

namespace fs = std::filesystem;

namespace {

std::vector<std::string> splitLines(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r')
                cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string lineText(const std::vector<std::string>& lines, int line)
{
    if (line < 1 || line > static_cast<int>(lines.size()))
        return {};
    std::string t = lines[static_cast<size_t>(line - 1)];
    if (t.size() > 160)
        t.resize(160);
    return t;
}

std::string num(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

const char* typeName(fx::IsfInputType t)
{
    switch (t) {
    case fx::IsfInputType::Float: return "float";
    case fx::IsfInputType::Bool: return "bool";
    case fx::IsfInputType::Long: return "long";
    case fx::IsfInputType::Color: return "color";
    case fx::IsfInputType::Point2D: return "point2D";
    case fx::IsfInputType::Event: return "event";
    case fx::IsfInputType::Image: return "image";
    case fx::IsfInputType::Audio: return "audio";
    case fx::IsfInputType::AudioFft: return "audioFFT";
    case fx::IsfInputType::Cube: return "cube";
    default: return "?";
    }
}

ParamInfo infoFor(const fx::IsfInput& in)
{
    ParamInfo p;
    p.name = in.name;
    p.label = in.label;
    p.type = typeName(in.type);
    switch (in.type) {
    case fx::IsfInputType::Float:
        p.def = num(in.defV);
        p.range = num(in.minV) + " a " + num(in.maxV);
        break;
    case fx::IsfInputType::Bool: p.def = in.defBool ? "true" : "false"; break;
    case fx::IsfInputType::Long: {
        p.def = num(in.defV);
        for (size_t i = 0; i < in.values.size(); ++i) {
            if (i)
                p.range += ", ";
            p.range += std::to_string(in.values[i]);
            if (i < in.labels.size())
                p.range += "=" + in.labels[i];
        }
        break;
    }
    case fx::IsfInputType::Color:
        p.def = num(in.defColor[0]) + "," + num(in.defColor[1]) + "," + num(in.defColor[2]) + "," + num(in.defColor[3]);
        break;
    case fx::IsfInputType::Point2D:
        p.def = num(in.defPoint[0]) + "," + num(in.defPoint[1]);
        break;
    default: break;
    }
    return p;
}

void applySets(const fx::IsfDoc& doc, const CheckOptions& opt, media::ParamValues& values, std::vector<std::string>& warnings)
{
    for (const auto& [key, text] : opt.sets) {
        const fx::IsfInput* in = nullptr;
        for (const fx::IsfInput& i : doc.inputs)
            if (i.name == key)
                in = &i;
        if (!in) {
            warnings.push_back("--set " + key + ": não existe esse parâmetro.");
            continue;
        }
        auto numbers = [&](size_t want, std::vector<double>& out) {
            std::stringstream ss(text);
            std::string tok;
            while (std::getline(ss, tok, ','))
                out.push_back(std::atof(tok.c_str()));
            return out.size() >= want;
        };
        switch (in->type) {
        case fx::IsfInputType::Float: values.set(key, std::atof(text.c_str())); break;
        case fx::IsfInputType::Bool: values.set(key, text == "1" || text == "true" || text == "sim"); break;
        case fx::IsfInputType::Long: values.set(key, text); break;
        case fx::IsfInputType::Color: {
            std::vector<double> v;
            if (!numbers(3, v)) {
                warnings.push_back("--set " + key + ": use r,g,b ou r,g,b,a (0 a 1).");
                break;
            }
            if (v.size() < 4)
                v.push_back(1.0);
            values.set(key, media::Json::array({v[0], v[1], v[2], v[3]}));
            break;
        }
        case fx::IsfInputType::Point2D: {
            std::vector<double> v;
            if (!numbers(2, v)) {
                warnings.push_back("--set " + key + ": use x,y.");
                break;
            }
            values.set(key, media::Json::array({v[0], v[1]}));
            break;
        }
        case fx::IsfInputType::Event: values.set(key, 1); break;
        default: warnings.push_back("--set " + key + ": esse tipo de entrada não tem valor."); break;
        }
    }
}

GLuint makeTestCard(int w, int h)
{
    std::vector<unsigned char> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool left = x < w / 2, top = y < h / 2;
            float r = top ? (left ? 0.95f : 0.15f) : (left ? 0.15f : 0.95f);
            float g = top ? (left ? 0.15f : 0.85f) : (left ? 0.35f : 0.95f);
            float b = top ? (left ? 0.20f : 0.30f) : (left ? 0.90f : 0.30f);
            const float shade = 0.65f + 0.35f * static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
            r *= shade;
            g *= shade;
            b *= shade;
            const int gx = std::max(1, w / 8), gy = std::max(1, h / 8);
            if (x % gx == 0 || y % gy == 0) {
                r = g = b = 1.0f;
            }
            unsigned char* p = &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
            p[0] = static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
            p[1] = static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
            p[2] = static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            p[3] = 255;
        }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint uploadImage(const RgbaImage& img)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

audio::AudioSnapshot syntheticAudio(double t)
{
    audio::AudioSnapshot a;
    a.active = true;
    a.sampleRate = 48000;
    for (int i = 0; i < audio::kSpectrumBands; ++i) {
        const double base = 0.12 + 0.8 * std::exp(-static_cast<double>(i) / 11.0);
        const double wob = 0.65 + 0.35 * std::sin(t * 3.0 + i * 0.7);
        a.spectrum[static_cast<size_t>(i)] = static_cast<float>(std::clamp(base * wob, 0.0, 1.0));
    }
    a.bass = 0.9f;
    a.mid = 0.5f;
    a.high = 0.3f;
    a.rms = 0.4f;
    a.peak = 0.7f;
    a.kick = 0.8f;
    return a;
}

bool hasNonAsciiOutsideComments(const std::string& s, size_t from)
{
    size_t i = from;
    while (i < s.size()) {
        if (s.compare(i, 2, "//") == 0) {
            while (i < s.size() && s[i] != '\n')
                ++i;
        } else if (s.compare(i, 2, "/*") == 0) {
            const size_t e = s.find("*/", i + 2);
            i = e == std::string::npos ? s.size() : e + 2;
        } else {
            if (static_cast<unsigned char>(s[i]) >= 0x80)
                return true;
            ++i;
        }
    }
    return false;
}

void lintSource(const std::string& fragment, const fx::IsfDoc& doc, std::vector<std::string>& warnings)
{
    const size_t headerEnd = fragment.find("*/");
    const size_t codeStart = headerEnd == std::string::npos ? 0 : headerEnd + 2;
    const std::string code = fragment.substr(codeStart);

    if (doc.categories.empty())
        warnings.push_back("Sem CATEGORIES no cabeçalho: o plugin aparece em \"" + std::string(doc.isEffect() ? "Outros" : "Geradores") + "\". Sugestão: \"CATEGORIES\": [\"Meus plugins\"].");
    if (doc.description.empty())
        warnings.push_back("Sem DESCRIPTION no cabeçalho: a dica do efeito fica vazia.");
    if (doc.isEffect() && code.find("inputImage") == std::string::npos && code.find("IMG_THIS") == std::string::npos)
        warnings.push_back("O efeito declara inputImage mas o código não lê a imagem de entrada (use IMG_NORM_PIXEL(inputImage, uv)).");
    if (!doc.isEffect() && code.find("inputImage") != std::string::npos)
        warnings.push_back("O código usa inputImage mas o cabeçalho não declara { \"NAME\": \"inputImage\", \"TYPE\": \"image\" }: sem isso o plugin é um gerador e a imagem fica vazia.");
    if (hasNonAsciiOutsideComments(fragment, codeStart))
        warnings.push_back("Há caracteres fora do ASCII (acentos, aspas curvas) no código GLSL, fora de comentários. O BSL Stage troca por espaço, mas outros programas recusam.");
    static const std::regex versionRe(R"((^|\n)\s*#\s*version)");
    if (std::regex_search(code, versionRe))
        warnings.push_back("Remova a linha #version: o BSL Stage a acrescenta sozinho (GLSL 3.30 core).");
    static const std::regex uniformRe(R"((^|\n)\s*uniform\s+\w+\s+(\w+))");
    std::smatch m;
    if (std::regex_search(code, m, uniformRe))
        warnings.push_back("O código declara \"uniform " + m[2].str() + "\": não declare uniforms, os INPUTS do cabeçalho já viram variáveis (a declaração duplicada dá erro).");
    static const std::regex whileRe(R"(\b(while|do)\b\s*[\({])");
    if (std::regex_search(code, whileRe))
        warnings.push_back("Laço while/do no GLSL: prefira \"for\" com limite fixo pequeno; laços dinâmicos custam caro em PC fraco.");
    if (code.find("gl_FragColor") == std::string::npos && code.find("isf_FragColor") == std::string::npos)
        warnings.push_back("O código nunca escreve em gl_FragColor: a saída ficará sem valor definido.");
}

}

CheckResult checkIsfSource(const std::string& fragmentIn, const std::string& vertex, const std::string& name,
                           const fs::path& baseDir, const CheckOptions& opt)
{
    CheckResult r;
    r.name = name;
    r.width = std::max(16, opt.width);
    r.height = std::max(16, opt.height);

    std::string fragment = fragmentIn;
    if (fragment.compare(0, 3, "\xEF\xBB\xBF") == 0)
        fragment.erase(0, 3);
    const std::vector<std::string> lines = splitLines(fragment);

    const fx::IsfParseResult parsed = fx::parseIsf(fragment, vertex, name);
    if (!parsed.ok) {
        CheckError e;
        e.message = parsed.error;
        static const std::regex lineRe(R"(linha (\d+))");
        std::smatch m;
        if (std::regex_search(parsed.error, m, lineRe))
            e.line = std::atoi(m[1].str().c_str());
        e.sourceLine = lineText(lines, e.line);
        r.errors.push_back(e);
        r.rawError = parsed.error;
        return r;
    }
    r.parsed = true;
    const fx::IsfDoc& doc = *parsed.doc;
    r.kind = doc.isEffect() ? "efeito" : "gerador";
    r.description = doc.description;
    r.credit = doc.credit;
    r.categories = doc.categories;
    r.passes = doc.passCount();
    for (const fx::IsfInput& in : doc.inputs)
        if (in.type != fx::IsfInputType::Image || in.name != "inputImage")
            r.params.push_back(infoFor(in));
    for (const fx::IsfImported& im : doc.imported)
        r.imported.push_back(im.name + " = " + im.path);
    r.warnings = doc.warnings;
    lintSource(fragment, doc, r.warnings);

    fx::IsfRenderer renderer(parsed.doc, baseDir);
    media::ParamValues values;
    const std::vector<media::ParamDesc> descs = fx::isfParamDescs(doc);
    applySets(doc, opt, values, r.warnings);
    renderer.setParams(media::ParamSet(descs, values));

    if (!renderer.compile()) {
        r.rawError = renderer.error();
        static const std::regex lineRe(R"(^Linha (\d+): (.*)$)");
        std::istringstream in(renderer.error());
        std::string ln;
        while (std::getline(in, ln)) {
            std::smatch m;
            if (std::regex_match(ln, m, lineRe)) {
                CheckError e;
                e.line = std::atoi(m[1].str().c_str());
                e.message = m[2].str();
                e.sourceLine = lineText(lines, e.line);
                r.errors.push_back(e);
            }
        }
        if (r.errors.empty()) {
            CheckError e;
            e.message = renderer.error();
            r.errors.push_back(e);
        }
        return r;
    }
    r.compiled = true;
    for (const std::string& w : renderer.warnings())
        if (std::find(r.warnings.begin(), r.warnings.end(), w) == r.warnings.end())
            r.warnings.push_back(w);

    GLuint inputTex = 0;
    int inW = r.width, inH = r.height;
    if (doc.isEffect()) {
        if (!opt.inputImage.empty()) {
            RgbaImage img = loadImageFile(opt.inputImage);
            if (img.empty()) {
                r.warnings.push_back("Não consegui ler a imagem de entrada; usando a cartela de teste.");
                inputTex = makeTestCard(inW, inH);
            } else {
                inW = img.width;
                inH = img.height;
                inputTex = uploadImage(img);
            }
        } else {
            inputTex = makeTestCard(inW, inH);
        }
    }

    Fbo out(r.width, r.height);
    fx::EffectContext ctx;
    ctx.topOrigin = true;
    ctx.canvasWidth = r.width;
    ctx.canvasHeight = r.height;
    const int fps = std::clamp(opt.fps, 1, 240);
    const double time = std::max(0.0, opt.time);
    const int frames = std::clamp(static_cast<int>(std::ceil(time * fps)) + 1, 1, 1800);
    const double dt = frames > 1 ? time / (frames - 1) : 0.0;
    bool drew = true;
    for (int i = 0; i < frames && drew; ++i) {
        ctx.dt = i == 0 ? 0.0 : dt;
        ctx.time = dt * i;
        ctx.frameIndex = static_cast<std::uint64_t>(i);
        ctx.beat = ctx.time * 2.0;
        ctx.beatPhase = static_cast<float>(ctx.beat - std::floor(ctx.beat));
        if (opt.audio)
            ctx.audio = syntheticAudio(ctx.time);
        drew = renderer.render(ctx, inputTex, inW, inH, r.width, r.height, out);
    }
    if (!drew) {
        r.errors.push_back({0, "O host não conseguiu desenhar o quadro: " + renderer.error(), {}});
        if (inputTex)
            glDeleteTextures(1, &inputTex);
        return r;
    }
    r.rendered = true;

    r.rgba.resize(static_cast<size_t>(r.width) * static_cast<size_t>(r.height) * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, out.id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, r.rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    double sum[4] = {0, 0, 0, 0};
    size_t lit = 0;
    int maxC = 0;
    const size_t pixels = static_cast<size_t>(r.width) * static_cast<size_t>(r.height);
    for (size_t i = 0; i < pixels; ++i) {
        const unsigned char* p = &r.rgba[i * 4];
        for (int c = 0; c < 4; ++c)
            sum[c] += p[c];
        const int mx = std::max({static_cast<int>(p[0]), static_cast<int>(p[1]), static_cast<int>(p[2])});
        maxC = std::max(maxC, mx);
        if (mx > 8 && p[3] > 8)
            ++lit;
    }
    r.stats.meanR = sum[0] / static_cast<double>(pixels);
    r.stats.meanG = sum[1] / static_cast<double>(pixels);
    r.stats.meanB = sum[2] / static_cast<double>(pixels);
    r.stats.meanA = sum[3] / static_cast<double>(pixels);
    r.stats.litFraction = static_cast<double>(lit) / static_cast<double>(pixels);
    r.stats.maxChannel = maxC;
    r.blank = r.stats.litFraction < 0.001;

    if (!opt.pngOut.empty()) {
        std::string png;
        stbi_write_png_to_func(
            [](void* c, void* data, int size) { static_cast<std::string*>(c)->append(static_cast<const char*>(data), static_cast<size_t>(size)); },
            &png, r.width, r.height, 4, r.rgba.data(), r.width * 4);
        std::error_code ec;
        fs::create_directories(opt.pngOut.parent_path(), ec);
        std::ofstream f(opt.pngOut, std::ios::binary | std::ios::trunc);
        f.write(png.data(), static_cast<std::streamsize>(png.size()));
        if (!f)
            r.warnings.push_back("Não consegui gravar o PNG em " + paths::toUtf8(opt.pngOut));
    }

    {
        ctx.dt = 0.0;
        double total = 0.0;
        int counted = 0;
        for (int i = 0; i < 8; ++i) {
            glFinish();
            const auto t0 = std::chrono::steady_clock::now();
            renderer.render(ctx, inputTex, inW, inH, r.width, r.height, out);
            glFinish();
            const auto t1 = std::chrono::steady_clock::now();
            if (i >= 3) {
                total += std::chrono::duration<double, std::milli>(t1 - t0).count();
                ++counted;
            }
        }
        r.stats.msPerFrame = counted ? total / counted : 0.0;
        r.stats.msAt1080p = r.stats.msPerFrame * (1920.0 * 1080.0) / (static_cast<double>(r.width) * r.height);
    }
    if (inputTex)
        glDeleteTextures(1, &inputTex);

    if (r.blank)
        r.warnings.push_back(doc.isEffect()
                                 ? "A saída ficou toda preta ou transparente com a cartela de teste na entrada. Confira se o código usa a imagem de entrada e se o alfa não é 0."
                                 : "A saída ficou toda preta ou transparente. Confira se o gerador escreve cores acima de 0 e alfa 1.");
    if (r.stats.msAt1080p > 10.0)
        r.warnings.push_back("Custo alto: ~" + num(r.stats.msAt1080p) + " ms por quadro em 1920x1080 nesta GPU. Em PC fraco (Intel HD 4000) pode não manter 60 fps; veja docs/plugins/boas-praticas.md.");
    return r;
}

CheckResult checkIsfFile(const fs::path& file, const CheckOptions& opt)
{
    std::string text;
    if (!readTextFile(file, text, 1u << 20)) {
        CheckResult r;
        r.name = paths::toUtf8(file.stem());
        r.errors.push_back({0, "Não foi possível ler o arquivo (não existe ou passa de 1 MB): " + paths::toUtf8(file), {}});
        return r;
    }
    std::string vertex;
    fs::path vs = file;
    vs.replace_extension(".vs");
    std::error_code ec;
    if (fs::is_regular_file(vs, ec))
        readTextFile(vs, vertex, 1u << 20);
    return checkIsfSource(text, vertex, paths::toUtf8(file.stem()), file.parent_path(), opt);
}

std::string CheckResult::toText(const std::string& fileLabel) const
{
    std::string s;
    s += "Arquivo: " + fileLabel + "\n";
    s += std::string("Resultado: ") + (ok() ? (blank ? "COMPILA, mas a saída está vazia (veja os avisos)" : "OK (compilou e desenhou)") : "ERRO") + "\n";
    if (parsed) {
        s += "Tipo: " + kind + "   Passes: " + std::to_string(passes);
        s += "   Categorias:";
        if (categories.empty())
            s += " (nenhuma)";
        for (const std::string& c : categories)
            s += " " + c;
        s += "\n";
        if (!description.empty())
            s += "Descrição: " + description + "\n";
        s += "Parâmetros (" + std::to_string(params.size()) + "):\n";
        for (const ParamInfo& p : params) {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "  %-18s %-9s padrão %-18s %s%s%s\n", p.name.c_str(), p.type.c_str(), p.def.c_str(),
                          p.range.c_str(), p.label != p.name ? "   rótulo: " : "", p.label != p.name ? p.label.c_str() : "");
            s += buf;
        }
        for (const std::string& im : imported)
            s += "Imagem importada: " + im + "\n";
    }
    if (!errors.empty()) {
        s += "ERROS:\n";
        for (const CheckError& e : errors) {
            if (e.line > 0)
                s += "  " + fileLabel + ":" + std::to_string(e.line) + ": " + e.message + "\n";
            else
                s += "  " + fileLabel + ": " + e.message + "\n";
            if (!e.sourceLine.empty())
                s += "      " + std::to_string(e.line) + " | " + e.sourceLine + "\n";
        }
    }
    if (!warnings.empty()) {
        s += "Avisos:\n";
        for (const std::string& w : warnings)
            s += "  - " + w + "\n";
    }
    if (rendered) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "Quadro %dx%d: média RGBA (%.0f, %.0f, %.0f, %.0f) de 255; %.1f%% dos pixels acesos; canal máximo %d\n", width,
                      height, stats.meanR, stats.meanG, stats.meanB, stats.meanA, stats.litFraction * 100.0, stats.maxChannel);
        s += buf;
        std::snprintf(buf, sizeof(buf), "Custo: %.2f ms por quadro neste tamanho (~%.1f ms estimados em 1920x1080)\n", stats.msPerFrame,
                      stats.msAt1080p);
        s += buf;
    }
    return s;
}

std::string CheckResult::toJson(const std::string& fileLabel) const
{
    using nlohmann::json;
    json j;
    j["file"] = fileLabel;
    j["ok"] = ok();
    j["parsed"] = parsed;
    j["compiled"] = compiled;
    j["rendered"] = rendered;
    j["blank"] = blank;
    j["name"] = name;
    j["kind"] = kind;
    j["description"] = description;
    j["categories"] = categories;
    j["passes"] = passes;
    json ps = json::array();
    for (const ParamInfo& p : params)
        ps.push_back({{"name", p.name}, {"label", p.label}, {"type", p.type}, {"default", p.def}, {"range", p.range}});
    j["params"] = ps;
    j["imported"] = imported;
    json es = json::array();
    for (const CheckError& e : errors)
        es.push_back({{"line", e.line}, {"message", e.message}, {"source", e.sourceLine}});
    j["errors"] = es;
    j["warnings"] = warnings;
    if (rendered) {
        j["stats"] = {{"width", width},
                      {"height", height},
                      {"mean", {stats.meanR, stats.meanG, stats.meanB, stats.meanA}},
                      {"litFraction", stats.litFraction},
                      {"maxChannel", stats.maxChannel},
                      {"msPerFrame", stats.msPerFrame},
                      {"msAt1080p", stats.msAt1080p}};
    }
    return j.dump(2);
}

}
