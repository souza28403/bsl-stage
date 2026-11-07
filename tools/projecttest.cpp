#include "control/ActionRegistry.h"
#include "core/Composition.h"
#include "media/DocInfo.h"
#include "io/ProjectFile.h"
#include "output/OutputSetup.h"

#include <nlohmann/json.hpp>

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
    comp.addColumn();
    comp.setClip(0, 8, Clip::fromFile(media / "alpha.png"));
    Clip hevc = Clip::fromFile(media / "hevc.mkv");
    hevc.setSpeed(2.0f);
    hevc.fit = FitMode::Original;
    comp.setClip(2, 0, std::move(hevc));
    comp.setClip(1, 1, Clip::testPattern());

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
    Clip ndi = Clip::generator(kNdiId);
    ndi.setParam("source", "MAQUINA (BSL Stage - Tela 1)");
    ndi.setParam("lowLatency", true);
    comp.setClip(2, 7, std::move(ndi));
    Clip spout = Clip::generator(kSpoutId);
    spout.setParam("sender", "Fonte de teste");
    comp.setClip(1, 7, std::move(spout));
    return comp;
}

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
    left.zFilter = kZoneFront;
    left.zSource = 2;
    Slice right = makeFullSlice("Direita", cw, ch, s1.width, s1.height);
    right.input = {cw / 2, 0, cw - cw / 2, ch};
    right.corners[kCornerTL] = {1000.0f, 20.0f};
    right.corners[kCornerBR] = {1900.0f, 1060.0f};
    right.flipH = true;
    right.flipV = true;
    right.enabled = false;
    right.zFilter = kZoneBack;
    right.zSource = kZoneSourceMaster;
    right.edges[kEdgeLeft].width = 64.0f;
    s1.slices.push_back(left);
    s1.slices.push_back(right);

    const int b = out.addScreen(cw, ch);
    Screen& s2 = out.screens[static_cast<size_t>(b)];
    s2.name = "Spout A";
    s2.kind = OutputKind::Spout;
    s2.spoutName = "BSL Stage - Spout A";
    s2.ndiName = "BSL Stage - NDI A";
    s2.ndiBgra = true;
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

}

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
    const fs::path proj = root / u8"projeção";
    fs::create_directories(proj / "media", ec);
    for (const char* name : {"photo.jpg", "h264_1080p.mp4", "anim.gif", "alpha.png", "hevc.mkv"})
        fs::copy_file(media / name, proj / "media" / name, fs::copy_options::overwrite_existing, ec);

    const fs::path file = proj / "show.bslstage";
    const fs::path mediaDir = proj / "media";

    section("save -> load -> save");
    Composition comp = buildComposition(mediaDir);
    OutputSetup out = buildOutput(comp.width, comp.height);
    CHECK(countClips(comp) == 13);
    comp.layers[1].sliceOnly = true;
    comp.nextLayerId = 9;
    out.screens[0].slices[0].source = kSliceFromLayer;
    out.screens[0].slices[0].layerId = comp.layers[1].id;
    out.screens[0].slices[0].includeMaster = true;
    out.screens[0].slices[1].source = kSliceFromLayer;
    out.screens[0].slices[1].layerId = comp.layers[2].id;
    const int layerIdA = comp.layers[1].id, layerIdB = comp.layers[2].id;

    const std::string text1 = project::serialize(comp, out, proj);
    std::string err;
    CHECK(project::saveFile(file, text1, &err));
    CHECK(readAll(file) == text1);
    CHECK(!fs::exists(fs::path(file).concat(".tmp"), ec));
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

    CHECK(project::saveFile(file, text2, &err));
    CHECK(readAll(file) == text1);

    CHECK(comp2.width == 1280 && comp2.height == 720);
    CHECK(comp2.fps == 30.0f && comp2.bpm == 128.5f);
    CHECK(comp2.masterOpacity == 0.8f && comp2.masterBypass);
    CHECK(comp2.layerCount() == 3 && comp2.deckCount() == 2 && comp2.currentDeck == 1);
    CHECK(comp2.layers[0].name == "Fundo" && comp2.layers[0].blendMode == BlendMode::Add);
    CHECK(comp2.layers[1].bypass && comp2.layers[2].solo);
    CHECK(comp2.layers[2].blendMode == BlendMode::Difference);
    CHECK(comp2.decks[0].name == "Abertura" && comp2.decks[0].columnCount() == 8);
    CHECK(comp2.decks[1].name == "Segundo ato" && comp2.decks[1].columnCount() == 9);
    CHECK(countClips(comp2) == 13);
    const Clip& v = comp2.clipAt(0, 1, 2);
    CHECK(v.type == Clip::Type::File && v.kind == media::MediaKind::Video && !v.missing);
    CHECK(v.speed == 0.5f && v.loop == media::LoopMode::Once && v.fit == FitMode::Fill);
    CHECK(comp2.clipAt(0, 2, 7).loop == media::LoopMode::PingPong && comp2.clipAt(0, 2, 7).fit == FitMode::Stretch);
    CHECK(comp2.clipAt(0, 0, 3).isTestPattern());
    CHECK(comp2.clipAt(0, 0, 3).params.empty());
    CHECK(comp2.clipAt(1, 2, 0).speed == 2.0f && comp2.clipAt(1, 2, 0).fit == FitMode::Original);
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
        CHECK(ps.b("wrap") && ps.f("lineSpacing") == 1.0f);
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
        const Clip& n = comp2.clipAt(1, 2, 7);
        CHECK(n.generatorId == kNdiId && n.isLive() && n.name == "NDI: MAQUINA (BSL Stage - Tela 1)");
        CHECK(media::ParamSet(n.paramDescs(), n.params).s("source") == "MAQUINA (BSL Stage - Tela 1)");
        CHECK(media::ParamSet(n.paramDescs(), n.params).b("lowLatency") && !media::ParamSet(n.paramDescs(), n.params).b("lowBandwidth"));
        const Clip& sp = comp2.clipAt(1, 1, 7);
        CHECK(sp.generatorId == kSpoutId && sp.isLive() && sp.name == "Spout: Fonte de teste");
    }
    CHECK(text1.find("\"generator\": \"gen:texto\"") != std::string::npos);
    CHECK(text1.find("\"params\"") != std::string::npos);
    for (const Layer& l : comp2.layers)
        CHECK(l.activeDeck == -1 && l.activeColumn == -1);
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
    CHECK(sc.slices[0].zFilter == kZoneFront && sc.slices[0].zSource == 2);
    CHECK(sc.slices[1].zFilter == kZoneBack && sc.slices[1].zSource == kZoneSourceMaster);
    CHECK(out2.screens[1].slices[0].zFilter == kZoneAll && out2.screens[1].slices[0].zSource == kZoneSourceMaster);
    CHECK(text1.find("\"zFilter\": \"front\"") != std::string::npos && text1.find("\"zFilter\": \"back\"") != std::string::npos);
    {
        OutputSetup plain = buildOutput(comp.width, comp.height);
        for (Screen& s : plain.screens)
            for (Slice& sl : s.slices) {
                sl.zFilter = kZoneAll;
                sl.zSource = kZoneSourceMaster;
            }
        const std::string plainText = project::serialize(comp, plain, proj);
        CHECK(plainText.find("zFilter") == std::string::npos && plainText.find("zSource") == std::string::npos);
        std::string bad = text1;
        const size_t at = bad.find("\"zFilter\": \"front\"");
        if (at != std::string::npos)
            bad.replace(at, std::string("\"zFilter\": \"front\"").size(), "\"zFilter\": \"inexistente\"");
        const fs::path badFile = proj / "zinvalido.bslstage";
        writeAll(badFile, bad);
        Composition c3;
        OutputSetup o3;
        const project::LoadResult r3 = project::loadFile(badFile, c3, o3);
        CHECK(r3.ok && o3.screens.size() == 2 && o3.screens[0].slices[0].zFilter == kZoneAll);
    }
    CHECK(sc.slices[0].source == kSliceFromLayer && sc.slices[0].layerId == layerIdA && sc.slices[0].includeMaster);
    CHECK(sc.slices[1].source == kSliceFromLayer && sc.slices[1].layerId == layerIdB && !sc.slices[1].includeMaster);
    CHECK(out2.screens[1].slices[0].source == kSliceFromComposition && out2.screens[1].slices[0].layerId == 0);
    CHECK(comp2.layers[1].sliceOnly && !comp2.layers[0].sliceOnly && !comp2.layers[2].sliceOnly);
    CHECK(comp2.layers[1].id == layerIdA && comp2.layers[2].id == layerIdB && comp2.nextLayerId == 9);
    CHECK(comp2.findLayerById(layerIdA) == 1 && comp2.findLayerById(layerIdB) == 2 && comp2.findLayerById(999) < 0);
    CHECK(text1.find("\"sliceOnly\": true") != std::string::npos && text1.find("\"source\": \"layer\"") != std::string::npos);
    {
        Composition plainComp = buildComposition(mediaDir);
        OutputSetup plainOut = buildOutput(plainComp.width, plainComp.height);
        const std::string plainText = project::serialize(plainComp, plainOut, proj);
        CHECK(plainText.find("sliceOnly") == std::string::npos && plainText.find("includeMaster") == std::string::npos
              && plainText.find("\"source\": \"layer\"") == std::string::npos && plainText.find("\"layerId\"") == std::string::npos);
        std::swap(comp2.layers[1], comp2.layers[2]);
        CHECK(comp2.findLayerById(layerIdA) == 2 && comp2.findLayerById(layerIdB) == 1);
        std::swap(comp2.layers[1], comp2.layers[2]);
    }
    CHECK(out2.screens[1].kind == OutputKind::Spout && out2.screens[1].spoutName == "BSL Stage - Spout A");
    CHECK(!out2.screens[1].enabled);
    CHECK(out2.screens[1].ndiName == "BSL Stage - NDI A" && out2.screens[1].ndiBgra && !out2.screens[0].ndiBgra);
    CHECK(out2.screens[0].id != out2.screens[1].id);

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
        CHECK(!pdf.params.empty() && pdf.params.has("autoMode") && html.params.has("zoom"));
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
        {
            Composition ce = Composition::createDefault(2, 4);
            Clip doc = Clip::fromFile(mediaDir / "doc.pdf");
            doc.setParam("autoMode", "seconds");
            doc.setParam("bg", media::param::colorJson({0.1f, 0.2f, 0.3f, 1.0f}));
            doc.fit = FitMode::Stretch;
            ce.setClip(1, 3, std::move(doc));
            CHECK(ce.expandPages(1, 3) == 0);
            media::setDocPageCount(mediaDir / "doc.pdf", 4);
            CHECK(ce.expandPages(1, 3) == 4);
            CHECK(ce.columnCount() == 7);
            for (int i = 0; i < 4; ++i) {
                const Clip& c = ce.clip(1, 3 + i);
                const media::ParamSet pe(c.paramDescs(), c.params);
                CHECK(c.isPaged() && pe.i("page") == i + 1 && pe.s("autoMode") == "off" && c.fit == FitMode::Stretch);
                CHECK(pe.c("bg").b == 0.3f && c.name == "doc.pdf (p. " + std::to_string(i + 1) + ")");
            }
            CHECK(ce.clip(0, 3).empty());
            CHECK(ce.expandPages(0, 0) == 0);
        }

        Clip moved = Clip::fromFile(mediaDir / "doc.pdf");
        moved.params = media::ParamValues();
        CHECK(project::relinkClip(moved, mediaDir / "doc.pdf") && moved.params.has("page"));
    }

    section("arquivo corrompido");
    {
        const std::string before = project::serialize(comp2, out2, proj);
        const std::string cases[] = {
            "",
            "isto nao e json",
            text1.substr(0, text1.size() / 2),
            "[1, 2, 3]",
            "{\"format\": \"outra-coisa\", \"version\": 1}",
            "{\"version\": 1}",
            std::string("\x00\x01\x02\xff\xfe", 5),
        };
        for (const std::string& bad : cases) {
            Composition c = buildComposition(mediaDir);
            OutputSetup o = buildOutput(c.width, c.height);
            const std::string ref = project::serialize(c, o, proj);
            project::LoadResult br = project::parse(bad, proj, c, o);
            CHECK(!br.ok);
            CHECK(!br.error.empty());
            CHECK(project::serialize(c, o, proj) == ref);
        }
        Composition c = buildComposition(mediaDir);
        OutputSetup o;
        CHECK(!project::loadFile(proj / "nao-existe.bslstage", c, o).ok);
        CHECK(project::serialize(comp2, out2, proj) == before);

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
        CHECK(w.decks[0].columnCount() == 1);
    }

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
        CHECK(wr.warnings.size() == 1);
        const Clip& t = w.clipAt(0, 0, 0);
        const media::ParamSet ps(t.paramDescs(), t.params);
        CHECK(ps.i("size") == 96);
        CHECK(ps.s("text") == "Seu texto aqui");
        CHECK(ps.c("color").r == 1.0f && ps.c("color").a == 1.0f);
        CHECK(ps.s("marquee") == "none");
        CHECK(t.params.has("campoNovo"));
        CHECK(w.clipAt(0, 0, 1).generatorId == kSolidColorId);
        CHECK(!w.clipAt(0, 0, 2).empty() && w.clipAt(0, 0, 2).missingPlugin);
        CHECK(w.clipAt(0, 0, 2).generatorId == "gen:que-nao-existe");
        CHECK(w.clipAt(0, 0, 3).isTestPattern());
    }

    section("plugins removidos: projeto antigo abre com o clip e o efeito ausentes, params preservados");
    {
        const std::string old =
            "{\"format\":\"bslstage\",\"version\":1,\"decks\":[{\"columns\":4,\"clips\":["
            "{\"layer\":0,\"column\":0,\"generator\":\"bsl:led-mapper\",\"params\":{\"columns\":4,\"rows\":3}},"
            "{\"layer\":0,\"column\":1,\"generator\":\"bsl:web-motion\",\"params\":{\"text\":\"OI\"}},"
            "{\"layer\":0,\"column\":2,\"generator\":\"bsl:ppt-manager\",\"params\":{\"document\":\"C:/x.pptx\"}},"
            "{\"layer\":0,\"column\":3,\"generator\":\"bsl:gen-backgrounds\",\"params\":{\"style\":\"aurora\"},"
            "\"effects\":[{\"type\":\"bsl:alpha-reactor\",\"params\":{\"auraIntensity\":70}},"
            "{\"type\":\"bsl:datamosh-pixelsort\"},{\"type\":\"bsl:audio-typo\",\"params\":{\"djText\":\"DJ X\"}}]}"
            "]}]}";
        Composition w;
        OutputSetup wo;
        project::LoadResult oldRes = project::parse(old, proj, w, wo);
        CHECK(oldRes.ok);
        CHECK(oldRes.warnings.size() == 4);
        const char* ids[] = {"bsl:led-mapper", "bsl:web-motion", "bsl:ppt-manager", "bsl:gen-backgrounds"};
        for (int i = 0; i < 4; ++i) {
            const Clip& c = w.clipAt(0, 0, i);
            CHECK(!c.empty() && c.missing && c.missingPlugin && c.generatorId == ids[i]);
            CHECK(!c.isOpen() && c.paramDescs().empty());
        }
        CHECK(w.clipAt(0, 0, 0).params.json().value("columns", 0) == 4);
        CHECK(w.clipAt(0, 0, 1).params.json().value("text", std::string()) == "OI");
        CHECK(w.clipAt(0, 0, 2).params.json().value("document", std::string()) == "C:/x.pptx");
        CHECK(!w.trigger(0, 0));
        const fx::EffectChain& chain = w.clipAt(0, 0, 3).effects;
        CHECK(chain.size() == 3 && chain.items[0].typeId == "bsl:alpha-reactor" && chain.items[1].typeId == "bsl:datamosh-pixelsort");
        CHECK(chain.items[0].params.json().value("auraIntensity", 0.0) == 70.0);
        CHECK(chain.items[2].params.json().value("djText", std::string()) == "DJ X");
        const std::string again = project::serialize(w, wo, proj);
        Composition w2;
        OutputSetup wo2;
        project::LoadResult againRes = project::parse(again, proj, w2, wo2);
        CHECK(againRes.ok && w2.clipAt(0, 0, 0).generatorId == "bsl:led-mapper" && w2.clipAt(0, 0, 0).params.json().value("rows", 0) == 3);
        CHECK(w2.clipAt(0, 0, 3).effects.size() == 3 && w2.clipAt(0, 0, 3).effects.items[2].typeId == "bsl:audio-typo");
        CHECK(project::serialize(w2, wo2, proj) == again);
    }

    section("replayMode: padrao por tipo e ida e volta");
    {
        Composition w = Composition::createDefault(1, 4);
        w.setClip(0, 0, Clip::generator(kWebUrlId));
        w.setClip(0, 1, Clip::generator(kNdiId));
        w.setClip(0, 2, Clip::generator(kSolidColorId));
        CHECK(w.clip(0, 0).replay == ReplayMode::Resume && w.clip(0, 1).replay == ReplayMode::Resume);
        CHECK(w.clip(0, 2).replay == ReplayMode::Restart);
        if (!media.empty()) {
            w.setClip(0, 3, Clip::fromFile(media / "h264_1080p.mp4"));
            CHECK(w.clip(0, 3).replay == ReplayMode::Restart);
            w.clip(0, 3).replay = ReplayMode::Resume;
        }
        w.clip(0, 0).replay = ReplayMode::Restart;
        OutputSetup wo = buildOutput(w.width, w.height);
        const std::string text = project::serialize(w, wo, proj);
        Composition back;
        OutputSetup backOut;
        CHECK(project::parse(text, proj, back, backOut).ok);
        CHECK(back.clip(0, 0).replay == ReplayMode::Restart);
        CHECK(back.clip(0, 1).replay == ReplayMode::Resume);
        if (!media.empty())
            CHECK(back.clip(0, 3).replay == ReplayMode::Resume);
        Composition plain = Composition::createDefault(1, 2);
        plain.setClip(0, 0, Clip::generator(kWebUrlId));
        CHECK(project::serialize(plain, wo, proj).find("replayMode") == std::string::npos);
    }

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
        CHECK(countClips(c) == 13);

        Composition d;
        OutputSetup dout;
        project::LoadResult mr = project::parse("{\"format\":\"bslstage\",\"version\":1}", proj, d, dout);
        CHECK(mr.ok && mr.warnings.empty());
        CHECK(d.width == 1920 && d.height == 1080 && d.fps == 60.0f && d.bpm == 120.0f);
        CHECK(d.layerCount() == 3 && d.deckCount() == 1 && d.columnCount() == 8);
        CHECK(dout.screens.size() == 1 && dout.screens[0].slices.size() == 1);
    }

    section("slices e camadas: projeto antigo e ids");
    {
        const std::string old = "{\"format\":\"bslstage\",\"version\":1,\"composition\":{\"width\":640,\"height\":360},"
                                "\"layers\":[{\"name\":\"A\"},{\"name\":\"B\"},{\"name\":\"C\"}],"
                                "\"decks\":[{\"name\":\"D\",\"columns\":2}],"
                                "\"output\":{\"screens\":[{\"name\":\"T\",\"slices\":[{\"name\":\"S\","
                                "\"input\":{\"x\":0,\"y\":0,\"w\":640,\"h\":360}}]}]}}";
        Composition c;
        OutputSetup o;
        const project::LoadResult lr = project::parse(old, proj, c, o);
        CHECK(lr.ok && lr.warnings.empty());
        CHECK(c.layerCount() == 3 && c.layers[0].id == 1 && c.layers[1].id == 2 && c.layers[2].id == 3 && c.nextLayerId == 4);
        CHECK(!c.layers[0].sliceOnly && !c.layers[1].sliceOnly && !c.layers[2].sliceOnly);
        CHECK(o.screens.size() == 1 && o.screens[0].slices.size() == 1);
        const Slice& s = o.screens[0].slices[0];
        CHECK(s.source == kSliceFromComposition && s.layerId == 0 && !s.includeMaster);
        const std::string saved = project::serialize(c, o, proj);
        Composition c2;
        OutputSetup o2;
        CHECK(project::parse(saved, proj, c2, o2).ok);
        CHECK(c2.layers[0].id == 1 && c2.layers[2].id == 3 && c2.nextLayerId == 4);
        CHECK(project::serialize(c2, o2, proj) == saved);

        const std::string dup = "{\"format\":\"bslstage\",\"version\":1,\"layers\":[{\"id\":5,\"name\":\"A\"},"
                                "{\"id\":5,\"name\":\"B\"},{\"id\":-3,\"name\":\"C\"},{\"id\":2,\"name\":\"D\"}],"
                                "\"composition\":{\"nextLayerId\":4}}";
        Composition c3;
        OutputSetup o3;
        CHECK(project::parse(dup, proj, c3, o3).ok && c3.layerCount() == 4);
        CHECK(c3.layers[0].id == 5 && c3.layers[3].id == 2);
        CHECK(c3.layers[1].id > 0 && c3.layers[2].id > 0 && c3.layers[1].id != c3.layers[0].id
              && c3.layers[1].id != c3.layers[2].id && c3.layers[1].id != c3.layers[3].id && c3.layers[2].id != c3.layers[3].id);
        CHECK(c3.findLayerById(c3.layers[1].id) == 1 && c3.nextLayerId > 5);
        const std::string orphan = "{\"format\":\"bslstage\",\"version\":1,\"layers\":[{\"id\":1,\"name\":\"A\"}],"
                                   "\"output\":{\"screens\":[{\"name\":\"T\",\"slices\":[{\"name\":\"S\","
                                   "\"source\":\"layer\",\"layerId\":77,\"includeMaster\":true}]}]}}";
        Composition c4;
        OutputSetup o4;
        CHECK(project::parse(orphan, proj, c4, o4).ok);
        const Slice& os = o4.screens[0].slices[0];
        CHECK(os.source == kSliceFromLayer && os.layerId == 77 && os.includeMaster && c4.findLayerById(77) < 0);
        std::string weird = orphan;
        weird.replace(weird.find("\"layer\""), 7, "\"xyz\"");
        Composition c5;
        OutputSetup o5;
        CHECK(project::parse(weird, proj, c5, o5).ok && o5.screens[0].slices[0].source == kSliceFromComposition);
    }

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
        CHECK(mr.missingFiles == 0);
        const Clip& photo = c.clipAt(0, 0, 0);
        CHECK(!photo.missing);
        CHECK(photo.path.lexically_normal() == (moved / "media" / "photo.jpg").lexically_normal());
        CHECK(!fs::exists(mediaDir / "photo.jpg", ec));
        const std::string t = project::serialize(c, o, moved);
        CHECK(project::saveFile(movedFile, t, &err));
        Composition c2;
        OutputSetup o2;
        CHECK(project::loadFile(movedFile, c2, o2).ok);
        CHECK(project::serialize(c2, o2, moved) == t);

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
        CHECK(m.clipAt(0, 0, 0).name == "photo.jpg");
        CHECK(m.clipAt(0, 0, 0).type == Clip::Type::File);
        CHECK(countClips(m) == 13);
        CHECK(!m.trigger(0, 0));
        CHECK(m.layers[0].activeColumn == -1);
        const std::string mt = project::serialize(m, mo, moved);
        CHECK(mt.find("photo.jpg") != std::string::npos && mt.find("anim.gif") != std::string::npos);

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
        CHECK(!project::relinkClip(m.clipAt(0, 0, 0), found / "arquivo.txt"));
    }

    {
        section("efeitos: cadeias de clip, camada e master");
        Composition fxComp = Composition::createDefault(2, 3);
        fxComp.setClip(0, 0, Clip::fromFile(media / "photo.jpg"));
        fx::EffectChain& chain = fxComp.clip(0, 0).effects;
        chain.add("native:blur").setParam("radius", media::param::floatJson(12.5f));
        chain.add("native:invert").bypass = true;
        chain.add("ffgl:plugin-ausente").setParam("x", 0.5);
        fxComp.setClip(1, 1, Clip::generator("isf:Gradiente animado.fs"));
        fxComp.layers[1].effects.add("native:color").setParam("hue", 45.0);
        fxComp.masterEffects.add("native:vignette");
        fxComp.masterEffects.add("native:strobe").setParam("mode", "free");
        const OutputSetup fxOut = buildOutput(fxComp.width, fxComp.height);

        const std::string t = project::serialize(fxComp, fxOut, proj);
        CHECK(t.find("\"effects\"") != std::string::npos);
        Composition back;
        OutputSetup backOut;
        const project::LoadResult lr = project::parse(t, proj, back, backOut);
        CHECK(lr.ok && lr.warnings.empty());
        const fx::EffectChain& bc = back.clipAt(0, 0, 0).effects;
        CHECK(bc.size() == 3 && bc.items[0].typeId == "native:blur" && !bc.items[0].bypass);
        CHECK(bc.items[0].params.json().value("radius", 0.0) == 12.5);
        CHECK(bc.items[1].typeId == "native:invert" && bc.items[1].bypass);
        CHECK(bc.items[2].typeId == "ffgl:plugin-ausente" && bc.items[2].params.json().value("x", 0.0) == 0.5);
        CHECK(back.clipAt(0, 1, 1).isGenerator() && back.clipAt(0, 1, 1).generatorId == "isf:Gradiente animado.fs");
        CHECK(back.clipAt(0, 1, 1).name == "Gradiente animado");
        CHECK(back.layers[0].effects.empty() && back.layers[1].effects.size() == 1);
        CHECK(back.layers[1].effects.items[0].params.json().value("hue", 0.0) == 45.0);
        CHECK(back.masterEffects.size() == 2 && back.masterEffects.items[1].typeId == "native:strobe");
        CHECK(project::serialize(back, backOut, proj) == t);
        Composition plain = Composition::createDefault(1, 1);
        CHECK(project::serialize(plain, backOut, proj).find("effects") == std::string::npos);

        using OJson = nlohmann::ordered_json;
        OJson j = OJson::parse(t);
        for (auto& clip : j["decks"][0]["clips"])
            if (clip.contains("effects"))
                clip["effects"] = OJson::array({5, OJson{{"type", 7}}, OJson{{"type", "native:invert"}, {"bypass", "x"}, {"params", 3}}});
        j["layers"][1]["effects"] = "nao e uma lista";
        Composition odd;
        OutputSetup oddOut;
        const project::LoadResult orr = project::parse(j.dump(), proj, odd, oddOut);
        CHECK(orr.ok && !orr.warnings.empty());
        CHECK(odd.clipAt(0, 0, 0).effects.size() == 1 && !odd.clipAt(0, 0, 0).effects.items[0].bypass);
        CHECK(odd.layers[1].effects.empty() && odd.masterEffects.size() == 2);

        fx::EffectChain copy = fxComp.clipAt(0, 0, 0).effects;
        CHECK(copy.size() == 3 && copy.items[0].uid != fxComp.clipAt(0, 0, 0).effects.items[0].uid);
        CHECK(copy.toJson() == fxComp.clipAt(0, 0, 0).effects.toJson());
    }

    {
        section("Pixel Map: configuracao no projeto (aditivo)");
        Composition pc = Composition::createDefault(2, 3);
        pc.width = 1920;
        pc.height = 1080;
        const OutputSetup pOut = buildOutput(pc.width, pc.height);

        const std::string untouched = project::serialize(pc, pOut, proj);
        CHECK(untouched.find("pixelMap") == std::string::npos);
        pc.width = 1280;
        pc.height = 720;
        CHECK(project::serialize(pc, pOut, proj).find("pixelMap") == std::string::npos);
        pc.width = 1920;
        pc.height = 1080;

        pixelmap::Config& cfg = pc.pixelMap;
        cfg.physW = 2048;
        cfg.physH = 640;
        cfg.watermark = pixelmap::Watermark::Image;
        cfg.watermarkPath = "C:/logos/bsl \xC3\xA7.png";
        cfg.panels.push_back(pixelmap::makePanel("Testeira Palco", 0, 0, 2048, 128));
        pixelmap::Panel lat = pixelmap::makePanel("Lateral \xC3\x8D", 0, 128, 512, 512);
        lat.color = 0x123456;
        lat.byCabinets = true;
        lat.cols = 4;
        lat.rows = 4;
        lat.cabW = 128;
        lat.cabH = 128;
        cfg.panels.push_back(lat);
        const std::string t = project::serialize(pc, pOut, proj);
        CHECK(t.find("\"pixelMap\"") != std::string::npos && t.find("\"Testeira Palco\"") != std::string::npos);

        Composition back;
        OutputSetup backOut;
        const project::LoadResult lr = project::parse(t, proj, back, backOut);
        CHECK(lr.ok && lr.warnings.empty());
        CHECK(back.pixelMap == pc.pixelMap);
        CHECK(back.pixelMap.panels.size() == 2 && back.pixelMap.panels[1].color == 0x123456 && back.pixelMap.panels[1].byCabinets);
        CHECK(back.pixelMap.panels[1].cols == 4 && back.pixelMap.panels[1].w == 512 && back.pixelMap.panels[1].name == "Lateral \xC3\x8D");
        CHECK(back.pixelMap.watermark == pixelmap::Watermark::Image && back.pixelMap.watermarkPath == cfg.watermarkPath);
        CHECK(project::serialize(back, backOut, proj) == t);

        cfg.linkCanvas = false;
        cfg.inW = 3000;
        cfg.inH = 900;
        const std::string tFree = project::serialize(pc, pOut, proj);
        Composition freeIn;
        OutputSetup freeInOut;
        CHECK(project::parse(tFree, proj, freeIn, freeInOut).ok);
        CHECK(!freeIn.pixelMap.linkCanvas && freeIn.pixelMap.inW == 3000 && freeIn.pixelMap.inH == 900);
        cfg.linkCanvas = true;
        using OJson = nlohmann::ordered_json;
        OJson j = OJson::parse(t);
        j["composition"]["width"] = 1000;
        j["composition"]["height"] = 500;
        Composition linked;
        OutputSetup linkedOut;
        CHECK(project::parse(j.dump(), proj, linked, linkedOut).ok);
        CHECK(linked.pixelMap.inW == 1000 && linked.pixelMap.inH == 500);

        j["pixelMap"]["physical"] = {{"w", "x"}, {"h", 99999999}};
        j["pixelMap"]["watermark"] = {{"kind", "image"}};
        j["pixelMap"]["panels"] = OJson::array({5, OJson{{"name", 3}, {"w", -4}, {"h", 1e9}, {"x", "a"}}});
        Composition odd;
        OutputSetup oddOut;
        const project::LoadResult orr = project::parse(j.dump(), proj, odd, oddOut);
        CHECK(orr.ok);
        CHECK(odd.pixelMap.physW == 2048 && odd.pixelMap.physH == pixelmap::kMaxDim);
        CHECK(odd.pixelMap.watermark == pixelmap::Watermark::Monogram);
        CHECK(odd.pixelMap.panels.size() == 2 && odd.pixelMap.panels[1].w == 1 && odd.pixelMap.panels[1].h == pixelmap::kMaxDim);
        Composition none;
        OutputSetup noneOut;
        CHECK(project::parse(untouched, proj, none, noneOut).ok && none.pixelMap.panels.empty() && none.pixelMap.linkCanvas);
    }

    {
        section("atalhos de teclado: acompanham o projeto (aditivo)");
        Composition kc = Composition::createDefault(3, 8);
        const OutputSetup kOut = buildOutput(kc.width, kc.height);

        CHECK(kc.keys == control::defaultProjectKeys());
        const std::string untouched = project::serialize(kc, kOut, proj);
        CHECK(untouched.find("\"keys\"") == std::string::npos);
        Composition old;
        OutputSetup oldOut;
        old.keys.items.clear();
        const project::LoadResult olr = project::parse(untouched, proj, old, oldOut);
        CHECK(olr.ok && olr.warnings.empty() && old.keys == control::defaultProjectKeys());

        control::KeyBinding cell;
        cell.action = "clip.trigger";
        cell.chord = {control::Key::J, control::ModNone};
        cell.args.deck = 0;
        cell.args.layer = 2;
        cell.args.column = 4;
        control::KeyBinding power;
        power.action = "layer.power";
        power.chord = {control::Key::K, control::ModCtrl};
        power.args.layer = 1;
        control::KeyBinding black;
        black.action = "master.blackout";
        black.chord = {control::Key::F, control::ModNone};
        black.mode = control::Mode::Hold;
        black.global = true;
        kc.keys.items.erase(kc.keys.items.begin(), kc.keys.items.begin() + 10);
        kc.keys.items.push_back(cell);
        kc.keys.items.push_back(power);
        kc.keys.items.push_back(black);
        const std::string kt = project::serialize(kc, kOut, proj);
        CHECK(kt.find("\"keys\"") != std::string::npos && kt.find("\"layer.power\"") != std::string::npos);
        Composition kb;
        OutputSetup kbOut;
        const project::LoadResult klr = project::parse(kt, proj, kb, kbOut);
        CHECK(klr.ok && klr.warnings.empty());
        control::KeyBindingSet migrated = kc.keys;
        CHECK(control::stampLayerIds(migrated, kc) == 2);
        CHECK(kb.keys == migrated && !(kb.keys == kc.keys));
        CHECK(kb.keys.items.size() == kc.keys.items.size());
        for (const control::KeyBinding& b : kb.keys.items) {
            if (b.action == "layer.power")
                CHECK(b.args.layerId == kc.layers[1].id);
            if (b.action == "clip.trigger" && !b.args.selected)
                CHECK(b.args.layerId == kc.layers[2].id);
        }
        const control::KeyBinding& last = kb.keys.items.back();
        CHECK(last.action == "master.blackout" && last.mode == control::Mode::Hold && last.global
              && last.chord.key == control::Key::F);
        const std::string kt2 = project::serialize(kb, kbOut, proj);
        CHECK(kt2.find("\"layerId\"") != std::string::npos && kt2 != kt);
        Composition kb2;
        OutputSetup kb2Out;
        CHECK(project::parse(kt2, proj, kb2, kb2Out).ok && kb2.keys == kb.keys);
        CHECK(project::serialize(kb2, kb2Out, proj) == kt2);

        kc.keys.items.clear();
        const std::string et = project::serialize(kc, kOut, proj);
        Composition empty;
        OutputSetup emptyOut;
        CHECK(project::parse(et, proj, empty, emptyOut).ok && empty.keys.items.empty());

        using OJson = nlohmann::ordered_json;
        OJson j = OJson::parse(kt);
        j["keys"].push_back({{"action", "acao.futura"}, {"key", "Q"}});
        j["keys"].push_back({{"action", "clip.trigger"}, {"key", "Ctrl+Nada"}});
        j["keys"].push_back(7);
        Composition odd;
        OutputSetup oddOut;
        const project::LoadResult odr = project::parse(j.dump(), proj, odd, oddOut);
        CHECK(odr.ok && odr.warnings.size() == 2 && odd.keys == kb.keys);
        j["keys"] = "nao e lista";
        Composition wrong;
        OutputSetup wrongOut;
        CHECK(project::parse(j.dump(), proj, wrong, wrongOut).ok && wrong.keys == control::defaultProjectKeys());
    }

    {
        section("Transform da camada e do clip: ida e volta (aditivo)");
        Composition tc = Composition::createDefault(3, 4);
        const OutputSetup tOut = buildOutput(tc.width, tc.height);
        const std::string plain = project::serialize(tc, tOut, proj);
        CHECK(plain.find("\"transform\"") == std::string::npos && plain.find("ownTransform") == std::string::npos
              && plain.find("applyLayerTransform") == std::string::npos);

        tc.layers[0].transform.w = static_cast<float>(tc.width);
        tc.layers[0].transform.h = static_cast<float>(tc.height);
        CHECK(project::serialize(tc, tOut, proj) == plain);
        tc.layers[0].transform = LayerTransform{};

        tc.layers[1].transform = LayerTransform::rect(480, 270, 960, 540);
        tc.layers[1].transform.rotation = 15.5f;
        tc.layers[1].transform.anchorX = 0.25f;
        tc.layers[1].transform.anchorY = 0.75f;
        tc.layers[1].transform.cropL = 10;
        tc.layers[1].transform.cropB = 20;
        tc.layers[2].transform = gridCellTransform(2, 2, 1, 1, tc.width, tc.height);
        Clip clip = Clip::generator("gen:test-pattern");
        if (clip.empty())
            clip = Clip::testPattern();
        clip.ownTransform = true;
        clip.transform = LayerTransform::rect(0, 0, 640, 360);
        tc.setClip(2, 1, std::move(clip));
        OutputSetup linked = tOut;
        linked.screens[0].slices[0].source = kSliceFromLayer;
        linked.screens[0].slices[0].layerId = tc.layers[1].id;
        linked.screens[0].slices[0].applyLayerTransform = true;
        linked.screens[0].slices[1].source = kSliceFromLayer;
        linked.screens[0].slices[1].layerId = tc.layers[2].id;

        const std::string tt = project::serialize(tc, linked, proj);
        CHECK(tt.find("\"transform\"") != std::string::npos && tt.find("\"ownTransform\": true") != std::string::npos);
        CHECK(tt.find("applyLayerTransform") != std::string::npos);
        Composition tb;
        OutputSetup tbOut;
        const project::LoadResult tr = project::parse(tt, proj, tb, tbOut);
        CHECK(tr.ok && tr.warnings.empty());
        CHECK(tb.layers[0].transform.isDefault());
        CHECK(tb.layers[1].transform == tc.layers[1].transform);
        CHECK(tb.layers[2].transform == tc.layers[2].transform);
        CHECK(tb.clip(2, 1).ownTransform && tb.clip(2, 1).transform == LayerTransform::rect(0, 0, 640, 360));
        CHECK(tbOut.screens[0].slices[0].applyLayerTransform && !tbOut.screens[0].slices[1].applyLayerTransform);
        CHECK(project::serialize(tb, tbOut, proj) == tt);

        Composition legacy;
        OutputSetup legacyOut;
        CHECK(project::parse(plain, proj, legacy, legacyOut).ok);
        bool allDefault = true;
        for (const Layer& l : legacy.layers)
            allDefault = allDefault && l.transform.isDefault();
        CHECK(allDefault && !legacyOut.screens[0].slices[0].applyLayerTransform);

        using OJson = nlohmann::ordered_json;
        OJson j = OJson::parse(tt);
        j["layers"][1]["transform"]["crop"] = OJson::array({999999, 0, 999999, 0});
        j["layers"][1]["transform"]["anchor"] = OJson::array({7, -3});
        Composition wild;
        OutputSetup wildOut;
        CHECK(project::parse(j.dump(), proj, wild, wildOut).ok);
        const LayerTransform& wt = wild.layers[1].transform;
        CHECK(wt.anchorX == 1.0f && wt.anchorY == 0.0f && wt.cropL + wt.cropR <= wt.width(wild.width) - 1.0f);
        j["layers"][1]["transform"] = "texto";
        Composition bad;
        OutputSetup badOut;
        CHECK(project::parse(j.dump(), proj, bad, badOut).ok && bad.layers[1].transform.isDefault());
    }


    fs::remove_all(root, ec);
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "PROJECTTEST OK\n" : "PROJECTTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
