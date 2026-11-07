#include "app/Paths.h"
#include "fx/EffectChain.h"
#include "fx/EffectRegistry.h"
#include "fx/IsfLibrary.h"
#include "plugin/IsfCheck.h"
#include "plugin/PluginFiles.h"
#include "plugin/PluginPackage.h"
#include "plugin/PluginTemplates.h"
#include "render/FboPool.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stb_image_write.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
using namespace bsl::plugin;

namespace {

int g_failures = 0;
int g_checks = 0;

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

void report(bool ok, const std::string& what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FALHOU: %s\n", what.c_str());
    }
}

#define CHECK(cond) report((cond), std::string(#cond) + " (linha " + std::to_string(__LINE__) + ")")

fs::path g_tmp;

std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const fs::path& p, const std::string& s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void touch(const fs::path& p, int seconds)
{
    std::error_code ec;
    fs::last_write_time(p, fs::file_time_type::clock::now() + std::chrono::seconds(seconds), ec);
}

fs::path pngSmall(const fs::path& file)
{
    const unsigned char px[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    std::string png;
    stbi_write_png_to_func([](void* c, void* d, int n) { static_cast<std::string*>(c)->append(static_cast<const char*>(d), static_cast<size_t>(n)); },
                           &png, 2, 2, 4, px, 8);
    spit(file, png);
    return file;
}

CheckOptions quick()
{
    CheckOptions o;
    o.width = 160;
    o.height = 90;
    o.keepPixels = true;
    return o;
}

bool anyErrorLine(const CheckResult& r, int line)
{
    for (const CheckError& e : r.errors)
        if (e.line == line)
            return true;
    return false;
}

void testTemplates()
{
    section("modelos do assistente");
    const fs::path dir = g_tmp / "modelos";
    int built = 0;
    for (int k = 0; k < 2; ++k) {
        const Kind kind = k == 0 ? Kind::Effect : Kind::Generator;
        for (int t = 0; t < kTemplateCount; ++t) {
            const Template tmpl = static_cast<Template>(t);
            if (!templateAvailable(tmpl, kind))
                continue;
            PluginSpec spec;
            spec.name = std::string(templateLabel(tmpl, kind)) + (k == 0 ? " fx" : " gen");
            spec.kind = kind;
            spec.tmpl = tmpl;
            spec.params = defaultParams(tmpl, kind);
            const CreateResult c = createPlugin(spec, dir);
            report(c.ok, "cria " + spec.name + ": " + c.error);
            if (!c.ok)
                continue;
            CheckOptions o = quick();
            o.audio = tmpl == Template::AudioReactive;
            o.time = 0.7;
            const CheckResult r = checkIsfFile(c.file, o);
            report(r.ok(), spec.name + " compila e renderiza\n" + r.toText(c.file.filename().string()));
            report(!r.blank && r.stats.litFraction > 0.2, spec.name + " nao e preto (acesos " + std::to_string(r.stats.litFraction) + ")");
            report(r.kind == (kind == Kind::Effect ? "efeito" : "gerador"), spec.name + " tipo certo");
            report(r.params.size() == spec.params.size(), spec.name + " parametros no cabecalho");
            report(r.categories.size() == 1 && r.categories[0] == "Meus plugins", spec.name + " categoria");
            ++built;
        }
    }
    CHECK(built == 9);

    {
        PluginSpec spec;
        spec.name = "Branco";
        spec.tmpl = Template::Blank;
        const CreateResult c = createPlugin(spec, dir);
        CHECK(c.ok);
        CheckOptions o = quick();
        const CheckResult r = checkIsfFile(c.file, o);
        CHECK(r.ok() && r.kind == "efeito");

        if (r.rgba.size() >= 4 * 160 * 90) {
            const unsigned char* p = &r.rgba[(static_cast<size_t>(10) * 160 + 10) * 4];
            CHECK(p[0] > p[1] && p[0] > p[2]);
            const unsigned char* q = &r.rgba[(static_cast<size_t>(10) * 160 + 150) * 4];
            CHECK(q[1] > q[0]);
        }
    }

    {
        PluginSpec spec;
        spec.name = "X";
        spec.kind = Kind::Generator;
        spec.tmpl = Template::UvDistort;
        CHECK(!validateSpec(spec).empty());
        spec.kind = Kind::Effect;
        spec.tmpl = Template::Pattern;
        CHECK(!validateSpec(spec).empty());
    }

    {
        PluginSpec spec;
        spec.name = "Brilho";
        spec.tmpl = Template::ColorAdjust;
        spec.params = defaultParams(Template::ColorAdjust, Kind::Effect);
        const CreateResult c = createPlugin(spec, dir);
        CheckOptions a = quick();
        CheckOptions b = quick();
        b.sets.emplace_back("brightness", "0.4");
        const CheckResult ra = checkIsfFile(c.file, a);
        const CheckResult rb = checkIsfFile(c.file, b);
        CHECK(ra.ok() && rb.ok());
        CHECK(rb.stats.meanR + rb.stats.meanG + rb.stats.meanB > ra.stats.meanR + ra.stats.meanG + ra.stats.meanB + 60.0);
    }

    {
        PluginSpec spec;
        spec.name = "Padrao anima";
        spec.kind = Kind::Generator;
        spec.tmpl = Template::Pattern;
        spec.params = defaultParams(Template::Pattern, Kind::Generator);
        const CreateResult c = createPlugin(spec, dir);
        CheckOptions a = quick();
        CheckOptions b = quick();
        b.time = 1.3;
        const CheckResult ra = checkIsfFile(c.file, a);
        const CheckResult rb = checkIsfFile(c.file, b);
        CHECK(ra.ok() && rb.ok() && ra.rgba != rb.rgba);

        PluginSpec au;
        au.name = "Barras";
        au.kind = Kind::Generator;
        au.tmpl = Template::AudioReactive;
        au.params = defaultParams(Template::AudioReactive, Kind::Generator);
        const CreateResult ca = createPlugin(au, dir);
        CheckOptions silent = quick();
        CheckOptions loud = quick();
        loud.audio = true;
        const CheckResult rs = checkIsfFile(ca.file, silent);
        const CheckResult rl = checkIsfFile(ca.file, loud);
        CHECK(rs.ok() && rl.ok() && !rs.blank);
        CHECK(rl.stats.meanR + rl.stats.meanG + rl.stats.meanB > rs.stats.meanR + rs.stats.meanG + rs.stats.meanB + 8.0);
    }
}

void testWizardParams()
{
    section("parametros do assistente");
    const fs::path dir = g_tmp / "params";

    for (int k = 0; k < 2; ++k) {
        PluginSpec spec;
        spec.name = k == 0 ? "Todos fx" : "Todos gen";
        spec.kind = k == 0 ? Kind::Effect : Kind::Generator;
        spec.author = "Maria Silva";
        spec.description = "Teste com todos os tipos";
        ParamSpec f;
        f.name = "forca";
        f.label = "Força";
        f.kind = ParamKind::Float;
        f.minV = 0;
        f.maxV = 10;
        f.defV = 2.5;
        ParamSpec c;
        c.name = "cor";
        c.label = "Cor";
        c.kind = ParamKind::Color;
        ParamSpec b;
        b.name = "ligado";
        b.label = "Ligado";
        b.kind = ParamKind::Bool;
        b.defBool = true;
        ParamSpec p;
        p.name = "ponto";
        p.label = "Ponto";
        p.kind = ParamKind::Point2D;
        ParamSpec e;
        e.name = "disparo";
        e.label = "Disparo";
        e.kind = ParamKind::Event;
        ParamSpec a;
        a.name = "som";
        a.label = "Som";
        a.kind = ParamKind::Audio;
        ParamSpec im;
        im.name = "logo";
        im.label = "Logo";
        im.kind = ParamKind::Image;
        im.imageFile = paths::toUtf8(pngSmall(g_tmp / "fonte" / "logo.png"));
        spec.params = {f, c, b, p, e, a, im};
        const CreateResult res = createPlugin(spec, dir);
        report(res.ok, spec.name + ": " + res.error);
        if (!res.ok)
            continue;
        const std::string text = slurp(res.file);
        CHECK(text.find("\"CREDIT\": \"Maria Silva\"") != std::string::npos);
        CHECK(text.find("\"TYPE\": \"audioFFT\"") != std::string::npos);
        CHECK(text.find("\"IMPORTED\"") != std::string::npos);
        const CheckResult r = checkIsfFile(res.file, quick());
        report(r.ok(), spec.name + " compila\n" + r.toText("x.fs"));
        CHECK(r.params.size() == 6);
        CHECK(r.imported.size() == 1);

        CHECK(fs::exists(dir / (res.file.stem().string() + "_logo.png")));

        CHECK(r.stats.litFraction > 0.5);
    }

    {
        PluginSpec spec;
        spec.name = "Sem imagem";
        ParamSpec im;
        im.name = "foto";
        im.kind = ParamKind::Image;
        spec.params = {im};
        const CreateResult res = createPlugin(spec, dir);
        CHECK(res.ok && fs::exists(dir / "Sem imagem_foto.png"));
        CHECK(res.ok && checkIsfFile(res.file, quick()).ok());
    }

    CHECK(identFromLabel("Intensidade do brilho") == "intensidadeDoBrilho");
    CHECK(identFromLabel("Força") == "forca");
    CHECK(identFromLabel("Ângulo (graus)") == "anguloGraus");
    CHECK(identFromLabel("123 abc") == "abc");
    CHECK(identFromLabel("???") == "parametro");
    CHECK(isReservedIdent(identFromLabel("TIME")) == false);
    CHECK(identFromLabel("float") != "float" && isValidIdent(identFromLabel("float")));
    CHECK(identFromLabel("Cor", {"cor"}) == "cor2");
    CHECK(!isValidIdent("TIME") && !isValidIdent("2x") && !isValidIdent("a b") && !isValidIdent("isf_x") && !isValidIdent("a__b") &&
          !isValidIdent("mix") && !isValidIdent("") && isValidIdent("minhaCor_1"));

    {
        PluginSpec s;
        s.name = "  ";
        CHECK(!validateSpec(s).empty());
        s.name = "Ok";
        ParamSpec a;
        a.name = "x";
        s.params = {a, a};
        CHECK(validateSpec(s).find("mesmo nome") != std::string::npos);
        s.params = {a};
        s.params[0].name = "TIME";
        CHECK(!validateSpec(s).empty());
        s.params[0].name = "x";
        s.params[0].maxV = s.params[0].minV;
        CHECK(!validateSpec(s).empty());

        PluginSpec u;
        u.name = "U";
        u.tmpl = Template::UvDistort;
        u.params = defaultParams(u.tmpl, u.kind);
        u.params[0].kind = ParamKind::Bool;
        CHECK(validateSpec(u).find("amount") != std::string::npos);

        PluginSpec au;
        au.name = "A";
        au.tmpl = Template::AudioReactive;
        au.params = {};
        CHECK(validateSpec(au).find("spectrum") != std::string::npos);
        CHECK(!createPlugin(au, dir).ok);
    }

    for (Template t : {Template::ColorAdjust, Template::UvDistort, Template::Feedback}) {
        PluginSpec spec;
        spec.name = std::string("Sem params ") + std::to_string(static_cast<int>(t));
        spec.tmpl = t;
        spec.params.clear();
        const CreateResult res = createPlugin(spec, dir);
        report(res.ok, "cria sem parametros");
        if (res.ok) {
            const CheckResult r = checkIsfFile(res.file, quick());
            report(r.ok() && !r.blank, spec.name + " compila sem parametros\n" + r.toText("x.fs"));
        }
    }

    CHECK(safeFileName("a/b:c*d?") == "abcd");
    CHECK(safeFileName("CON") == "CON_");
    CHECK(safeFileName("  ponto. ") == "ponto");
    CHECK(safeFileName("") == "plugin");
    CHECK(safeFileName(std::string(100, 'x')).size() <= 60);
    {
        PluginSpec spec;
        spec.name = "Meu Efeito Ótimo: \"v2\"";
        const CreateResult a = createPlugin(spec, dir);
        const CreateResult b = createPlugin(spec, dir);
        CHECK(a.ok && b.ok && a.file != b.file);
        CHECK(b.ok && b.file.stem().string().find(" 2") != std::string::npos);
        const auto d = duplicatePlugin(a.file);
        CHECK(d.ok && fs::exists(d.file) && slurp(d.file) == slurp(a.file));
        const auto d2 = duplicatePlugin(d.file);
        CHECK(d2.ok && d2.file != d.file && d2.file.stem().string().find("(cópia) (cópia)") == std::string::npos);
        CHECK(!duplicatePlugin(dir / "nao-existe.fs").ok);
    }
}

struct Pixel
{
    int r = 0, g = 0, b = 0;
};

Pixel renderCenter(fx::EffectInstance& inst, GLuint input, int w, int h)
{
    fx::IEffect* e = inst.ensureEffect();
    Pixel px{-1, -1, -1};
    if (!e)
        return px;
    inst.syncParams();
    FboPool pool;
    fx::EffectContext ctx;
    ctx.pool = &pool;
    ctx.dt = 1.0 / 60.0;
    Fbo out(w, h);
    e->process(ctx, input, w, h, out);
    unsigned char buf[4] = {};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, out.id());
    glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return {buf[0], buf[1], buf[2]};
}

