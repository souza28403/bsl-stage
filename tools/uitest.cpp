// bslstage_uitest: presets de layout da interface (sem janela, sem GL e sem ImGui).
// Uso: bslstage_uitest
//
// Cobre: serializacao e leitura do preset (.bsllayout), preset invalido sem crash, troca de trechos do imgui.ini
// (so os paineis e os dockspaces; IDs renumerados; proporcoes em outra janela) e a pasta de presets
// (salvar, listar, renomear, excluir, exportar, importar).

#include "ui/LayoutPreset.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// Numeros hexadecimais depois de "key" (ex.: " ID=0x", "DockId=0x") em todo o texto
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

// imgui.ini como o ImGui grava (janelas da Composicao com ### no nome e as do Mapeamento sem)
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

// Outro arranjo da Composicao (IDs de no diferentes, que colidem com os do Mapeamento do ini A) em uma janela 1000x700
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
    CHECK(toJson(q) == text); // estavel: salvar o que foi lido da o mesmo arquivo

    LayoutPreset noScale = p;
    noScale.hasUiScale = false;
    LayoutPreset r;
    CHECK(fromJson(toJson(noScale), r));
    CHECK(!r.hasUiScale); // escala da interface e opcional

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
        R"({"name":"X")", // cortado no meio
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
            acentos += "ã"; // 2 bytes por caractere: o corte nao pode partir um caractere
        const std::string cut = sanitizeName(acentos);
        CHECK(cut.size() <= 64 && cut.size() % 2 == 0);
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
    // O que nao e do preset continua
    CHECK(contains(merged, "[Window][Debug##Default]"));
    CHECK(contains(merged, "[Window][Audio]"));
    CHECK(contains(merged, "[Table][0xABCDEF01,3]"));
    CHECK(contains(merged, "[Window][WindowOverViewport_11111111]"));
    CHECK(contains(merged, "[Window][Lista]") && contains(merged, "[Window][Entrada]"));
    CHECK(hasDockRoot(extractDock(merged), kMapDockRoot)); // dockspace do Mapeamento intacto
    CHECK(contains(merged, "Selected=0xBBBB0001"));
    // A Composicao agora e a do preset (a Saida passou a ter 300 de largura)
    CHECK(contains(merged, "Size=300,680"));
    CHECK(contains(merged, "Selected=0xAAAA0002"));
    CHECK(!contains(merged, "SizeRef=1118,857")); // o no antigo da grade sumiu
    // IDs: sem repeticao, e todo DockId e todo Parent aponta para um no que existe
    const std::vector<unsigned> ids = hexAfterAll(merged, " ID=0x");
    CHECK(noDuplicates(ids));
    const std::set<unsigned> idSet(ids.begin(), ids.end());
    for (unsigned id : hexAfterAll(merged, "DockId=0x"))
        CHECK(idSet.count(id) == 1);
    for (unsigned id : hexAfterAll(merged, " Parent=0x"))
        CHECK(idSet.count(id) == 1);
    // As janelas do Mapeamento continuam nos nos do Mapeamento
    CHECK(contains(merged, "DockId=0x00000009,0")); // Lista no no 0x9 do Mapeamento, que nao foi renumerado
    // Cada janela da Composicao ficou num no da Composicao (o do preset), com a mesma aba
    const std::vector<unsigned> dockIds = hexAfterAll(merged, "DockId=0x");
    CHECK(dockIds.size() == 5); // 3 da Composicao + Lista + Entrada

    section("ini: idempotente");
    const std::string again = applyDock(merged, extractDock(merged), 1.0, 1.0);
    CHECK(noDuplicates(hexAfterAll(again, " ID=0x")));
    CHECK(hasDockRoot(extractDock(again), kCompDockRoot) && hasDockRoot(extractDock(again), kMapDockRoot));
    CHECK(hexAfterAll(again, " ID=0x").size() == hexAfterAll(merged, " ID=0x").size());
    CHECK(hexAfterAll(again, "DockId=0x").size() == hexAfterAll(merged, "DockId=0x").size());
    CHECK(contains(again, "Selected=0xBBBB0001") && contains(again, "Selected=0xAAAA0002"));

    section("ini: janela de outro tamanho mantem as proporcoes");
    const std::string scaled = applyDock(kIniA, presetB, 2.0, 0.5); // 1000x680 -> 2000x340
    CHECK(contains(scaled, "Size=2000,340"));    // raiz do dockspace
    CHECK(contains(scaled, "SizeRef=600,340"));  // 300x680
    CHECK(contains(scaled, "SizeRef=1400,340")); // 700x680
    CHECK(contains(scaled, "SizeRef=1400,100")); // 700x200
    CHECK(contains(scaled, "SizeRef=1400,240")); // 700x480
    CHECK(contains(scaled, "Size=600,340"));     // a janela da Saida
    CHECK(contains(scaled, "Pos=604,10"));       // a da Previa (302,20)
    // O Mapeamento nao e escalado (o preset nao o traz)
    CHECK(contains(scaled, "SizeRef=272,857") && contains(scaled, "SizeRef=1326,857"));
    // Proporcao da divisao: 300/1000 antes e depois
    {
        const LayoutPreset p = sample();
        const std::string half = applyDock(kIniB, p.dock, 1000.0 / p.refW, 680.0 / p.refH); // ini de 1600x857 numa de 1000x680
        CHECK(contains(half, "Size=1000,680"));    // raiz: 1600x857 -> 1000x680
        CHECK(contains(half, "SizeRef=300,680"));  // 480 de 1600 -> 300 de 1000: mesma fracao da largura
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
        "[Docking][Data]\nDockSpace ID=0x42534C43\n",                                   // sem Pos/Size
        "[Docking][Data]\nDockNode ID=0x00000001 Parent=0x00000002 SizeRef=1,1\n",       // raiz que nao e dockspace
        "[Docking][Data]\nDockSpace ID=0x42534C43 Pos=0,0 Size=10,10\n  DockNode ID=0x00000005 Parent=0x00000099 SizeRef=1,1\n", // pai inexistente
        "[Docking][Data]\nDockSpace ID=0x00001234 Pos=0,0 Size=10,10\n",                 // dockspace desconhecido
        "[Docking][Data]\nDockSpace ID=0x42534C43 Pos=0,0 Size=10,10\n  DockNode ID=0x42534C43 Parent=0x42534C43 SizeRef=1,1\n", // ID repetido
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
        CHECK(hasDockRoot(extractDock(out), kCompDockRoot)); // a Composicao do ini atual sobreviveu
        CHECK(hasDockRoot(extractDock(out), kMapDockRoot));
    }
    // Fatores absurdos viram 1
    CHECK(contains(applyDock(kIniA, presetB, 0.0, -3.0), "Size=1000,680"));
    CHECK(!applyDock(kIniA, presetB, 1e300, 1e300).empty());
    // Sem ini atual: o preset sozinho
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
          && st.entries()[2].preset.name == "Meio dia"); // ordem em que foram criados (Alt+1, Alt+2...)
    CHECK(!st.save(named("   "), &error) && !error.empty());

    PresetStore other(dir); // outra instancia (abrir o app de novo) enxerga os mesmos
    other.reload();
    CHECK(other.count() == 3);
    CHECK(other.entries()[0].preset.name == "Zebra" && other.entries()[2].preset.name == "Meio dia");
    CHECK(other.entries()[0].preset.dock == sample().dock);
    CHECK(other.entries()[0].preset.preview == sample().preview);

    section("pasta: mesmo nome substitui sem mudar a posicao");
    LayoutPreset changed = named("alfa"); // maiusculas diferentes
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
    CHECK(st.save(named("Palco_ A_B__"), &error)); // mesmo nome de arquivo depois da limpeza: sufixo
    CHECK(st.count() == 5);
    CHECK(st.entries()[3].file != st.entries()[4].file);

    section("pasta: renomear");
    CHECK(st.rename(0, "Zebra 2", &error));
    CHECK(st.entries()[0].preset.name == "Zebra 2");
    CHECK(!st.rename(0, "Alfa", &error)); // nome de outro preset
    CHECK(!st.rename(0, "  ", &error));
    CHECK(!st.rename(99, "Qualquer", &error));
    CHECK(st.rename(0, "ZEBRA 2", &error)); // so muda as maiusculas do proprio nome
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
    CHECK(st.importFrom(out, &error, &importedName)); // o nome ja existe
    CHECK(importedName == "Alfa (2)");
    CHECK(st.count() == 6);
    CHECK(st.find("Alfa (2)") == 5); // vai para o fim da lista
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
    CHECK(!st.importFrom(root, &error)); // uma pasta
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
    CHECK(none.save(named("Novo"), &error)); // cria a pasta
    CHECK(fs::exists(root / "nao-existe"));
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / ("bslstage_uitest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    testJson();
    testIni();
    testStore(root);

    fs::remove_all(root, ec);
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "UITEST OK\n" : "UITEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
