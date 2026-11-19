#include "app/Paths.h"
#include "app/Settings.h"
#include "legal/Notices.h"
#include "ui/LayoutPreset.h"
#include "ui/ViewState.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <windows.h>

namespace fs = std::filesystem;
using namespace bsl::ui::layout;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);    \
        }                                                                 \
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

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

std::vector<unsigned> hexAfterAll(const std::string& text, const std::string& key)
{
    std::vector<unsigned> out;
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
        pos += key.size();
        out.push_back(static_cast<unsigned>(std::strtoul(text.substr(pos, 8).c_str(), nullptr, 16)));
    }
    return out;
}

bool noDuplicates(const std::vector<unsigned>& v)
{
    return std::set<unsigned>(v.begin(), v.end()).size() == v.size();
}

const char* kIniA = R"([Window][WindowOverViewport_11111111]
Pos=0,22
Size=1600,857
Collapsed=0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Saída###cmp_saida]
Pos=0,22
Size=480,428
Collapsed=0
DockId=0x00000012,0
LastUsed=20261028

[Window][Prévia###cmp_previa]
Pos=0,452
Size=480,427
Collapsed=0
DockId=0x00000013,0
LastUsed=20261028

[Window][Composição###cmp_grade]
Pos=482,22
Size=1118,857
Collapsed=0
DockId=0x00000014,0
LastUsed=20261028

[Window][Lista]
Pos=0,22
Size=272,857
Collapsed=0
DockId=0x00000009,0

[Window][Entrada]
Pos=274,22
Size=1326,857
Collapsed=0
DockId=0x0000000A,0

[Window][Audio]
Pos=300,200
Size=420,380
Collapsed=0

[Table][0xABCDEF01,3]
RefScale=16
Column 0  Weight=1.0000

[Docking][Data]
DockSpace         ID=0x42534C43 Window=0x5F4A2F10 Pos=0,22 Size=1600,857 Split=X
  DockNode        ID=0x00000011 Parent=0x42534C43 SizeRef=480,857 Split=Y
    DockNode      ID=0x00000012 Parent=0x00000011 SizeRef=480,428 Selected=0xAAAA0001
    DockNode      ID=0x00000013 Parent=0x00000011 SizeRef=480,427 Selected=0xAAAA0002
  DockNode        ID=0x00000014 Parent=0x42534C43 SizeRef=1118,857 Selected=0xAAAA0003
DockSpace         ID=0x42534C4D Window=0x5F4A2F10 Pos=0,22 Size=1600,857 Split=X
  DockNode        ID=0x00000009 Parent=0x42534C4D SizeRef=272,857 Selected=0xBBBB0001
  DockNode        ID=0x0000000A Parent=0x42534C4D SizeRef=1326,857 Selected=0xBBBB0002
)";

const char* kIniB = R"([Window][Saída###cmp_saida]
Pos=0,20
Size=300,680
DockId=0x00000009,0

[Window][Prévia###cmp_previa]
Pos=302,20
Size=698,200
DockId=0x0000000A,0

[Window][Composição###cmp_grade]
Pos=302,222
Size=698,478
DockId=0x0000000B,0

[Docking][Data]
DockSpace         ID=0x42534C43 Window=0x5F4A2F10 Pos=0,20 Size=1000,680 Split=X
  DockNode        ID=0x00000009 Parent=0x42534C43 SizeRef=300,680 Selected=0xAAAA0001
  DockNode        ID=0x0000000C Parent=0x42534C43 SizeRef=700,680 Split=Y
    DockNode      ID=0x0000000A Parent=0x0000000C SizeRef=700,200 Selected=0xAAAA0002
    DockNode      ID=0x0000000B Parent=0x0000000C SizeRef=700,480 Selected=0xAAAA0003
)";

LayoutPreset sample()
{
    LayoutPreset p;
    p.name = "Palco Principal – saída grande";
    p.created = 1700000123;
    p.refW = 1600;
    p.refH = 857;
    p.dock = extractDock(kIniA);
    p.builtin = "bigOutput";
    p.preview.source = "layer";
    p.preview.checker = false;
    p.preview.fit = "fill";
    p.hasUiScale = true;
    p.uiMode = "fixed";
    p.uiScale = 1.25f;
    return p;
}

void testJson()
{
    section("json: serializar e ler");
    const LayoutPreset p = sample();
    const std::string text = toJson(p);
    LayoutPreset q;
    std::string error;
    CHECK(fromJson(text, q, &error));
    CHECK(q.name == p.name);
    CHECK(q.created == p.created);
    CHECK(q.refW == 1600 && q.refH == 857);
    CHECK(q.dock == p.dock);
    CHECK(q.builtin == "bigOutput");
    CHECK(q.preview == p.preview);
    CHECK(q.hasUiScale && q.uiMode == "fixed" && q.uiScale == 1.25f);
    CHECK(toJson(q) == text);

    LayoutPreset noScale = p;
    noScale.hasUiScale = false;
    LayoutPreset r;
    CHECK(fromJson(toJson(noScale), r));
    CHECK(!r.hasUiScale);

    section("json: campos opcionais ausentes ou com tipo errado viram o padrao");
    LayoutPreset d;
    CHECK(fromJson(R"({"name":"So nome"})", d));
    CHECK(d.name == "So nome" && d.dock.empty() && d.preview == PreviewPrefs{} && !d.hasUiScale);
    CHECK(fromJson(R"({"name":"X","preview":{"source":7,"checker":"sim","fit":null},"refSize":["a",2],"created":"ontem"})", d));
    CHECK(d.preview == PreviewPrefs{} && d.refW == 0 && d.created == 0);
    CHECK(fromJson(R"({"name":"X","preview":{"source":"qualquer","fit":"outro"},"extra":{"a":1}})", d));
    CHECK(d.preview.source == "clip" && d.preview.fit == "fit");
    CHECK(fromJson(R"({"name":"X","uiScale":{"mode":"fixed","value":99}})", d));
    CHECK(d.hasUiScale && d.uiScale <= 3.0f);

    section("json: invalido nao quebra");
    const std::vector<std::string> bad = {
        "",
        "   ",
        "{",
        "[]",
        "null",
        "42",
        "\"texto\"",
        "{}",
        R"({"name":5})",
        R"({"name":""})",
        R"({"name":"   "})",
        R"({"name":"\u0001\u0002"})",
        R"({"format":"outra-coisa","name":"X"})",
        R"({"format":7,"name":"X"})",
        R"({"version":99,"name":"X"})",
        R"({"version":"1","name":"X"})",
        R"({"name":"X","dock":123})",
        R"({"name":"X","dock":["a"]})",
        std::string("{\"name\":\"X\",\"dock\":\"") + std::string(300 * 1024, 'a') + "\"}",
        std::string("\xFF\xFE\x00\x01 lixo binario", 20),
        R"({"name":"X")",
    };
    for (size_t i = 0; i < bad.size(); ++i) {
        LayoutPreset out;
        std::string err;
        const bool ok = fromJson(bad[i], out, &err);
        if (ok)
            std::printf("  aceitou o invalido #%zu\n", i);
        CHECK(!ok);
        CHECK(!ok && !err.empty());
    }

    section("nome: limpeza");
    CHECK(sanitizeName("  Meu layout \t") == "Meu layout");
    CHECK(sanitizeName("a\nb\tc") == "abc");
    CHECK(sanitizeName("   ").empty());
    CHECK(sanitizeName(std::string(200, 'x')).size() == 64);
    {
        std::string acentos;
        for (int i = 0; i < 40; ++i)
            acentos += "ã";
        const std::string cut = sanitizeName(acentos);
        CHECK(cut.size() <= 64 && cut.size() % 2 == 0);
    }
}

const char* kIniFloating = R"([Window][Saída###cmp_saida]
Pos=40,60
Size=500,420
Collapsed=0
ViewportId=0xFC9C3A43
ViewportPos=2300,150

[Window][Prévia###cmp_previa]
Pos=0,452
Size=480,427
Collapsed=0
DockId=0x00000013,0

[Window][Composição###cmp_grade]
Pos=482,22
Size=1118,857
Collapsed=0
DockId=0x00000014,0

[Docking][Data]
DockSpace         ID=0x42534C43 Window=0x5F4A2F10 Pos=0,22 Size=1600,857 Split=X
  DockNode        ID=0x00000013 Parent=0x42534C43 SizeRef=480,857 Selected=0xAAAA0002
  DockNode        ID=0x00000014 Parent=0x42534C43 SizeRef=1118,857 Selected=0xAAAA0003
)";

void testView(const fs::path& root)
{
    using namespace bsl::ui::view;
    section("exibir: tabela de paineis");
    {
        std::set<std::string> keys, windows;
        bool ok = true;
        for (const PanelDef& d : kPanels) {
            ok = ok && keys.insert(d.key).second && windows.insert(d.window).second && findPanel(d.key) == &d
                 && findPanelByWindow(d.window) == &d && panelIndex(d.key) >= 0;
        }
        CHECK(ok);
        CHECK(findPanel("nao-existe") == nullptr && findPanelByWindow("Qualquer") == nullptr && panelIndex("x") == -1);
        CHECK(findPanel("saida") && findPanel("saida")->mode == PanelMode::Composition);
        CHECK(findPanel("map-saida") && findPanel("map-saida")->mode == PanelMode::Mapping);
        CHECK(findPanel("status") && findPanel("status")->mode == PanelMode::Any);
        int sequencias = 0;
        for (const PanelDef& d : kPanels)
            if (d.mode == PanelMode::Sequences)
                ++sequencias;
        CHECK(sequencias == 5);
        for (const char* chave : {"seq-lista", "seq-canvas", "seq-passos", "seq-gerador", "seq-envelopes"})
            CHECK(findPanel(chave) && findPanel(chave)->mode == PanelMode::Sequences);
    }

    section("exibir: paineis do modo Sequencias");
    {
        State s;
        CHECK(s.setHidden("seq-gerador", true) && s.setHidden("seq-envelopes", true) && s.setHidden("map-lista", true));
        CHECK(s.isHidden("seq-gerador") && !s.isHidden("seq-canvas"));
        CHECK(s.showAll(PanelMode::Sequences));
        CHECK(!s.isHidden("seq-gerador") && !s.isHidden("seq-envelopes") && s.isHidden("map-lista"));
        CHECK(!s.showAll(PanelMode::Sequences));
        CHECK(fromJson(toJson(s)) == s);
        s.setHidden("seq-passos", true);
        CHECK(fromJson(toJson(s)).isHidden("seq-passos"));
        CHECK((sanitizeKeys({"seq-canvas", "seq-xx", "seq-canvas"}) == std::vector<std::string>{"seq-canvas"}));
    }

    section("exibir: ocultar, soltar e restaurar (State)");
    State st;
    CHECK(!st.isHidden("previa") && st.hidden.empty());
    CHECK(st.setHidden("previa", true) && st.isHidden("previa"));
    CHECK(!st.setHidden("previa", true));
    CHECK(!st.setHidden("painel-que-nao-existe", true));
    CHECK(st.setHidden("efeitos", true) && st.setHidden("map-lista", true));
    CHECK(st.setDetached("saida", true) && st.isDetached("saida") && !st.setDetached("saida", true));
    CHECK(st.setDockId("saida", 0x11) && !st.setDockId("saida", 0x11) && !st.setDockId("saida", 0) && !st.setDockId("x", 5));
    st.locked = true;
    CHECK(st.setHidden("status", true));
    CHECK(st.showAll(PanelMode::Composition));
    CHECK(!st.isHidden("previa") && !st.isHidden("efeitos") && !st.isHidden("status") && st.isHidden("map-lista"));
    CHECK(!st.showAll(PanelMode::Composition));
    st.setHidden("previa", true);

    section("exibir: settings.json (ida e volta, entrada invalida)");
    {
        const nlohmann::json j = toJson(st);
        const State back = fromJson(nlohmann::json::parse(j.dump()));
        CHECK(back == st);
        CHECK(back.hidden.size() == 2 && back.isHidden("previa") && back.isHidden("map-lista"));
        CHECK(back.detached == std::vector<std::string>{"saida"} && back.locked && back.dockIds.at("saida") == 0x11);
        CHECK(fromJson(nlohmann::json()) == State{});
        CHECK(fromJson(nlohmann::json::parse("[1,2]")) == State{});
        CHECK(fromJson(nlohmann::json::parse(R"({"hidden":"previa","detached":5,"locked":"sim","dockIds":[1]})")) == State{});
        const State dirty = fromJson(nlohmann::json::parse(
            R"({"hidden":["previa","previa","nao-existe",7,null,"clip"],"detached":["saida","saida"],"locked":true,
                "dockIds":{"saida":17,"x":3,"clip":-1,"previa":"a","efeitos":0,"fontes":99999999999}})"));
        CHECK((dirty.hidden == std::vector<std::string>{"previa", "clip"}));
        CHECK(dirty.detached == std::vector<std::string>{"saida"} && dirty.locked);
        CHECK(dirty.dockIds.size() == 1 && dirty.dockIds.at("saida") == 17);
        CHECK((sanitizeKeys({"clip", "zzz", "clip", "status"}) == std::vector<std::string>{"clip", "status"}));
    }
    {
        const fs::path file = root / "view-settings.json";
        nlohmann::json out;
        out["view"] = toJson(st);
        writeAll(file, out.dump(2));
        const State loaded = fromJson(nlohmann::json::parse(readAll(file))["view"]);
        CHECK(loaded == st);
    }

    section("exibir: preset guarda os paineis ocultos");
    {
        LayoutPreset p = sample();
        p.hidden = {"previa", "efeitos"};
        LayoutPreset q;
        CHECK(fromJson(toJson(p), q));
        CHECK(q.hidden == p.hidden);
        CHECK(toJson(q) == toJson(p));
        CHECK(fromJson(R"({"name":"Antigo","dock":""})", q) && q.hidden.empty());
        CHECK(fromJson(R"({"name":"X","hidden":["clip","clip","zzz",3]})", q));
        CHECK(q.hidden == std::vector<std::string>{"clip"});
        CHECK(fromJson(R"({"name":"X","hidden":"clip"})", q) && q.hidden.empty());
        PresetStore store(root / "exibir-presets");
        LayoutPreset a = sample();
        a.name = "Sem Prévia";
        a.hidden = {"previa"};
        CHECK(store.save(a));
        PresetStore again(root / "exibir-presets");
        again.reload();
        CHECK(again.count() == 1 && again.entries()[0].preset.hidden == std::vector<std::string>{"previa"});
        CHECK(again.remove(0) && again.count() == 0);
    }

    section("exibir: janela solta no preset e no imgui.ini");
    {
        const std::string dock = extractDock(kIniFloating);
        CHECK(contains(dock, "cmp_saida") && contains(dock, "ViewportPos=2300,150") && contains(dock, "ViewportId=0xFC9C3A43"));
        CHECK(hasDockRoot(dock, kCompDockRoot));
        const std::string merged = applyDock(kIniA, dock, 0.5, 0.5);
        const size_t at = merged.find("[Window][Saída###cmp_saida]");
        CHECK(at != std::string::npos);
        const std::string section1 = merged.substr(at, merged.find("\n\n", at) - at);
        CHECK(contains(section1, "ViewportPos=2300,150"));
        CHECK(contains(section1, "Size=250,210"));
        CHECK(!contains(section1, "DockId="));
        const std::string docked = applyDock(merged, extractDock(kIniA), 1.0, 1.0);
        const size_t at2 = docked.find("[Window][Saída###cmp_saida]");
        CHECK(at2 != std::string::npos && contains(docked.substr(at2, docked.find("\n\n", at2) - at2), "DockId=0x"));
    }
}

void testIni()
{
    section("ini: extrair so paineis e dockspaces");
    const std::string dock = extractDock(kIniA);
    CHECK(contains(dock, "[Window][Saída###cmp_saida]"));
    CHECK(contains(dock, "[Window][Lista]"));
    CHECK(contains(dock, "[Window][Entrada]"));
    CHECK(!contains(dock, "Debug##Default"));
    CHECK(!contains(dock, "WindowOverViewport"));
    CHECK(!contains(dock, "[Window][Audio]"));
    CHECK(!contains(dock, "[Table]"));
    CHECK(!contains(dock, "LastUsed"));
    CHECK(hasDockRoot(dock, kCompDockRoot));
    CHECK(hasDockRoot(dock, kMapDockRoot));
    CHECK(!hasDockRoot("", kCompDockRoot));
    CHECK(extractDock("").empty());
    CHECK(extractDock("lixo sem secao\n\n\n").empty());

    section("ini: aplicar na mesma janela troca so a Composicao");
    const std::string presetB = extractDock(kIniB);
    CHECK(hasDockRoot(presetB, kCompDockRoot) && !hasDockRoot(presetB, kMapDockRoot));
    const std::string merged = applyDock(kIniA, presetB, 1.0, 1.0);
    CHECK(contains(merged, "[Window][Debug##Default]"));
    CHECK(contains(merged, "[Window][Audio]"));
    CHECK(contains(merged, "[Table][0xABCDEF01,3]"));
    CHECK(contains(merged, "[Window][WindowOverViewport_11111111]"));
    CHECK(contains(merged, "[Window][Lista]") && contains(merged, "[Window][Entrada]"));
    CHECK(hasDockRoot(extractDock(merged), kMapDockRoot));
    CHECK(contains(merged, "Selected=0xBBBB0001"));
    CHECK(contains(merged, "Size=300,680"));
    CHECK(contains(merged, "Selected=0xAAAA0002"));
    CHECK(!contains(merged, "SizeRef=1118,857"));
    const std::vector<unsigned> ids = hexAfterAll(merged, " ID=0x");
    CHECK(noDuplicates(ids));
    const std::set<unsigned> idSet(ids.begin(), ids.end());
    for (unsigned id : hexAfterAll(merged, "DockId=0x"))
        CHECK(idSet.count(id) == 1);
    for (unsigned id : hexAfterAll(merged, " Parent=0x"))
        CHECK(idSet.count(id) == 1);
    CHECK(contains(merged, "DockId=0x00000009,0"));
    const std::vector<unsigned> dockIds = hexAfterAll(merged, "DockId=0x");
    CHECK(dockIds.size() == 5);

    section("ini: idempotente");
    const std::string again = applyDock(merged, extractDock(merged), 1.0, 1.0);
    CHECK(noDuplicates(hexAfterAll(again, " ID=0x")));
    CHECK(hasDockRoot(extractDock(again), kCompDockRoot) && hasDockRoot(extractDock(again), kMapDockRoot));
    CHECK(hexAfterAll(again, " ID=0x").size() == hexAfterAll(merged, " ID=0x").size());
    CHECK(hexAfterAll(again, "DockId=0x").size() == hexAfterAll(merged, "DockId=0x").size());
    CHECK(contains(again, "Selected=0xBBBB0001") && contains(again, "Selected=0xAAAA0002"));

    section("ini: janela de outro tamanho mantem as proporcoes");
    const std::string scaled = applyDock(kIniA, presetB, 2.0, 0.5);
    CHECK(contains(scaled, "Size=2000,340"));
    CHECK(contains(scaled, "SizeRef=600,340"));
    CHECK(contains(scaled, "SizeRef=1400,340"));
    CHECK(contains(scaled, "SizeRef=1400,100"));
    CHECK(contains(scaled, "SizeRef=1400,240"));
    CHECK(contains(scaled, "Size=600,340"));
    CHECK(contains(scaled, "Pos=604,10"));
    CHECK(contains(scaled, "SizeRef=272,857") && contains(scaled, "SizeRef=1326,857"));
    {
        const LayoutPreset p = sample();
        const std::string half = applyDock(kIniB, p.dock, 1000.0 / p.refW, 680.0 / p.refH);
        CHECK(contains(half, "Size=1000,680"));
        CHECK(contains(half, "SizeRef=300,680"));
        const std::vector<unsigned> hids = hexAfterAll(half, " ID=0x");
        CHECK(noDuplicates(hids));
    }

    section("ini: preset que traz os dois modos");
    const std::string both = applyDock(kIniB, extractDock(kIniA), 1.0, 1.0);
    CHECK(hasDockRoot(extractDock(both), kCompDockRoot) && hasDockRoot(extractDock(both), kMapDockRoot));
    CHECK(noDuplicates(hexAfterAll(both, " ID=0x")));
    CHECK(contains(both, "Selected=0xBBBB0002") && contains(both, "Selected=0xAAAA0003"));

    section("ini: preset corrompido nao altera nem quebra");
    const std::vector<std::string> broken = {
        "",
        "lixo",
        "[Docking][Data]\n",
        "[Docking][Data]\nDockSpace ID=0x42534C43\n",
        "[Docking][Data]\nDockNode ID=0x00000001 Parent=0x00000002 SizeRef=1,1\n",
        "[Docking][Data]\nDockSpace ID=0x42534C43 Pos=0,0 Size=10,10\n  DockNode ID=0x00000005 Parent=0x00000099 SizeRef=1,1\n",
        "[Docking][Data]\nDockSpace ID=0x00001234 Pos=0,0 Size=10,10\n",
        "[Docking][Data]\nDockSpace ID=0x42534C43 Pos=0,0 Size=10,10\n  DockNode ID=0x42534C43 Parent=0x42534C43 SizeRef=1,1\n",
        "[Window][Saída###cmp_saida]\nDockId=0xZZZZ,0\nPos=a,b\nSize=,\n",
        std::string("\xFF\xFE\x00 binario", 10),
    };
    for (size_t i = 0; i < broken.size(); ++i) {
        const std::string out = applyDock(kIniA, broken[i], 1.0, 1.0);
        const std::vector<unsigned> oids = hexAfterAll(out, " ID=0x");
        const std::set<unsigned> oset(oids.begin(), oids.end());
        bool consistent = noDuplicates(oids);
        for (unsigned id : hexAfterAll(out, "DockId=0x"))
            consistent = consistent && oset.count(id) == 1;
        if (!consistent)
            std::printf("  ini inconsistente com o preset corrompido #%zu\n", i);
        CHECK(consistent);
        CHECK(hasDockRoot(extractDock(out), kCompDockRoot));
        CHECK(hasDockRoot(extractDock(out), kMapDockRoot));
    }
    CHECK(contains(applyDock(kIniA, presetB, 0.0, -3.0), "Size=1000,680"));
    CHECK(!applyDock(kIniA, presetB, 1e300, 1e300).empty());
    CHECK(hasDockRoot(applyDock("", presetB, 1.0, 1.0), kCompDockRoot));
}

LayoutPreset named(const std::string& name)
{
    LayoutPreset p = sample();
    p.name = name;
    return p;
}

void testStore(const fs::path& root)
{
    section("pasta: salvar e listar na ordem de criacao");
    const fs::path dir = root / "layouts";
    PresetStore st(dir);
    st.reload();
    CHECK(st.count() == 0);
    std::string error;
    CHECK(st.save(named("Zebra"), &error));
    CHECK(st.save(named("Alfa"), &error));
    CHECK(st.save(named("Meio dia"), &error));
    CHECK(st.count() == 3);
    CHECK(st.entries()[0].preset.name == "Zebra" && st.entries()[1].preset.name == "Alfa"
          && st.entries()[2].preset.name == "Meio dia");
    CHECK(!st.save(named("   "), &error) && !error.empty());

    PresetStore other(dir);
    other.reload();
    CHECK(other.count() == 3);
    CHECK(other.entries()[0].preset.name == "Zebra" && other.entries()[2].preset.name == "Meio dia");
    CHECK(other.entries()[0].preset.dock == sample().dock);
    CHECK(other.entries()[0].preset.preview == sample().preview);

    section("pasta: mesmo nome substitui sem mudar a posicao");
    LayoutPreset changed = named("alfa");
    changed.preview.checker = true;
    changed.preview.source = "clip";
    CHECK(st.save(changed, &error));
    CHECK(st.count() == 3);
    CHECK(st.find("ALFA") == 1);
    CHECK(st.entries()[1].preset.name == "Alfa");
    CHECK(st.entries()[1].preset.preview.source == "clip");

    section("pasta: nomes com caracteres de arquivo proibidos");
    CHECK(st.save(named("Palco: A/B?*"), &error));
    CHECK(st.count() == 4);
    CHECK(fs::exists(st.entries()[3].file));
    CHECK(st.entries()[3].file.parent_path() == dir);
    CHECK(st.save(named("Palco_ A_B__"), &error));
    CHECK(st.count() == 5);
    CHECK(st.entries()[3].file != st.entries()[4].file);

    section("pasta: renomear");
    CHECK(st.rename(0, "Zebra 2", &error));
    CHECK(st.entries()[0].preset.name == "Zebra 2");
    CHECK(!st.rename(0, "Alfa", &error));
    CHECK(!st.rename(0, "  ", &error));
    CHECK(!st.rename(99, "Qualquer", &error));
    CHECK(st.rename(0, "ZEBRA 2", &error));
    PresetStore afterRename(dir);
    afterRename.reload();
    CHECK(afterRename.find("zebra 2") == 0);

    section("pasta: exportar e importar .bsllayout");
    const fs::path out = root / ("Meu layout" + std::string(kFileExtension));
    CHECK(st.exportTo(1, out, &error));
    CHECK(fs::exists(out));
    LayoutPreset read;
    CHECK(readFile(out, read, &error));
    CHECK(read.name == "Alfa" && read.dock == st.entries()[1].preset.dock);
    std::string importedName;
    CHECK(st.importFrom(out, &error, &importedName));
    CHECK(importedName == "Alfa (2)");
    CHECK(st.count() == 6);
    CHECK(st.find("Alfa (2)") == 5);
    CHECK(st.importFrom(out, &error, &importedName) && importedName == "Alfa (3)");
    CHECK(!st.exportTo(99, out, &error));

    section("pasta: importar invalido");
    const fs::path badFile = root / "ruim.bsllayout";
    writeAll(badFile, "isto nao e json");
    const int before = st.count();
    CHECK(!st.importFrom(badFile, &error) && !error.empty());
    CHECK(!st.importFrom(root / "nao-existe.bsllayout", &error));
    writeAll(badFile, R"({"name":"","dock":"x"})");
    CHECK(!st.importFrom(badFile, &error));
    CHECK(!st.importFrom(root, &error));
    CHECK(st.count() == before);

    section("pasta: arquivo corrompido na pasta e ignorado");
    writeAll(dir / "quebrado.json", "{ nao fecha");
    writeAll(dir / "vazio.json", "");
    writeAll(dir / "estranho.json", R"({"format":"outro"})");
    writeAll(dir / "nao-eh-json.txt", "ignorado pela extensao");
    PresetStore dirty(dir);
    dirty.reload();
    CHECK(dirty.count() == before);
    CHECK(dirty.skipped() == 3);

    section("pasta: excluir");
    const fs::path file = st.entries()[2].file;
    CHECK(st.remove(2));
    CHECK(!fs::exists(file));
    CHECK(st.count() == before - 1);
    CHECK(!st.remove(99) && !st.remove(-1));
    CHECK(st.find("Meio dia") < 0);
    PresetStore afterDelete(dir);
    afterDelete.reload();
    CHECK(afterDelete.count() == before - 1);

    section("pasta: pasta inexistente");
    PresetStore none(root / "nao-existe");
    none.reload();
    CHECK(none.count() == 0);
    CHECK(none.save(named("Novo"), &error));
    CHECK(fs::exists(root / "nao-existe"));
}

bool validUtf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 99;
        if (extra == 99 || i + extra >= s.size() + (extra == 0 ? 1 : 0))
            return false;
        for (size_t k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
                return false;
        i += extra + 1;
    }
    return true;
}

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void testLegalParser()
{
    section("avisos: parser de THIRD-PARTY-NOTICES.md");
    const std::string md =
        "# Titulo\n\n## Grupo\n\n### Alfa\n- **Versão:** 1.2\n- **Licença:** MIT\n- **Copyright:** (c) Fulano\n- **URL:** https://a.example/\n"
        "- **Texto da licença:** `licenses/a.txt`, `licenses/b.txt`\n- **Uso:** faz coisas\n\n### Beta\n- **Versão:** 3\n"
        "- **Licença:** `ISC`\n- **Situação:** PENDENTE: falta o texto\n\n## Pendências\n- **Versão:** nao e de ninguem\n";
    const auto list = bsl::legal::parseThirdParty(md);
    CHECK(list.size() == 2);
    if (list.size() == 2) {
        CHECK(list[0].name == "Alfa" && list[0].version == "1.2" && list[0].license == "MIT");
        CHECK(list[0].copyright == "(c) Fulano" && list[0].url == "https://a.example/" && list[0].usage == "faz coisas");
        CHECK(list[0].licenseFiles.size() == 2 && list[0].licenseFiles[1] == "licenses/b.txt");
        CHECK(list[0].status.empty());
        CHECK(list[1].name == "Beta" && list[1].license == "ISC" && list[1].licenseFiles.empty());
        CHECK(list[1].status.rfind("PENDENTE", 0) == 0);
    }
    CHECK(bsl::legal::parseThirdParty("").empty());
    CHECK(bsl::legal::parseThirdParty("sem secoes\n- **Versão:** 1\n").empty());

    section("avisos: caminho de licenca seguro");
    CHECK(bsl::legal::safeLicensePath("licenses/GLFW-zlib.txt"));
    CHECK(!bsl::legal::safeLicensePath("licenses/"));
    CHECK(!bsl::legal::safeLicensePath("../LICENSE"));
    CHECK(!bsl::legal::safeLicensePath("licenses/../LICENSE"));
    CHECK(!bsl::legal::safeLicensePath("licenses\\x.txt"));
    CHECK(!bsl::legal::safeLicensePath("C:/Windows/win.ini"));
    CHECK(!bsl::legal::safeLicensePath("/etc/passwd"));
    CHECK(!bsl::legal::safeLicensePath(""));

    section("avisos: quebra de linha dos textos");
    {
        const auto lines = bsl::legal::wrapText("um dois tres quatro cinco seis sete oito nove dez", 20);
        CHECK(lines.size() >= 3);
        bool within = true;
        std::string joined;
        for (const std::string& l : lines) {
            within = within && l.size() <= 20;
            joined += (joined.empty() ? "" : " ") + l;
        }
        CHECK(within);
        CHECK(joined == "um dois tres quatro cinco seis sete oito nove dez");
        const auto blank = bsl::legal::wrapText("a\n\nb\r\n\tc", 40);
        CHECK(blank.size() == 4 && blank[1].empty() && blank[2] == "b" && blank[3] == "    c");
        const auto indented = bsl::legal::wrapText("    aaa bbb ccc ddd eee fff", 14);
        CHECK(indented.size() >= 2 && indented[1].rfind("    ", 0) == 0);
        const auto longWord = bsl::legal::wrapText(std::string(50, 'x'), 16);
        CHECK(longWord.size() == 4 && longWord[0].size() == 16 && longWord[3].size() == 2);
        std::string acc;
        for (int i = 0; i < 40; ++i)
            acc += "ação ";
        bool ok = true;
        for (const std::string& l : bsl::legal::wrapText(acc, 13))
            ok = ok && validUtf8(l);
        CHECK(ok);
        std::string accWord;
        for (int i = 0; i < 30; ++i)
            accWord += "ç";
        ok = true;
        for (const std::string& l : bsl::legal::wrapText(accWord, 9))
            ok = ok && validUtf8(l) && l.size() <= 18;
        CHECK(ok);
        CHECK(bsl::legal::wrapText("", 40).size() == 1);
    }
}

void testLegalFiles()
{
    const fs::path src = BSL_SOURCE_DIR;
    section("avisos: documentos existem e nao estao vazios");
    const std::string cmake = readAll(src / "CMakeLists.txt");
    const std::string notices = readAll(src / "THIRD-PARTY-NOTICES.md");
    const std::string privacy = readAll(src / "PRIVACIDADE.md");
    const std::string terms = readAll(src / "TERMOS-DE-USO.md");
    const std::string gpl = readAll(src / "LICENSE");
    CHECK(!cmake.empty() && !notices.empty() && !privacy.empty() && !terms.empty());
    CHECK(gpl.size() > 30000 && contains(gpl, "GNU GENERAL PUBLIC LICENSE") && contains(gpl, "Version 3"));
    CHECK(bsl::legal::findDocsRoot(src / "docs") == src);
    CHECK(bsl::legal::findDocsRoot(src) == src);
    CHECK(bsl::legal::findDocsRoot(fs::path()).empty());
    for (const std::string* doc : {&notices, &privacy, &terms})
        CHECK(!contains(*doc, "\xEF\xBF\xBD") && validUtf8(*doc));

    section("avisos: toda biblioteca do CMake aparece em THIRD-PARTY-NOTICES.md");
    const std::map<std::string, std::string> shown = {{"glfw", "GLFW"}, {"imgui", "Dear ImGui"}, {"nlohmann_json", "nlohmann/json"},
                                                      {"stb", "stb"}, {"spout2", "Spout"}, {"monocypher", "Monocypher"},
                                                      {"nfd", "Native File Dialog Extended"}, {"miniaudio", "miniaudio"}};
    std::set<std::string> declared;
    const std::regex declareRe(R"(FetchContent_Declare\(\s*(\w+))");
    for (std::sregex_iterator it(cmake.begin(), cmake.end(), declareRe), end; it != end; ++it)
        declared.insert((*it)[1]);
    CHECK(declared.size() >= 5);
    for (const std::string& name : declared) {
        const auto s = shown.find(name);
        CHECK(s != shown.end());
        if (s != shown.end())
            CHECK(contains(notices, "### " + s->second));
    }
    std::vector<std::string> versions;
    const std::regex gitTagRe(R"(GIT_TAG\s+v?([0-9][0-9A-Za-z.]*))");
    for (std::sregex_iterator it(cmake.begin(), cmake.end(), gitTagRe), end; it != end; ++it)
        versions.push_back((*it)[1]);
    for (const char* var : {"BSL_FFMPEG_VERSION", "BSL_PDFIUM_TAG", "BSL_ORT_VERSION", "BSL_DML_VERSION"}) {
        std::smatch m;
        const std::string pattern = std::string("set\\(") + var + R"re(\s+"([^"]+)")re";
        CHECK(std::regex_search(cmake, m, std::regex(pattern)));
        if (!m.empty())
            versions.push_back(m[1]);
    }
    {
        std::smatch m;
        CHECK(std::regex_search(cmake, m, std::regex(R"(stb[^)]*archive/([0-9a-f]{40})\.zip)")));
        if (!m.empty())
            versions.push_back(m[1]);
        for (const char* var : {"_hf_depth", "_hf_modnet"}) {
            std::smatch r;
            CHECK(std::regex_search(cmake, r, std::regex(std::string("set\\(") + var + R"(\s+"[^"]*/resolve/([0-9a-f]{40})/)")));
            if (!r.empty())
                versions.push_back(r[1].str().substr(0, 7));
        }
    }
    CHECK(versions.size() >= 11);
    for (const std::string& v : versions)
        CHECK(contains(notices, v));
    for (const char* name : {"FFmpeg", "PDFium", "ONNX Runtime", "DirectML", "Chromium Embedded Framework", "Depth Anything V2 Small",
                             "MODNet", "Barlow", "JetBrains Mono", "Lucide", "NDI", "glad", "FFGL", "dav1d", "libvpx"})
        CHECK(contains(notices, name));

    section("avisos: entradas da lista de terceiros");
    const auto list = bsl::legal::parseThirdParty(notices);
    CHECK(list.size() >= 25);
    std::set<std::string> cited;
    for (const auto& e : list) {
        CHECK(!e.name.empty() && !e.version.empty() && !e.license.empty() && !e.copyright.empty() && !e.url.empty());
        CHECK(e.url.rfind("http", 0) == 0);
        if (contains(e.copyright, "PENDENTE") || contains(e.license, "PENDENTE"))
            CHECK(!e.status.empty());
        for (const std::string& rel : e.licenseFiles) {
            CHECK(bsl::legal::safeLicensePath(rel));
            const fs::path f = src / fs::path(std::u8string(rel.begin(), rel.end()));
            std::error_code ec;
            const bool ok = fs::is_regular_file(f, ec) && fs::file_size(f, ec) > 0;
            if (!ok)
                std::printf("  licenca ausente ou vazia: %s (entrada %s)\n", rel.c_str(), e.name.c_str());
            CHECK(ok);
            cited.insert(fs::path(rel).filename().string());
        }
    }
    CHECK(contains(notices, "## Pendências"));

    section("avisos: todo arquivo de licenses/ existe, tem conteudo e e citado");
    int files = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(src / "licenses", ec)) {
        if (!entry.is_regular_file())
            continue;
        ++files;
        const std::string name = entry.path().filename().string();
        const std::string text = readAll(entry.path());
        const bool ok = !text.empty() && text.find_first_not_of(" \t\r\n") != std::string::npos;
        if (!ok)
            std::printf("  arquivo de licenca vazio: %s\n", name.c_str());
        CHECK(ok);
        CHECK(!contains(text, "\xEF\xBF\xBD") && validUtf8(text));
        CHECK(cited.count(name) == 1 || contains(notices, name));
    }
    CHECK(files >= 40);

    section("avisos: conteudo minimo dos documentos");
    CHECK(contains(privacy, "127.0.0.1") && contains(privacy, "api.github.com") && contains(privacy, "autoCheck"));
    CHECK(contains(privacy, "Nenhuma telemetria") && contains(privacy, "offline"));
    CHECK(contains(privacy, "Receptor DMX") && contains(privacy, "desligado") && contains(privacy, "%APPDATA%\\BSL Stage"));
    CHECK(contains(privacy, "navegador") && contains(privacy, "revisa"));
    const std::string t = lower(terms);
    CHECK(contains(t, "aconselhamento jurídico") && contains(t, "revisar com advogado antes da versão 1.0"));
    CHECK(contains(terms, "Disclaimer of Warranty") && contains(terms, "Limitation of Liability"));
    CHECK(contains(terms, "H.264") && contains(terms, "HEVC") && contains(terms, "Vizrt NDI AB"));
    CHECK(contains(terms, "BSL Stage") && contains(terms, "não são cobertos pela GPL"));
}

void testLegacyNoticeKey(const fs::path& root)
{
    using bsl::Settings;
    section("settings: chave acceptedNoticeVersion de versoes antigas e ignorada");
    const fs::path dir = root / "settings-antigo";
    fs::create_directories(dir);
    bsl::paths::setAppDataOverride(dir);

    Settings s;
    s.windowWidth = 1234;
    s.save();
    nlohmann::json j = nlohmann::json::parse(readAll(dir / "settings.json"));
    CHECK(!j.contains("acceptedNoticeVersion"));
    j["acceptedNoticeVersion"] = 1;
    writeAll(dir / "settings.json", j.dump());
    CHECK(Settings::load().windowWidth == 1234);
    j["acceptedNoticeVersion"] = "abc";
    writeAll(dir / "settings.json", j.dump());
    CHECK(Settings::load().windowWidth == 1234);
    writeAll(dir / "settings.json", "{ nao fecha");
    CHECK(Settings::load().windowWidth == 1600);
    bsl::paths::setAppDataOverride(fs::path());
}

void testOutputCompat(const fs::path& root)
{
    using bsl::Settings;
    section("settings: modo compativel da saida");
    const fs::path dir = root / "settings-compat";
    fs::create_directories(dir);
    bsl::paths::setAppDataOverride(dir);

    Settings s;
    CHECK(s.outputCompat == "auto");
    s.save();
    CHECK(Settings::load().outputCompat == "auto");
    for (const char* v : {"on", "off", "auto"}) {
        s.outputCompat = v;
        s.save();
        CHECK(Settings::load().outputCompat == v);
    }
    nlohmann::json j = nlohmann::json::parse(readAll(dir / "settings.json"));
    j["outputCompat"] = "talvez";
    writeAll(dir / "settings.json", j.dump());
    CHECK(Settings::load().outputCompat == "auto");
    j["outputCompat"] = 7;
    writeAll(dir / "settings.json", j.dump());
    CHECK(Settings::load().outputCompat == "auto");
    bsl::paths::setAppDataOverride(fs::path());

    using bsl::outputCompatFor;
    CHECK(outputCompatFor("auto", "ATI Technologies Inc.", "AMD Radeon(TM) Graphics"));
    CHECK(outputCompatFor("auto", "ATI Technologies Inc.", "Unknown"));
    CHECK(outputCompatFor("auto", "Advanced Micro Devices, Inc.", "AMD Radeon Vega 8"));
    CHECK(outputCompatFor("auto", "", "radeon rx 580"));
    CHECK(!outputCompatFor("auto", "NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2"));
    CHECK(!outputCompatFor("auto", "Intel", "Intel(R) UHD Graphics 620"));
    CHECK(outputCompatFor("on", "NVIDIA Corporation", "NVIDIA GeForce RTX 3060"));
    CHECK(!outputCompatFor("off", "ATI Technologies Inc.", "AMD Radeon(TM) Graphics"));
}

}

int main()
{
    const fs::path root = fs::temp_directory_path() / ("bslstage_uitest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    testJson();
    testIni();
    testStore(root);
    testView(root);
    testLegalParser();
    testLegalFiles();
    testLegacyNoticeKey(root);
    testOutputCompat(root);

    fs::remove_all(root, ec);
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "UITEST OK\n" : "UITEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