bool closeTo(int a, int b, int tol = 4)
{
    return std::abs(a - b) <= tol;
}

std::string effectSource(const char* body, double def = 0.25)
{
    return std::string("/*{ \"DESCRIPTION\": \"teste\", \"CATEGORIES\": [\"Teste\"], \"INPUTS\": [\n"
                       "  { \"NAME\": \"inputImage\", \"TYPE\": \"image\" },\n"
                       "  { \"NAME\": \"amount\", \"TYPE\": \"float\", \"DEFAULT\": ") +
           std::to_string(def) + ", \"MIN\": 0, \"MAX\": 1 }\n] }*/\n" + body;
}

void testHotReload()
{
    section("recarga automatica");
    const fs::path dir = g_tmp / "reload";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / "Cor.fs";

    fx::IsfLibrary& lib = fx::IsfLibrary::instance();
    lib.configure({dir});

    spit(file, effectSource("void main(){\n    gl_FragColor = vec4(amount, 0.0, 0.0, 1.0);\n}\n"));
    touch(file, 1);
    lib.rescan();
    lib.waitScan();
    const std::string id = "isf:Cor.fs";
    CHECK(lib.find(id).has_value() && lib.find(id)->doc);
    CHECK(lib.idForFile(file).value_or("") == id);
    CHECK(!lib.idForFile(g_tmp / "fora" / "x.fs").has_value());

    fx::EffectInstance inst(id);
    inst.setParam("amount", 0.8);
    const GLuint input = 0;
    Pixel p = renderCenter(inst, input, 32, 32);
    report(closeTo(p.r, 204) && p.g == 0, "v1: vermelho = 0.8 (obtido " + std::to_string(p.r) + "," + std::to_string(p.g) + ")");
    CHECK(inst.status().text.empty());

    {
        const auto d1 = lib.doc(id);
        lib.rescan();
        lib.waitScan();
        CHECK(d1 && lib.doc(id) == d1);
        CHECK(!lib.checkForChanges(true));
    }

    spit(file, effectSource("void main(){\n    gl_FragColor = vec4(0.0, amount, 0.0, 1.0);\n}\n"));
    touch(file, 2);
    CHECK(lib.checkForChanges(true));
    lib.waitScan();
    p = renderCenter(inst, input, 32, 32);
    report(p.r == 0 && closeTo(p.g, 204), "v2: recompilou e manteve o valor (g=" + std::to_string(p.g) + ")");
    CHECK(inst.params.find("amount") && inst.params.find("amount")->get<double>() == 0.8);
    CHECK(inst.status().text.empty());

    spit(file, effectSource("void main(){\n    vec4 c = vec4(0.0, amount, 0.0, 1.0);\n    gl_FragColor = c + naoExiste;\n}\n"));
    touch(file, 3);
    CHECK(lib.checkForChanges(true));
    lib.waitScan();
    p = renderCenter(inst, input, 32, 32);
    report(p.r == 0 && closeTo(p.g, 204), "v3: com erro, continua o efeito antigo (g=" + std::to_string(p.g) + ")");
    {
        const media::SourceStatus st = inst.status();

        report(st.text.find("Linha 7") != std::string::npos, "v3: o erro mostra a linha 7 (a do codigo errado): " + st.text);
        CHECK(st.level == media::SourceStatus::Level::Warning);
        CHECK(st.text.find("versão anterior continua") != std::string::npos);
    }

    {
        const CheckResult r = checkIsfFile(file, quick());
        CHECK(!r.ok() && !r.errors.empty() && anyErrorLine(r, 7));
        CHECK(r.errors[0].sourceLine.find("naoExiste") != std::string::npos);
    }

    p = renderCenter(inst, input, 32, 32);
    CHECK(closeTo(p.g, 204));

    spit(file, effectSource("void main(){\n    gl_FragColor = vec4(0.0, 0.0, amount, 1.0);\n}\n"));
    touch(file, 4);
    CHECK(lib.checkForChanges(true));
    lib.waitScan();
    p = renderCenter(inst, input, 32, 32);
    report(p.r == 0 && p.g == 0 && closeTo(p.b, 204), "v4: corrigido (b=" + std::to_string(p.b) + ")");
    CHECK(inst.status().text.empty());

    spit(file, "/*{ \"INPUTS\": [ { \"NAME\": \"inputImage\" \"TYPE\": \"image\" } ] }*/\nvoid main(){ gl_FragColor = vec4(1.0); }\n");
    touch(file, 5);
    CHECK(lib.checkForChanges(true));
    lib.waitScan();
    p = renderCenter(inst, input, 32, 32);
    CHECK(closeTo(p.b, 204));
    {
        const media::SourceStatus st = inst.status();
        report(st.text.find("JSON") != std::string::npos && st.text.find("linha 1") != std::string::npos, "v5: erro de JSON com linha: " + st.text);
    }

    spit(file, effectSource("void main(){\n    gl_FragColor = vec4(amount, amount, 0.0, 1.0);\n}\n"));
    touch(file, 6);
    CHECK(lib.checkForChanges(true));
    lib.waitScan();
    p = renderCenter(inst, input, 32, 32);
    report(closeTo(p.r, 204) && closeTo(p.g, 204) && p.b == 0, "v6: amarelo com o valor mantido");
    CHECK(inst.status().text.empty());

    {
        const fs::path novo = dir / "Novo.fs";
        spit(novo, effectSource("void main(){ gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n"));
        touch(novo, 7);
        CHECK(lib.checkForChanges(true));
        lib.waitScan();
        CHECK(lib.find("isf:Novo.fs").has_value());
        const auto types = fx::EffectRegistry::instance().types();
        CHECK(std::any_of(types.begin(), types.end(), [](const fx::EffectTypeInfo& t) { return t.id == "isf:Novo.fs" && !t.sourceFile.empty(); }));
        fs::remove(novo, ec);
        CHECK(lib.checkForChanges(true));
        lib.waitScan();
        bool listed = false;
        for (const auto& e : lib.entries())
            listed = listed || e.id == "isf:Novo.fs";
        CHECK(!listed);
    }

    {
        lib.waitScan();
        const std::uint64_t before = lib.version();
        lib.startWatcher(80);
        lib.startWatcher(80);
        spit(file, effectSource("void main(){\n    gl_FragColor = vec4(0.0, amount, amount, 1.0);\n}\n"));
        touch(file, 8);
        bool seen = false;
        for (int i = 0; i < 100 && !seen; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            seen = lib.version() != before;
        }
        lib.stopWatcher();
        lib.waitScan();
        CHECK(seen);
        p = renderCenter(inst, input, 32, 32);
        report(p.r == 0 && closeTo(p.g, 204) && closeTo(p.b, 204), "vigia: ciano depois de salvar");
    }

    {
        const fs::path gen = dir / "Gera.fs";
        spit(gen, "/*{ \"CATEGORIES\": [\"Teste\"], \"INPUTS\": [ { \"NAME\": \"v\", \"TYPE\": \"float\", \"DEFAULT\": 0.5, \"MIN\": 0, \"MAX\": 1 } ] }*/\nvoid main(){ gl_FragColor = vec4(v, 0.0, 0.0, 1.0); }\n");
        touch(gen, 9);
        lib.rescan();
        lib.waitScan();
        const auto info = fx::EffectRegistry::instance().find("isf:Gera.fs");
        CHECK(info && info->generator);
    }
}

