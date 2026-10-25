// bslstage_projecttest: valida o formato .bslstage (sem janela e sem GL).
// Uso: bslstage_projecttest [pasta-testmedia]
//
// Cobre: save -> load -> save identico, arquivo corrompido, versao futura, campos ausentes/extras,
// relPath depois de mover a pasta do projeto, arquivo ausente e "Localizar".

#include "core/Composition.h"
#include "media/DocInfo.h"
#include "io/ProjectFile.h"
#include "output/OutputSetup.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <windows.h>

namespace fs = std::filesystem;
using namespace bsl;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);                     \
        }                                                                                  \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeAll(const fs::path& p, const std::string& text)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

fs::path findTestMedia(int argc, char** argv)
{
    std::error_code ec;
    if (argc > 1 && fs::is_directory(argv[1], ec))
        return fs::absolute(argv[1], ec);
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path dir = fs::path(std::wstring(buf, n)).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        if (fs::is_directory(dir / "testmedia", ec))
            return dir / "testmedia";
        dir = dir.parent_path();
    }
    return {};
}

// Composicao do teste: 2 decks, 3 camadas, clips de arquivo e gerador
Composition buildComposition(const fs::path& media)
{
    Composition comp = Composition::createDefault(3, 8);
    comp.width = 1280;
    comp.height = 720;
    comp.fps = 30.0f;
    comp.bpm = 128.5f;
    comp.masterOpacity = 0.8f;
    comp.masterBypass = true;

    comp.layers[0].name = "Fundo";
    comp.layers[0].opacity = 0.65f;
    comp.layers[0].blendMode = BlendMode::Add;
    comp.layers[1].bypass = true;
    comp.layers[2].solo = true;
    comp.layers[2].blendMode = BlendMode::Difference;

    comp.decks[0].name = "Abertura";
    comp.setClip(0, 0, Clip::fromFile(media / "photo.jpg"));
    Clip video = Clip::fromFile(media / "h264_1080p.mp4");
    video.setSpeed(0.5f);
    video.setLoop(media::LoopMode::Once);
    video.fit = FitMode::Fill;
    comp.setClip(1, 2, std::move(video));
    Clip anim = Clip::fromFile(media / "anim.gif");
    anim.setLoop(media::LoopMode::PingPong);
    anim.fit = FitMode::Stretch;
    comp.setClip(2, 7, std::move(anim));
    comp.setClip(0, 3, Clip::testPattern());

    const int d2 = comp.addDeck();
    comp.currentDeck = d2;
    comp.decks[static_cast<size_t>(d2)].name = "Segundo ato";
    comp.addColumn(); // 9 colunas
    comp.setClip(0, 8, Clip::fromFile(media / "alpha.png"));
    Clip hevc = Clip::fromFile(media / "hevc.mkv");
    hevc.setSpeed(2.0f);
    hevc.fit = FitMode::Original;
    comp.setClip(2, 0, std::move(hevc));
    comp.setClip(1, 1, Clip::testPattern());

    // Fase 3A: geradores com parametros (texto UTF-8 com acentos, cor, alvo da captura, dispositivo)
    Clip text = Clip::generator(kTextId);
    text.setParam("text", "Olá, ação\nSegunda linha");
    text.setParam("size", 72);
    text.setParam("color", media::param::colorJson({1.0f, 0.5f, 0.25f, 0.75f}));
    text.setParam("marquee", "vertical");
    text.setParam("marqueeSpeed", media::param::floatJson(123.5f));
    text.setParam("font", "Barlow-Bold.ttf");
    comp.setClip(2, 3, std::move(text));
    Clip screen = Clip::generator(kScreenCaptureId);
    screen.setParam("target", media::Json{{"kind", "window"}, {"exe", "chrome.exe"}, {"title", "Teste ção"}});
    screen.setParam("regionW", 640);
    screen.setParam("fps", "60");
    comp.setClip(2, 4, std::move(screen));
    Clip device = Clip::generator(kCaptureDeviceId);
    device.setParam("device", "USB Video Device");
    device.setParam("format", "1280x720@30:yuyv422");
    comp.setClip(2, 5, std::move(device));
    Clip solid = Clip::generator(kSolidColorId);
    solid.setParam("color", media::param::colorJson({0.0f, 0.25f, 1.0f, 1.0f}));
    comp.setClip(2, 6, std::move(solid));
    return comp;
}