std::vector<std::uint8_t> zipOf(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::vector<ZipItem> v;
    for (const auto& [name, data] : files)
        v.push_back({name, data});
    return zipStoreItems(v);
}

void writeBytes(const fs::path& file, const std::vector<std::uint8_t>& bytes)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

const char* kGoodManifest = R"({"name":"Teste","author":"Ana","version":"1.0.0","license":"MIT","description":"x"})";
const char* kGoodShader = "/*{ \"DESCRIPTION\": \"ok\", \"CATEGORIES\": [\"Teste\"] }*/\nvoid main(){ gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }\n";

size_t countEntries(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec))
        return 0;
    return static_cast<size_t>(std::distance(fs::directory_iterator(dir, ec), fs::directory_iterator()));
}

void testPackage()
{
    section(".bslplugin: ida e volta");
    const fs::path work = g_tmp / "pkg";
    std::error_code ec;
    fs::remove_all(work, ec);

    PluginSpec spec;
    spec.name = "Meu Pacote";
    spec.author = "Ana";
    ParamSpec im;
    im.name = "logo";
    im.kind = ParamKind::Image;
    im.imageFile = paths::toUtf8(pngSmall(work / "src" / "logo.png"));
    ParamSpec f;
    f.name = "forca";
    f.label = "Força";
    spec.params = {f, im};
    const CreateResult made = createPlugin(spec, work / "origem");
    CHECK(made.ok);
    if (!made.ok)
        return;

    Manifest m;
    m.name = "Meu Pacote";
    m.author = "Ana";
    m.version = "1.2.0";
    m.license = "CC BY 4.0";
    m.description = "Pacote de teste";
    const fs::path pkg = work / "meu.bslplugin";
    const PackageResult ex = exportPlugin(made.file, m, pkg);
    report(ex.ok, "exporta: " + ex.error);
    CHECK(fs::exists(pkg) && fs::file_size(pkg, ec) > 100);

    PackageInfo info;
    const PackageResult ins = inspectPackage(pkg, info);
    report(ins.ok, "confere: " + ins.error);
    CHECK(info.manifest.name == "Meu Pacote" && info.manifest.license == "CC BY 4.0" && info.manifest.author == "Ana" &&
          info.manifest.version == "1.2.0" && info.manifest.description == "Pacote de teste");
    CHECK(info.shaders.size() == 1 && info.shaders[0] == "Meu Pacote.fs");
    CHECK(info.files.size() == 3);
    CHECK(info.warnings.empty());

    const InstallResult inst = installPackage(pkg, work / "destino");
    report(inst.ok, "instala: " + inst.error);
    if (inst.ok) {
        CHECK(inst.shaders.size() == 1 && fs::exists(inst.shaders[0]));
        CHECK(slurp(inst.shaders[0]) == slurp(made.file));
        CHECK(slurp(inst.folder / (made.file.stem().string() + "_logo.png")) == slurp(work / "origem" / (made.file.stem().string() + "_logo.png")));
        CHECK(fs::exists(inst.folder / "manifest.json"));
        const CheckResult r = checkIsfFile(inst.shaders[0], quick());
        report(r.ok(), "o plugin instalado compila\n" + r.toText("x.fs"));

        const InstallResult again = installPackage(pkg, work / "destino");
        CHECK(again.ok && again.folder != inst.folder);
    }

    {
        Manifest bad = m;
        bad.license = "";
        CHECK(!exportPlugin(made.file, bad, work / "x.bslplugin").ok);
        bad = m;
        bad.name = " ";
        CHECK(!exportPlugin(made.file, bad, work / "x.bslplugin").ok);
        const fs::path broken = work / "origem" / "Quebrado.fs";
        spit(broken, "/*{ \"INPUTS\": [ }*/\nvoid main(){}\n");
        CHECK(!exportPlugin(broken, m, work / "y.bslplugin").ok);
        CHECK(!fs::exists(work / "x.bslplugin") && !fs::exists(work / "y.bslplugin"));

        const fs::path semImg = work / "origem" / "SemImg.fs";
        spit(semImg, "/*{ \"IMPORTED\": { \"t\": { \"PATH\": \"nao-existe.png\" } } }*/\nvoid main(){ gl_FragColor = vec4(1.0); }\n");
        CHECK(!exportPlugin(semImg, m, work / "z.bslplugin").ok);

        const fs::path fora = work / "origem" / "Fora.fs";
        spit(fora, "/*{ \"IMPORTED\": { \"t\": { \"PATH\": \"../src/logo.png\" } } }*/\nvoid main(){ gl_FragColor = vec4(1.0); }\n");
        CHECK(!exportPlugin(fora, m, work / "w.bslplugin").ok);
    }

    section(".bslplugin: pacotes maliciosos");
    const fs::path bad = work / "mal";
    const fs::path dest = work / "mal-destino";
    fs::create_directories(dest, ec);
    int n = 0;
    auto expectRefused = [&](const char* what, const std::vector<std::uint8_t>& bytes, const char* messagePart = nullptr) {
        const fs::path f = bad / ("p" + std::to_string(++n) + ".bslplugin");
        writeBytes(f, bytes);
        PackageInfo pi;
        const PackageResult r = inspectPackage(f, pi);
        report(!r.ok && !r.error.empty(), std::string("recusa (confere): ") + what + (r.ok ? " [ACEITOU]" : ""));
        if (messagePart)
            report(r.error.find(messagePart) != std::string::npos, std::string(what) + ": mensagem \"" + r.error + "\" tem \"" + messagePart + "\"");
        const InstallResult ir = installPackage(f, dest);
        report(!ir.ok, std::string("recusa (instala): ") + what);
    };
    const std::string good = kGoodShader;
    expectRefused("zip slip ..", zipOf({{"manifest.json", kGoodManifest}, {"../evil.fs", good}}), "inseguro");
    expectRefused("zip slip com pasta", zipOf({{"manifest.json", kGoodManifest}, {"a/../../evil.fs", good}}));
    expectRefused("zip slip com barra invertida", zipOf({{"manifest.json", kGoodManifest}, {"..\\evil.fs", good}}));
    expectRefused("caminho absoluto", zipOf({{"manifest.json", kGoodManifest}, {"/windows/evil.fs", good}}));
    expectRefused("letra de unidade", zipOf({{"manifest.json", kGoodManifest}, {"C:/evil.fs", good}}));
    expectRefused("fluxo alternativo NTFS", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs:evil.exe", good}}));
    expectRefused("executavel .exe", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"setup.exe", "MZ\x90\x00"}}), "não permitido");
    expectRefused("dll", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"plugin.dll", "MZ\x90\x00"}}), "não permitido");
    expectRefused("script .bat", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"x.bat", "del *.*"}}));
    expectRefused("script .ps1", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"x.ps1", "x"}}));
    expectRefused("atalho .lnk", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"x.lnk", "x"}}));
    expectRefused("dupla extensao", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"foto.png.exe", "MZ"}}));
    expectRefused("executavel disfarcado de PNG", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"foto.png", "MZ\x90\x00\x03"}}), "programa");
    expectRefused("zip dentro do zip", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"foto.png", "PK\x03\x04"}}));
    expectRefused("shader binario", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", std::string("/*{}*/\0void main(){}", 20)}}));
    expectRefused("json estranho", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"outro.json", "{}"}}));
    expectRefused("nome reservado CON", zipOf({{"manifest.json", kGoodManifest}, {"CON.fs", good}}), "reservado");
    expectRefused("ponto no fim", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs.", good}}));
    expectRefused("arquivo sem extensao", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"LEIAME", "x"}}));
    expectRefused("pastas demais", zipOf({{"manifest.json", kGoodManifest}, {"a/b/c/d/e/f/ok.fs", good}}), "pastas demais");
    expectRefused("nome comprido", zipOf({{"manifest.json", kGoodManifest}, {std::string(130, 'a') + ".fs", good}}));
    expectRefused("nomes repetidos (maiusculas)", zipOf({{"manifest.json", kGoodManifest}, {"A.fs", good}, {"a.FS", good}}), "repetido");
    expectRefused("sem manifest", zipOf({{"ok.fs", good}}), "manifest");
    expectRefused("manifest sem licenca", zipOf({{"manifest.json", R"({"name":"X"})"}, {"ok.fs", good}}), "licença");
    expectRefused("manifest sem nome", zipOf({{"manifest.json", R"({"license":"MIT"})"}, {"ok.fs", good}}));
    expectRefused("manifest nao e json", zipOf({{"manifest.json", "isso nao e json"}, {"ok.fs", good}}), "JSON");
    expectRefused("manifest com campo errado", zipOf({{"manifest.json", R"({"name":"X","license":5})"}, {"ok.fs", good}}));
    expectRefused("sem shader", zipOf({{"manifest.json", kGoodManifest}, {"leia.txt", "oi"}}), "shader");
    expectRefused("shader com JSON quebrado", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", "/*{ \"INPUTS\": [ }*/\nvoid main(){}\n"}}), "erro");
    expectRefused("shader sem cabecalho", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", "void main(){}\n"}}));
    {
        std::vector<std::pair<std::string, std::string>> many = {{"manifest.json", kGoodManifest}, {"ok.fs", good}};
        for (int i = 0; i < 70; ++i)
            many.push_back({"img" + std::to_string(i) + ".png", "x"});
        expectRefused("arquivos demais", zipOf(many), "demais");
    }
    {
        std::vector<std::pair<std::string, std::string>> shaders = {{"manifest.json", kGoodManifest}};
        for (int i = 0; i < 33; ++i)
            shaders.push_back({"s" + std::to_string(i) + ".fs", good});
        expectRefused("shaders demais", zipOf(shaders));
    }
    expectRefused("arquivo de 17 MB", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"grande.png", std::string(17u << 20, 'x')}}), "grande");
    expectRefused("tres de 15 MB (total 48 MB)", zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}, {"a.png", std::string(15u << 20, 'x')}, {"b.png", std::string(15u << 20, 'y')}}));
    expectRefused("nao e zip", std::vector<std::uint8_t>(5000, 0x41), "ZIP");
    expectRefused("vazio", std::vector<std::uint8_t>{});
    {
        std::vector<std::uint8_t> trunc = zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}});
        trunc.resize(trunc.size() / 2);
        expectRefused("zip cortado ao meio", trunc);
    }

    {
        auto patched = [&](std::uint32_t newSize, std::uint32_t newComp) {
            std::vector<std::uint8_t> z = zipOf({{"manifest.json", kGoodManifest}, {"ok.fs", good}});
            for (size_t i = 0; i + 46 < z.size(); ++i)
                if (z[i] == 'P' && z[i + 1] == 'K' && z[i + 2] == 1 && z[i + 3] == 2) {
                    for (int b = 0; b < 4; ++b) {
                        z[i + 24 + static_cast<size_t>(b)] = static_cast<std::uint8_t>(newSize >> (8 * b));
                        if (newComp)
                            z[i + 20 + static_cast<size_t>(b)] = static_cast<std::uint8_t>(newComp >> (8 * b));
                    }
                }
            return z;
        };
        expectRefused("tamanho declarado de 2 GB", patched(0x7FFFFFFFu, 0), "grande");
        expectRefused("taxa de compressao absurda", patched(5u << 20, 50), "compress");
    }

    CHECK(countEntries(dest) == 0);
    CHECK(!fs::exists(dest.parent_path() / "evil.fs") && !fs::exists(work / "evil.fs") && !fs::exists(bad / "evil.fs"));

    {
        const fs::path goodFile = bad / "bom.bslplugin";
        writeBytes(goodFile, zipOf({{"manifest.json", kGoodManifest},
                             {"ok.fs", "/*{ \"IMPORTED\": { \"t\": { \"PATH\": \"falta.png\" } } }*/\nvoid main(){ gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }\n"},
                             {"leia.md", "# oi"}}));
        PackageInfo pi;
        const PackageResult r = inspectPackage(goodFile, pi);
        report(r.ok, "pacote bom: " + r.error);
        CHECK(pi.warnings.size() == 1 && pi.warnings[0].find("falta.png") != std::string::npos);
        const InstallResult ir = installPackage(goodFile, dest);
        CHECK(ir.ok && countEntries(dest) == 1);
    }

    {
        std::string why;
        CHECK(checkEntryName("a/b.fs", &why));
        CHECK(checkEntryName("manifest.json", &why));
        CHECK(!checkEntryName("sub/manifest.json", &why));
        CHECK(checkEntryName("imagens/", &why));
        CHECK(!checkEntryName("", &why));
        CHECK(!checkEntryName("a\x01.fs", &why));
        CHECK(!checkEntryName("a*.fs", &why));
    }
}