// Saida do teste: 2 screens (monitor e Spout) com slices em warp
OutputSetup buildOutput(int cw, int ch)
{
    OutputSetup out;
    const int a = out.addScreen(cw, ch);
    Screen& s1 = out.screens[static_cast<size_t>(a)];
    s1.name = "Palco";
    s1.kind = OutputKind::Monitor;
    s1.monitorIndex = 1;
    s1.monitorName = "DELL U2415";
    s1.width = 1920;
    s1.height = 1080;
    s1.slices.clear();

    Slice left = makeFullSlice("Esquerda", cw, ch, s1.width, s1.height);
    left.input = {0, 0, cw / 2, ch};
    left.corners[kCornerTL] = {12.5f, 8.25f};
    left.corners[kCornerTR] = {950.1f, 0.0f};
    left.corners[kCornerBR] = {900.0f, 1080.0f};
    left.corners[kCornerBL] = {0.0f, 1040.75f};
    left.edges[kEdgeRight].width = 64.0f;
    left.edges[kEdgeRight].gamma = 1.8f;
    left.gain[0] = 0.9f;
    left.gain[1] = 1.05f;
    left.gain[2] = 0.97f;
    left.brightness = -0.1f;
    left.opacity = 0.9f;
    Slice right = makeFullSlice("Direita", cw, ch, s1.width, s1.height);
    right.input = {cw / 2, 0, cw - cw / 2, ch};
    right.corners[kCornerTL] = {1000.0f, 20.0f};
    right.corners[kCornerBR] = {1900.0f, 1060.0f};
    right.flipH = true;
    right.flipV = true;
    right.enabled = false;
    right.edges[kEdgeLeft].width = 64.0f;
    s1.slices.push_back(left);
    s1.slices.push_back(right);

    const int b = out.addScreen(cw, ch);
    Screen& s2 = out.screens[static_cast<size_t>(b)];
    s2.name = "Spout A";
    s2.kind = OutputKind::Spout;
    s2.spoutName = "BSL Stage - Spout A";
    s2.enabled = false;
    s2.slices[0].corners[kCornerBL] = {20.0f, 700.0f};
    return out;
}