std::vector<fs::path> filesWith(const fs::path& dir, const char* ext)
{
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return out;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ext)
            out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> shaderBlocks(const std::string& md)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while ((pos = md.find("```", pos)) != std::string::npos) {
        const size_t eol = md.find('\n', pos);
        if (eol == std::string::npos)
            break;
        const size_t end = md.find("```", eol);
        if (end == std::string::npos)
            break;
        std::string body = md.substr(eol + 1, end - eol - 1);
        if (body.compare(0, 3, "/*{") == 0 && body.find("void main") != std::string::npos)
            out.push_back(std::move(body));
        pos = end + 3;
    }
    return out;
}

fs::path sourceRoot()
{
#ifdef BSL_SOURCE_DIR
    return fs::path(BSL_SOURCE_DIR);
#else
    return fs::current_path();
#endif
}

void testDocsAndSkill()
{
    section("isfcheck: skill e documentacao");
    const fs::path root = sourceRoot();

    const auto templates = filesWith(root / "skills" / "bsl-stage-plugin" / "templates", ".fs");
    CHECK(templates.size() >= 6);
    for (const fs::path& f : templates) {
        CheckOptions o = quick();
        o.audio = true;
        o.time = 0.5;
        const CheckResult r = checkIsfFile(f, o);
        report(r.ok() && !r.blank, "modelo da skill " + f.filename().string() + "\n" + r.toText(f.filename().string()));
        report(r.warnings.empty() || std::none_of(r.warnings.begin(), r.warnings.end(), [](const std::string& w) { return w.find("Sem CATEGORIES") != std::string::npos || w.find("DESCRIPTION") != std::string::npos; }),
               "modelo da skill " + f.filename().string() + " tem categoria e descricao");
    }

    const auto examples = filesWith(root / "docs" / "plugins" / "exemplos", ".fs");
    CHECK(examples.size() >= 3);
    for (const fs::path& f : examples) {
        CheckOptions o = quick();
        o.audio = true;
        o.time = 0.5;
        const CheckResult r = checkIsfFile(f, o);
        report(r.ok() && !r.blank, "exemplo " + f.filename().string() + "\n" + r.toText(f.filename().string()));
    }

    for (const fs::path& f : filesWith(root / "assets" / "isf", ".fs")) {
        CheckOptions o = quick();
        o.time = 0.5;
        const CheckResult r = checkIsfFile(f, o);
        report(r.ok(), "assets/isf/" + f.filename().string() + "\n" + r.toText(f.filename().string()));
    }

    std::vector<fs::path> docs = {root / "AGENTS.md", root / "skills" / "bsl-stage-plugin" / "SKILL.md"};
    for (const fs::path& f : filesWith(root / "docs" / "plugins", ".md"))
        docs.push_back(f);
    int blocks = 0;
    for (const fs::path& md : docs) {
        const std::string text = slurp(md);
        report(!text.empty(), "documento existe: " + md.filename().string());
        for (const std::string& code : shaderBlocks(text)) {
            ++blocks;

            CheckOptions o = quick();
            o.audio = true;
            o.time = 0.5;
            const CheckResult r = checkIsfSource(code, "", "bloco", md.parent_path(), o);
            report(r.compiled && r.rendered && r.errors.empty(),
                   "bloco de codigo de " + md.filename().string() + "\n" + code.substr(0, 200) + "\n" + r.toText("bloco"));
        }
    }
    std::printf("  %d bloco(s) de shader completos nos documentos\n", blocks);
    CHECK(blocks >= 6);
}

int runCommand(const std::wstring& command, const fs::path& outFile)
{
    std::wstring cmd = L"cmd /c \"" + command + L" > \"" + outFile.wstring() + L"\" 2>&1\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 99;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

void testCommandLine()
{
    section("bslstage_isfcheck (linha de comando)");
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const fs::path exe = fs::path(std::wstring(buf, len)).parent_path() / "bslstage_isfcheck.exe";
    if (!fs::exists(exe)) {
        report(false, "bslstage_isfcheck.exe nao encontrado ao lado do teste");
        return;
    }
    const fs::path dir = g_tmp / "cli";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path out = dir / "saida.txt";

    const fs::path ok = dir / "bom.fs";
    spit(ok, std::string("/*{ \"DESCRIPTION\": \"d\", \"CATEGORIES\": [\"Teste\"], \"INPUTS\": [ { \"NAME\": \"cor\", \"TYPE\": \"color\", \"DEFAULT\": [1.0, 0.5, 0.0, 1.0] } ] }*/\n"
                         "void main(){ gl_FragColor = cor; }\n"));
    const fs::path png = dir / "prev.png";
    int code = runCommand(L"\"" + exe.wstring() + L"\" \"" + ok.wstring() + L"\" --png \"" + png.wstring() + L"\" --time 1.5 --size 320x180", out);
    std::string text = slurp(out);
    report(code == 0, "bom.fs sai com 0 (saiu " + std::to_string(code) + ")\n" + text);
    CHECK(text.find("Resultado: OK") != std::string::npos);
    CHECK(text.find("cor") != std::string::npos && text.find("color") != std::string::npos);
    CHECK(fs::exists(png) && fs::file_size(png, ec) > 50);
    {
        const std::string head = slurp(png).substr(0, 8);
        CHECK(head.size() == 8 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G');
    }

    const fs::path bad = dir / "ruim.fs";
    spit(bad, "/*{ \"CATEGORIES\": [\"Teste\"] }*/\nvoid main()\n{\n    gl_FragColor = vec4(semNome, 0.0, 0.0, 1.0);\n}\n");
    code = runCommand(L"\"" + exe.wstring() + L"\" \"" + bad.wstring() + L"\"", out);
    text = slurp(out);
    report(code == 1, "ruim.fs sai com 1 (saiu " + std::to_string(code) + ")");
    CHECK(text.find("ruim.fs:4:") != std::string::npos);
    CHECK(text.find("semNome") != std::string::npos);

    const fs::path black = dir / "preto.fs";
    spit(black, "/*{ \"CATEGORIES\": [\"Teste\"], \"DESCRIPTION\": \"x\" }*/\nvoid main(){ gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); }\n");
    code = runCommand(L"\"" + exe.wstring() + L"\" \"" + black.wstring() + L"\"", out);
    report(code == 3, "preto.fs sai com 3 (saiu " + std::to_string(code) + ")");
    code = runCommand(L"\"" + exe.wstring() + L"\" \"" + black.wstring() + L"\" --allow-blank", out);
    CHECK(code == 0);

    code = runCommand(L"\"" + exe.wstring() + L"\" \"" + (dir / "nao-existe.fs").wstring() + L"\"", out);
    CHECK(code == 1);
    code = runCommand(L"\"" + exe.wstring() + L"\"", out);
    CHECK(code == 2);

    code = runCommand(L"\"" + exe.wstring() + L"\" \"" + ok.wstring() + L"\" --set cor=0,0,1,1 --json", out);
    text = slurp(out);
    CHECK(code == 0 && text.find("\"ok\": true") != std::string::npos && text.find("\"kind\": \"gerador\"") != std::string::npos);
}

}

int writeTemplates(const fs::path& dir)
{
    struct Entry
    {
        const char* file;
        Template tmpl;
        Kind kind;
        const char* description;
    };
    const Entry entries[] = {
        {"efeito-em-branco", Template::Blank, Kind::Effect, "Ponto de partida de um efeito: repete a imagem de entrada. Troque o corpo de main()."},
        {"efeito-cor-ajuste", Template::ColorAdjust, Kind::Effect, "Brilho, contraste, saturação, matiz e tingimento."},
        {"efeito-distorcao-uv", Template::UvDistort, Kind::Effect, "Ondas que deformam a imagem a partir de um ponto."},
        {"efeito-feedback", Template::Feedback, Kind::Effect, "Rastro que fica na tela, gira e cresce (passes persistentes)."},
        {"efeito-audio", Template::AudioReactive, Kind::Effect, "A imagem bate no ritmo dos graves e mostra barras do espectro."},
        {"gerador-em-branco", Template::Blank, Kind::Generator, "Ponto de partida de um gerador: degradê animado. Troque o corpo de main()."},
        {"gerador-cor-degrade", Template::ColorAdjust, Kind::Generator, "Degradê entre duas cores, com ângulo e movimento."},
        {"gerador-padrao", Template::Pattern, Kind::Generator, "Listras, xadrez, círculos ou pontos em duas cores."},
        {"gerador-audio", Template::AudioReactive, Kind::Generator, "Barras que dançam com o som do PC ou do microfone."},
    };
    std::error_code ec;
    fs::create_directories(dir, ec);
    int failed = 0;
    for (const Entry& e : entries) {
        std::error_code rm;
        fs::remove(dir / (std::string(e.file) + ".fs"), rm);
        PluginSpec spec;
        spec.name = e.file;
        spec.kind = e.kind;
        spec.tmpl = e.tmpl;
        spec.description = e.description;
        spec.author = "BSL Stage (modelo livre, CC0)";
        spec.params = defaultParams(e.tmpl, e.kind);
        const CreateResult r = createPlugin(spec, dir);
        std::printf("%s %s\n", r.ok ? "ok    " : "FALHOU", e.file);
        if (!r.ok) {
            std::printf("  %s\n", r.error.c_str());
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--write-templates")
        return writeTemplates(fs::path(std::u8string(reinterpret_cast<const char8_t*>(argv[2]))));
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "plugintest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    std::error_code ec;
    g_tmp = fs::temp_directory_path(ec) / "bslstage_plugintest";
    fs::remove_all(g_tmp, ec);
    fs::create_directories(g_tmp, ec);

    testTemplates();
    testWizardParams();
    testHotReload();
    testPackage();
    testDocsAndSkill();
    testCommandLine();

    {
        GLenum err;
        int glErrors = 0;
        while ((err = glGetError()) != GL_NO_ERROR) {
            std::printf("  erro de GL pendente: 0x%04X\n", err);
            ++glErrors;
        }
        report(glErrors == 0, "nenhum erro de GL");
    }

    fx::IsfLibrary::instance().stopWatcher();
    glfwDestroyWindow(win);
    glfwTerminate();
    fs::remove_all(g_tmp, ec);
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "PLUGINTEST OK\n" : "PLUGINTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