int countClips(const Composition& c)
{
    int n = 0;
    for (const Deck& d : c.decks)
        n += d.clipCount();
    return n;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path media = findTestMedia(argc, argv);
    if (media.empty()) {
        std::printf("testmedia/ nao encontrada (passe a pasta como argumento)\n");
        return 2;
    }

    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / ("bslstage_projecttest_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root, ec);
    // Pasta com acentos: o caminho precisa sobreviver ao JSON em UTF-8
    const fs::path proj = root / u8"projeção";
    fs::create_directories(proj / "media", ec);
    for (const char* name : {"photo.jpg", "h264_1080p.mp4", "anim.gif", "alpha.png", "hevc.mkv"})
        fs::copy_file(media / name, proj / "media" / name, fs::copy_options::overwrite_existing, ec);

    const fs::path file = proj / "show.bslstage";
    const fs::path mediaDir = proj / "media";

    // ------------------------------------------------------------------ save -> load -> save
    section("save -> load -> save");
    Composition comp = buildComposition(mediaDir);
    OutputSetup out = buildOutput(comp.width, comp.height);
    CHECK(countClips(comp) == 11);

    const std::string text1 = project::serialize(comp, out, proj);
    std::string err;
    CHECK(project::saveFile(file, text1, &err));
    CHECK(readAll(file) == text1);
    CHECK(!fs::exists(fs::path(file).concat(".tmp"), ec)); // gravacao atomica nao deixa o temporario
    CHECK(text1.find("\"format\": \"bslstage\"") != std::string::npos);
    CHECK(text1.find("\"relPath\": \"media/photo.jpg\"") != std::string::npos);

    Composition comp2;
    OutputSetup out2;
    project::LoadResult r = project::loadFile(file, comp2, out2);
    CHECK(r.ok);
    CHECK(r.warnings.empty());
    CHECK(r.missingFiles == 0);
    const std::string text2 = project::serialize(comp2, out2, proj);
    CHECK(text1 == text2);
    if (text1 != text2) {
        writeAll(root / "a.json", text1);
        writeAll(root / "b.json", text2);
        std::printf("  (diferenca em %s e %s)\n", (root / "a.json").string().c_str(), (root / "b.json").string().c_str());
    }

    // Segundo save por cima do primeiro: arquivo identico
    CHECK(project::saveFile(file, text2, &err));
    CHECK(readAll(file) == text1);

    // Conteudo carregado
    CHECK(comp2.width == 1280 && comp2.height == 720);
    CHECK(comp2.fps == 30.0f && comp2.bpm == 128.5f);
    CHECK(comp2.masterOpacity == 0.8f && comp2.masterBypass);
    CHECK(comp2.layerCount() == 3 && comp2.deckCount() == 2 && comp2.currentDeck == 1);
    CHECK(comp2.layers[0].name == "Fundo" && comp2.layers[0].blendMode == BlendMode::Add);
    CHECK(comp2.layers[1].bypass && comp2.layers[2].solo);
    CHECK(comp2.layers[2].blendMode == BlendMode::Difference);
    CHECK(comp2.decks[0].name == "Abertura" && comp2.decks[0].columnCount() == 8);
    CHECK(comp2.decks[1].name == "Segundo ato" && comp2.decks[1].columnCount() == 9);
    CHECK(countClips(comp2) == 11);
    const Clip& v = comp2.clipAt(0, 1, 2);
    CHECK(v.type == Clip::Type::File && v.kind == media::MediaKind::Video && !v.missing);
    CHECK(v.speed == 0.5f && v.loop == media::LoopMode::Once && v.fit == FitMode::Fill);
    CHECK(comp2.clipAt(0, 2, 7).loop == media::LoopMode::PingPong && comp2.clipAt(0, 2, 7).fit == FitMode::Stretch);
    CHECK(comp2.clipAt(0, 0, 3).isTestPattern());
    CHECK(comp2.clipAt(0, 0, 3).params.empty());
    CHECK(comp2.clipAt(1, 2, 0).speed == 2.0f && comp2.clipAt(1, 2, 0).fit == FitMode::Original);
    // Geradores com parametros: valores, tipos e nomes sobrevivem
    {
        const Clip& t = comp2.clipAt(1, 2, 3);
        CHECK(t.isGenerator() && t.generatorId == kTextId && !t.isLive());
        CHECK(t.params.json().value("text", std::string()) == "Olá, ação\nSegunda linha");
        CHECK(t.params.json().value("size", 0) == 72);
        CHECK(t.params.json().value("marqueeSpeed", 0.0) == 123.5);
        CHECK(t.params.json().value("marquee", std::string()) == "vertical");
        const media::ParamSet ps(t.paramDescs(), t.params);
        CHECK(ps.i("size") == 72 && ps.f("marqueeSpeed") == 123.5f && ps.s("font") == "Barlow-Bold.ttf");
        CHECK(ps.c("color").r == 1.0f && ps.c("color").g == 0.5f && ps.c("color").a == 0.75f);
        CHECK(ps.b("wrap") && ps.f("lineSpacing") == 1.0f); // padrao preenchido
        CHECK(t.name.rfind("Texto: Olá", 0) == 0);
        const Clip& s = comp2.clipAt(1, 2, 4);
        CHECK(s.generatorId == kScreenCaptureId && s.isLive());
        CHECK(s.params.json()["target"].value("exe", std::string()) == "chrome.exe");
        CHECK(s.params.json()["target"].value("title", std::string()) == "Teste ção");
        CHECK(s.params.json().value("regionW", 0) == 640 && s.params.json().value("fps", std::string()) == "60");
        const Clip& d = comp2.clipAt(1, 2, 5);
        CHECK(d.generatorId == kCaptureDeviceId && d.isLive() && d.name == "Webcam: USB Video Device");
        CHECK(d.params.json().value("format", std::string()) == "1280x720@30:yuyv422");
        const Clip& c = comp2.clipAt(1, 2, 6);
        CHECK(c.generatorId == kSolidColorId && media::ParamSet(c.paramDescs(), c.params).c("color").b == 1.0f);
    }
    CHECK(text1.find("\"generator\": \"gen:texto\"") != std::string::npos);
    CHECK(text1.find("\"params\"") != std::string::npos);
    for (const Layer& l : comp2.layers)
        CHECK(l.activeDeck == -1 && l.activeColumn == -1); // nada comeca tocando
    for (const Deck& d : comp2.decks)
        for (const auto& row : d.clips)
            for (const Clip& c : row)
                CHECK(!c.isOpen());

    CHECK(out2.screens.size() == 2);
    const Screen& sc = out2.screens[0];
    CHECK(sc.name == "Palco" && sc.kind == OutputKind::Monitor && sc.monitorIndex == 1 && sc.monitorName == "DELL U2415");
    CHECK(sc.width == 1920 && sc.height == 1080 && sc.slices.size() == 2);
    CHECK(sc.slices[0].corners[kCornerTL].x == 12.5f && sc.slices[0].corners[kCornerTR].x == 950.1f);
    CHECK(sc.slices[0].edges[kEdgeRight].width == 64.0f && sc.slices[0].edges[kEdgeRight].gamma == 1.8f);
    CHECK(sc.slices[0].gain[0] == 0.9f && sc.slices[0].brightness == -0.1f);
    CHECK(sc.slices[1].flipH && sc.slices[1].flipV && !sc.slices[1].enabled);
    CHECK(out2.screens[1].kind == OutputKind::Spout && out2.screens[1].spoutName == "BSL Stage - Spout A");
    CHECK(!out2.screens[1].enabled);
    CHECK(out2.screens[0].id != out2.screens[1].id);

    // ------------------------------------------------------------------ PDF, Office e Web
    section("PDF, Office e Web: parametros no projeto");
    {
        writeAll(mediaDir / "doc.pdf", "%PDF-1.4\n");
        writeAll(mediaDir / "slides.pptx", "x");
        writeAll(mediaDir / "pagina.html", "<html></html>");
        Composition cd = Composition::createDefault(2, 5);
        Clip pdf = Clip::fromFile(mediaDir / "doc.pdf");
        Clip ppt = Clip::fromFile(mediaDir / "slides.pptx");
        Clip html = Clip::fromFile(mediaDir / "pagina.html");
        CHECK(pdf.kind == media::MediaKind::Document && ppt.kind == media::MediaKind::Office
              && html.kind == media::MediaKind::Web);
        CHECK(pdf.isPaged() && ppt.isPaged() && !html.isPaged() && html.isWeb() && html.isLive() && html.isExclusive());
        CHECK(!pdf.params.empty() && pdf.params.has("autoMode") && html.params.has("zoom")); // padrao preenchido
        pdf.setParam("page", 3);
        pdf.setParam("autoMode", "beats");
        pdf.setParam("autoBeats", 2);
        pdf.setParam("transition", "fade");
        pdf.setParam("label", "doc.pdf (p. 3)");
        pdf.fit = FitMode::Fill;
        ppt.setParam("autoMode", "seconds");
        ppt.setParam("autoSeconds", 7.5);
        html.setParam("zoom", 1.5);
        html.setParam("transparent", true);
        html.setParam("css", "body { color: red; }");
        Clip web = Clip::generator(kWebUrlId);
        web.setParam("url", "https://example.com/ao-vivo");
        web.setParam("muted", false);
        CHECK(web.isLive() && web.isRemote() && web.name == "Web: example.com");
        cd.setClip(0, 0, std::move(pdf));
        cd.setClip(0, 1, std::move(ppt));
        cd.setClip(1, 0, std::move(html));
        cd.setClip(1, 1, std::move(web));
        const std::string t = project::serialize(cd, out, proj);
        CHECK(project::saveFile(proj / "docs.bslstage", t, &err));
        Composition cd2;
        OutputSetup od2;
        const project::LoadResult rd = project::loadFile(proj / "docs.bslstage", cd2, od2);
        CHECK(rd.ok && rd.warnings.empty() && rd.missingFiles == 0);
        CHECK(project::serialize(cd2, od2, proj) == t);
        const Clip& p2 = cd2.clipAt(0, 0, 0);
        CHECK(p2.kind == media::MediaKind::Document && p2.fit == FitMode::Fill && !p2.missing);
        const media::ParamSet ps(p2.paramDescs(), p2.params);
        CHECK(ps.i("page") == 3 && ps.s("autoMode") == "beats" && ps.i("autoBeats") == 2 && ps.s("transition") == "fade");
        CHECK(p2.name == "doc.pdf (p. 3)");
        const media::ParamSet ps2(cd2.clipAt(0, 0, 1).paramDescs(), cd2.clipAt(0, 0, 1).params);
        CHECK(cd2.clipAt(0, 0, 1).kind == media::MediaKind::Office && ps2.s("autoMode") == "seconds" && ps2.f("autoSeconds") == 7.5f);
        const Clip& h2 = cd2.clipAt(0, 1, 0);
        const media::ParamSet ph(h2.paramDescs(), h2.params);
        CHECK(h2.kind == media::MediaKind::Web && ph.f("zoom") == 1.5f && ph.b("transparent") && ph.s("css") == "body { color: red; }");
        const Clip& w2 = cd2.clipAt(0, 1, 1);
        CHECK(w2.generatorId == kWebUrlId && w2.name == "Web: example.com" && !media::ParamSet(w2.paramDescs(), w2.params).b("muted"));
        CHECK(media::ParamSet(w2.paramDescs(), w2.params).s("url") == "https://example.com/ao-vivo");
        // Expandir paginas na linha: 4 paginas conhecidas, comecando na coluna 3 (a grade cresce)
        {
            Composition ce = Composition::createDefault(2, 4);
            Clip doc = Clip::fromFile(mediaDir / "doc.pdf");
            doc.setParam("autoMode", "seconds");
            doc.setParam("bg", media::param::colorJson({0.1f, 0.2f, 0.3f, 1.0f}));
            doc.fit = FitMode::Stretch;
            ce.setClip(1, 3, std::move(doc));
            CHECK(ce.expandPages(1, 3) == 0); // total de paginas ainda desconhecido
            media::setDocPageCount(mediaDir / "doc.pdf", 4);
            CHECK(ce.expandPages(1, 3) == 4);
            CHECK(ce.columnCount() == 7);
            for (int i = 0; i < 4; ++i) {
                const Clip& c = ce.clip(1, 3 + i);
                const media::ParamSet pe(c.paramDescs(), c.params);
                CHECK(c.isPaged() && pe.i("page") == i + 1 && pe.s("autoMode") == "off" && c.fit == FitMode::Stretch);
                CHECK(pe.c("bg").b == 0.3f && c.name == "doc.pdf (p. " + std::to_string(i + 1) + ")");
            }
            CHECK(ce.clip(0, 3).empty()); // so a linha do clip
            CHECK(ce.expandPages(0, 0) == 0); // celula vazia
        }

        // Solta o arquivo do clip e relinka: continua com os parametros e ganha os padroes que faltavam
        Clip moved = Clip::fromFile(mediaDir / "doc.pdf");
        moved.params = media::ParamValues();
        CHECK(project::relinkClip(moved, mediaDir / "doc.pdf") && moved.params.has("page"));
    }

    // ------------------------------------------------------------------ arquivo corrompido
    section("arquivo corrompido");
    {
        const std::string before = project::serialize(comp2, out2, proj);
        const std::string cases[] = {
            "",
            "isto nao e json",
            text1.substr(0, text1.size() / 2),                 // truncado
            "[1, 2, 3]",                                       // JSON, mas nao um objeto
            "{\"format\": \"outra-coisa\", \"version\": 1}",   // formato errado
            "{\"version\": 1}",                                // sem formato
            std::string("\x00\x01\x02\xff\xfe", 5),            // binario
        };
        for (const std::string& bad : cases) {
            Composition c = buildComposition(mediaDir);
            OutputSetup o = buildOutput(c.width, c.height);
            const std::string ref = project::serialize(c, o, proj);
            project::LoadResult br = project::parse(bad, proj, c, o);
            CHECK(!br.ok);
            CHECK(!br.error.empty());
            CHECK(project::serialize(c, o, proj) == ref); // o projeto atual fica intacto
        }
        // Arquivo inexistente
        Composition c = buildComposition(mediaDir);
        OutputSetup o;
        CHECK(!project::loadFile(proj / "nao-existe.bslstage", c, o).ok);
        CHECK(project::serialize(comp2, out2, proj) == before);

        // Tipos errados nos campos: nao pode quebrar (cai no padrao)
        const std::string weird =
            "{\"format\":\"bslstage\",\"version\":1,\"composition\":{\"width\":\"larga\",\"bpm\":null,"
            "\"master\":5},\"layers\":[7,{\"name\":3,\"opacity\":\"x\"}],\"decks\":[{\"columns\":-4,"
            "\"clips\":[{\"layer\":\"a\"},{\"layer\":0,\"column\":0,\"path\":12},null,"
            "{\"layer\":99,\"column\":0,\"path\":\"x.mp4\"}]}],\"output\":{\"screens\":[{\"slices\":[{\"corners\":[1]}]}]}}";
        Composition w;
        OutputSetup wo;
        project::LoadResult wr = project::parse(weird, proj, w, wo);
        CHECK(wr.ok);
        CHECK(w.width == 1920 && w.layerCount() == 2 && w.deckCount() == 1);
        CHECK(w.decks[0].columnCount() == 1); // clamp de -4 para o minimo
    }

    // ------------------------------------------------------------------ parametros invalidos / gerador desconhecido
    section("parametros de gerador");
    {
        const std::string odd =
            "{\"format\":\"bslstage\",\"version\":1,\"decks\":[{\"columns\":4,\"clips\":["
            "{\"layer\":0,\"column\":0,\"generator\":\"gen:texto\",\"params\":{\"size\":\"enorme\",\"text\":5,"
            "\"color\":[1,2],\"marquee\":7,\"campoNovo\":{\"a\":1}}},"
            "{\"layer\":0,\"column\":1,\"generator\":\"gen:cor-solida\",\"params\":[1,2,3]},"
            "{\"layer\":0,\"column\":2,\"generator\":\"gen:que-nao-existe\",\"params\":{}},"
            "{\"layer\":0,\"column\":3,\"generator\":\"gen:padrao-de-teste\"}]}]}";
        Composition w;
        OutputSetup wo;
        project::LoadResult wr = project::parse(odd, proj, w, wo);
        CHECK(wr.ok);
        CHECK(wr.warnings.size() == 1); // so o gerador desconhecido
        const Clip& t = w.clipAt(0, 0, 0);
        const media::ParamSet ps(t.paramDescs(), t.params);
        CHECK(ps.i("size") == 96);                    // tipo errado -> padrao
        CHECK(ps.s("text") == "Seu texto aqui");
        CHECK(ps.c("color").r == 1.0f && ps.c("color").a == 1.0f);
        CHECK(ps.s("marquee") == "none");
        CHECK(t.params.has("campoNovo"));             // chave desconhecida e mantida
        CHECK(w.clipAt(0, 0, 1).generatorId == kSolidColorId);
        CHECK(w.clipAt(0, 0, 2).empty());             // gerador desconhecido: ignorado
        CHECK(w.clipAt(0, 0, 3).isTestPattern());
    }

    // ------------------------------------------------------------------ versao futura / extras / ausentes
    section("versao futura e campos desconhecidos");
    {
        std::string future = text1;
        const std::string needle = "\"version\": 1";
        future.replace(future.find(needle), needle.size(), "\"version\": 99,\n  \"campoNovo\": {\"x\": [1, 2, 3]}");
        Composition c;
        OutputSetup o;
        project::LoadResult fr = project::parse(future, proj, c, o);
        CHECK(fr.ok);
        CHECK(!fr.warnings.empty());
        CHECK(countClips(c) == 11);

        // Campos ausentes recebem o padrao
        Composition d;
        OutputSetup dout;
        project::LoadResult mr = project::parse("{\"format\":\"bslstage\",\"version\":1}", proj, d, dout);
        CHECK(mr.ok && mr.warnings.empty());
        CHECK(d.width == 1920 && d.height == 1080 && d.fps == 60.0f && d.bpm == 120.0f);
        CHECK(d.layerCount() == 3 && d.deckCount() == 1 && d.columnCount() == 8);
        CHECK(dout.screens.size() == 1 && dout.screens[0].slices.size() == 1);
    }

    // ------------------------------------------------------------------ mover a pasta do projeto (relPath)
    section("mover a pasta do projeto");
    {
        const fs::path moved = root / u8"movido";
        fs::rename(proj, moved, ec);
        CHECK(!ec);
        const fs::path movedFile = moved / "show.bslstage";
        Composition c;
        OutputSetup o;
        project::LoadResult mr = project::loadFile(movedFile, c, o);
        CHECK(mr.ok);
        CHECK(mr.missingFiles == 0); // o path absoluto antigo nao existe mais: quem achou foi o relPath
        const Clip& photo = c.clipAt(0, 0, 0);
        CHECK(!photo.missing);
        CHECK(photo.path.lexically_normal() == (moved / "media" / "photo.jpg").lexically_normal());
        CHECK(!fs::exists(mediaDir / "photo.jpg", ec));
        // Salvar na nova pasta e reabrir continua identico
        const std::string t = project::serialize(c, o, moved);
        CHECK(project::saveFile(movedFile, t, &err));
        Composition c2;
        OutputSetup o2;
        CHECK(project::loadFile(movedFile, c2, o2).ok);
        CHECK(project::serialize(c2, o2, moved) == t);

        // ------------------------------------------------------------ arquivo ausente + Localizar
        section("arquivo ausente e localizar");
        fs::remove(moved / "media" / "photo.jpg", ec);
        fs::remove(moved / "media" / "anim.gif", ec);
        Composition m;
        OutputSetup mo;
        project::LoadResult ar = project::loadFile(movedFile, m, mo);
        CHECK(ar.ok);
        CHECK(ar.missingFiles == 2);
        CHECK(m.clipAt(0, 0, 0).missing && m.clipAt(0, 2, 7).missing);
        CHECK(!m.clipAt(0, 1, 2).missing && !m.clipAt(1, 2, 0).missing);
        CHECK(m.clipAt(0, 0, 0).name == "photo.jpg");   // o nome continua visivel
        CHECK(m.clipAt(0, 0, 0).type == Clip::Type::File);
        CHECK(countClips(m) == 11);                      // o clip ausente continua na grade
        CHECK(!m.trigger(0, 0));                         // disparar nao faz nada
        CHECK(m.layers[0].activeColumn == -1);
        // O ausente sobrevive a um save: nada e perdido
        const std::string mt = project::serialize(m, mo, moved);
        CHECK(mt.find("photo.jpg") != std::string::npos && mt.find("anim.gif") != std::string::npos);

        // Localizar um deles em outra pasta; o outro e reencontrado pelo nome na mesma pasta
        const fs::path found = root / "achados";
        fs::create_directories(found, ec);
        fs::copy_file(media / "photo.jpg", found / "photo.jpg", fs::copy_options::overwrite_existing, ec);
        fs::copy_file(media / "anim.gif", found / "anim.gif", fs::copy_options::overwrite_existing, ec);
        CHECK(project::relinkClip(m.clipAt(0, 0, 0), found / "photo.jpg"));
        CHECK(!m.clipAt(0, 0, 0).missing);
        CHECK(m.clipAt(0, 2, 7).missing);
        CHECK(project::relinkMissingInFolder(m, found) == 1);
        CHECK(!m.clipAt(0, 2, 7).missing);
        CHECK(project::relinkMissingInFolder(m, found) == 0);
        CHECK(!project::relinkClip(m.clipAt(0, 0, 0), found / "arquivo.txt")); // formato nao suportado
    }

    fs::remove_all(root, ec);
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "PROJECTTEST OK\n" : "PROJECTTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
