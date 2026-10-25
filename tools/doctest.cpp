// bslstage_doctest: PDF, Office e Web no processo auxiliar (bslstage_worker.exe), com janela oculta (GL 3.3).
// Cobre: PDF de 3 paginas (troca de pagina muda o quadro, avanco automatico por tempo e batidas, fade),
// transporte dos quadros (textura D3D11 compartilhada x memoria compartilhada), worker morto a forca (a fonte
// reinicia sozinha e mantem o ultimo quadro), falha definitiva depois de 3 reinicios, miniatura da pagina 1,
// docx e pptx convertidos pelo Microsoft Office (contagem de paginas, cache), Web (file:// vermelho opaco e
// fundo transparente com alpha 0, clique encaminhado).
// No fim confere que nao sobrou bslstage_worker.exe nem Office aberto por este teste.
//
// Uso: bslstage_doctest [pdf] [crash] [office] [web]        (sem argumentos roda tudo)
//      bslstage_doctest --make-demo-docs <pasta>             (gera sample.pdf/.pptx/.html para o --demo)

#include "media/DocInfo.h"
#include "media/DocThumb.h"
#include "media/PdfSource.h"
#include "media/RemoteSource.h"
#include "media/WebSource.h"
#include "media/WorkerHost.h"
#include "render/D3D11Interop.h"
#include "worker/ComAuto.h"
#include "worker/OfficeConverter.h"
#include "worker/WorkerUtil.h"

#include <windows.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace bsl;
using namespace bsl::media;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skipped = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);              \
        }                                                                           \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
    std::fflush(stdout);
}

void skip(const std::string& why)
{
    ++g_skipped;
    std::printf("  PULADO: %s\n", why.c_str());
}

double secondsSince(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Pixels
{
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    const unsigned char* at(int x, int y) const
    {
        return &rgba[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
    }
};

Pixels readTexture(GLuint tex, int w, int h)
{
    Pixels p;
    p.w = w;
    p.h = h;
    p.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return p;
}

Pixels readSource(const ISource& s)
{
    return readTexture(s.texture(), s.width(), s.height());
}

bool isColor(const unsigned char* px, int r, int g, int b, int tol = 40)
{
    return std::abs(px[0] - r) <= tol && std::abs(px[1] - g) <= tol && std::abs(px[2] - b) <= tol;
}

std::string pixelText(const unsigned char* px)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "(%d,%d,%d,%d)", px[0], px[1], px[2], px[3]);
    return buf;
}

// Chama update() ate 'pred' ser verdadeiro ou estourar o prazo
bool waitUntil(ISource& src, double seconds, const std::function<bool()>& pred, double dt = 0.016)
{
    const auto t0 = Clock::now();
    while (secondsSince(t0) < seconds) {
        src.update(dt);
        if (pred())
            return true;
        sleepMs(5);
    }
    src.update(dt);
    return pred();
}

fs::path tempDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, buf);
    fs::path dir = fs::path(std::wstring(buf, n)) / L"bslstage-doctest";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// ----------------------------------------------------------------------------- fixtures

// PDF minimo escrito a mao: uma pagina 16:9 por cor, cobrindo a pagina inteira (com 'labels', escreve um texto
// branco em Helvetica no canto; os testes usam paginas lisas para conferir o pixel central)
void writeColorPdf(const fs::path& file, const std::vector<std::string>& colors, const std::vector<std::string>& labels = {})
{
    std::vector<std::string> objects;
    const int n = static_cast<int>(colors.size());
    const int fontObj = 3 + n * 2;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (int i = 0; i < n; ++i)
        kids += std::to_string(3 + i * 2) + " 0 R ";
    objects.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(n) + " >>");
    for (int i = 0; i < n; ++i) {
        const int contentObj = 4 + i * 2;
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 225] /Contents " + std::to_string(contentObj)
                          + " 0 R /Resources << /Font << /F1 " + std::to_string(fontObj) + " 0 R >> >> >>");
        std::string stream = colors[static_cast<size_t>(i)] + " rg 0 0 400 225 re f";
        if (static_cast<size_t>(i) < labels.size() && !labels[static_cast<size_t>(i)].empty())
            stream += " BT /F1 22 Tf 1 1 1 rg 20 24 Td (" + labels[static_cast<size_t>(i)] + ") Tj ET";
        objects.push_back("<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream + "\nendstream");
    }
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    std::string out = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", off);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref)
           + "\n%%EOF\n";
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
}

fs::path makeThreePagePdf()
{
    const fs::path file = tempDir() / L"tres-paginas.pdf";
    writeColorPdf(file, {"1 0 0", "0 1 0", "0 0 1"}); // vermelha, verde, azul
    return file;
}

void writeText(const fs::path& file, const std::string& text)
{
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// --------------------------------------------------------------------------- Office via COM

std::set<DWORD> pidsOf(const wchar_t* exe)
{
    const auto v = worker::processIds(exe);
    return {v.begin(), v.end()};
}

// Aplicativo do Office iniciado por este teste: so ele e encerrado no fim
struct OfficeSession
{
    const wchar_t* exe = nullptr;
    std::set<DWORD> before;
    com::AppInstance inst;
    std::set<DWORD> mine;

    bool start(const wchar_t* progId, const wchar_t* exeName)
    {
        exe = exeName;
        before = pidsOf(exeName);
        std::string err;
        inst = com::attachOrCreate(progId, &err);
        if (!inst.app) {
            std::printf("  %s\n", err.c_str());
            return false;
        }
        if (inst.createdByUs) {
            for (int i = 0; i < 40 && mine.empty(); ++i) {
                for (DWORD pid : pidsOf(exeName))
                    if (!before.count(pid))
                        mine.insert(pid);
                if (mine.empty())
                    Sleep(50);
            }
        }
        return true;
    }

    void finish()
    {
        const bool quit = inst.createdByUs && !mine.empty();
        if (quit) {
            try {
                inst.app.call(L"Quit");
            } catch (...) {
            }
        }
        inst.app.reset();
        if (quit) {
            const auto t0 = Clock::now();
            while (secondsSince(t0) < 10.0) {
                bool any = false;
                for (DWORD pid : mine)
                    any = any || WorkerHost::processRunning(pid);
                if (!any)
                    break;
                sleepMs(100);
            }
            for (DWORD pid : mine) {
                if (!WorkerHost::processRunning(pid))
                    continue;
                if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
                    TerminateProcess(h, 1);
                    CloseHandle(h);
                }
            }
        }
    }
};

// Cria um .pptx com 'slides' slides (cada um com um retangulo colorido grande) pelo PowerPoint
bool makePptx(const fs::path& file, int slides, std::string* error)
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    OfficeSession session;
    bool ok = false;
    if (!session.start(L"PowerPoint.Application", L"POWERPNT.EXE")) {
        if (error)
            *error = "PowerPoint indisponível";
    } else {
        try {
            using com::Var;
            const Var noWindow = Var::mso(false);
            com::Object pres = session.inst.app.getObject(L"Presentations").getObject(L"Add", {&noWindow});
            com::Object slideList = pres.getObject(L"Slides");
            const long colors[] = {0x8A3A1E, 0x50A030, 0x2060E0, 0xC09020}; // BGR: azul escuro, verde, laranja, azul claro
            for (int i = 0; i < slides; ++i) {
                const Var index = Var::integer(i + 1), layout = Var::integer(12); // ppLayoutBlank
                com::Object slide = slideList.getObject(L"Add", {&index, &layout});
                com::Object shapes = slide.getObject(L"Shapes");
                const Var type = Var::integer(1), l = Var::integer(0), t = Var::integer(0), w = Var::integer(960),
                          h = Var::integer(540); // msoShapeRectangle cobrindo o slide 16:9 (960x540 pt)
                com::Object rect = shapes.getObject(L"AddShape", {&type, &l, &t, &w, &h});
                rect.getObject(L"Fill").getObject(L"ForeColor").put(L"RGB", Var::integer(colors[i % 4]));
                const Var text = Var::text(L"Slide " + std::to_wstring(i + 1));
                rect.getObject(L"TextFrame").getObject(L"TextRange").put(L"Text", text);
            }
            const Var path = Var::text(file.wstring()), fmt = Var::integer(24); // ppSaveAsOpenXMLPresentation
            pres.call(L"SaveAs", {&path, &fmt});
            pres.call(L"Close");
            ok = true;
        } catch (const std::exception& e) {
            if (error)
                *error = e.what();
        }
        session.finish();
    }
    if (SUCCEEDED(initHr))
        CoUninitialize();
    return ok && fs::is_regular_file(file);
}

// Cria um .docx de duas paginas (quebra de pagina) pelo Word
bool makeDocx(const fs::path& file, std::string* error)
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    OfficeSession session;
    bool ok = false;
    if (!session.start(L"Word.Application", L"WINWORD.EXE")) {
        if (error)
            *error = "Word indisponível";
    } else {
        try {
            using com::Var;
            try {
                session.inst.app.put(L"Visible", Var::boolean(false));
                session.inst.app.put(L"DisplayAlerts", Var::integer(0));
            } catch (...) {
            }
            com::Object doc = session.inst.app.getObject(L"Documents").getObject(L"Add");
            com::Object content = doc.getObject(L"Content");
            content.put(L"Text", Var::text(L"Pagina um do documento de teste do BSL Stage"));
            // Quebra de pagina no fim e segunda pagina
            const Var collapseEnd = Var::integer(0);
            com::Object range = doc.getObject(L"Content");
            range.call(L"InsertParagraphAfter");
            com::Object r2 = doc.getObject(L"Content");
            r2.call(L"Collapse", {&collapseEnd});
            const Var breakType = Var::integer(7); // wdPageBreak
            r2.call(L"InsertBreak", {&breakType});
            com::Object r3 = doc.getObject(L"Content");
            const Var tail = Var::text(L"Pagina dois");
            r3.call(L"InsertAfter", {&tail});
            const Var path = Var::text(file.wstring()), fmt = Var::integer(12); // wdFormatXMLDocument
            doc.call(L"SaveAs2", {&path, &fmt});
            const Var noSave = Var::integer(0);
            doc.call(L"Close", {&noSave});
            ok = true;
        } catch (const std::exception& e) {
            if (error)
                *error = e.what();
        }
        session.finish();
    }
    if (SUCCEEDED(initHr))
        CoUninitialize();
    return ok && fs::is_regular_file(file);
}

// --------------------------------------------------------------------------- testes

std::unique_ptr<PdfSource> openPdf(const fs::path& file, int w, int h, const ParamValues& values = {})
{
    auto src = std::make_unique<PdfSource>(w, h);
    ParamValues v = values;
    v.fillDefaults(PdfSource::describe(v));
    src->setParams(v);
    std::string error;
    if (!src->open(file, &error)) {
        std::printf("  open falhou: %s\n", error.c_str());
        return nullptr;
    }
    return src;
}

bool waitPage(PdfSource& src, int page, double seconds = 12.0)
{
    return waitUntil(src, seconds, [&] { return src.shownPage() == page && src.texture() != 0; });
}

void testPdf(const char* transport)
{
    std::string title = std::string("PDF de 3 paginas (transporte ") + transport + ")";
    section(title.c_str());
    RemoteSource::forceTransport(transport);
    const fs::path pdf = makeThreePagePdf();

    auto srcPtr = openPdf(pdf, 640, 360);
    CHECK(srcPtr != nullptr);
    if (!srcPtr)
        return;
    PdfSource& src = *srcPtr;

    const auto t0 = Clock::now();
    const bool first = waitPage(src, 1);
    CHECK(first);
    std::printf("  primeiro quadro em %.2f s | transporte: %s (%s) | worker pid %lu\n", secondsSince(t0),
                src.transportName().c_str(), src.transportReason().c_str(), src.workerPid());
    if (std::string(transport) == "shm")
        CHECK(src.transportName() == "shm");
    else
        CHECK(src.transportName() == "texture" || src.transportName() == "shm");
    CHECK(!src.transportName().empty());
    CHECK(src.pageCount() == 3);
    CHECK(src.width() == 640 && src.height() == 360);
    if (first) {
        const Pixels p = readSource(src);
        std::printf("  pagina 1 centro %s\n", pixelText(p.at(320, 180)).c_str());
        CHECK(isColor(p.at(320, 180), 255, 0, 0));
        CHECK(p.at(320, 180)[3] == 255);
        CHECK(isColor(p.at(2, 2), 255, 0, 0)); // a pagina ocupa o canvas todo (16:9)
    }

    // Troca de pagina: o quadro muda
    src.goToPage(2);
    CHECK(waitPage(src, 2));
    if (src.shownPage() == 2) {
        const Pixels p = readSource(src);
        std::printf("  pagina 2 centro %s\n", pixelText(p.at(320, 180)).c_str());
        CHECK(isColor(p.at(320, 180), 0, 255, 0));
    }
    src.goToPage(3);
    CHECK(waitPage(src, 3));
    if (src.shownPage() == 3) {
        const Pixels p = readSource(src);
        std::printf("  pagina 3 centro %s\n", pixelText(p.at(320, 180)).c_str());
        CHECK(isColor(p.at(320, 180), 0, 0, 255));
    }
    src.goToPage(99); // alem do fim: fica na ultima
    CHECK(src.page() == 3);
    src.prevPage();
    CHECK(waitPage(src, 2));
    // A pagina anterior ja estava pre-renderizada: volta rapido
    const auto t1 = Clock::now();
    src.goToPage(1);
    CHECK(waitPage(src, 1, 5.0));
    std::printf("  voltar para a pagina 1: %.0f ms\n", secondsSince(t1) * 1000.0);

    // Parametro "page" do clip leva para a pagina
    ParamValues v = src.paramValues();
    v.set("page", 3);
    src.setParams(v);
    CHECK(waitPage(src, 3));

    // Cor de fundo: pagina 4:3 mostraria barras; aqui so confere que a mudanca de cor chega ao worker
    v.set("page", 1);
    v.set("bg", param::colorJson({0.0f, 0.0f, 1.0f, 1.0f}));
    src.setParams(v);
    CHECK(waitPage(src, 1));
    src.close();
    sleepMs(200);
}

void testPdfBackgroundAndFit()
{
    section("PDF: cor de fundo e encaixe (pagina 16:9 num canvas 4:3)");
    RemoteSource::forceTransport("auto");
    const fs::path pdf = makeThreePagePdf();
    ParamValues v;
    v.fillDefaults(PdfSource::describe(v));
    v.set("bg", param::colorJson({0.0f, 0.0f, 1.0f, 1.0f})); // azul
    auto srcPtr = openPdf(pdf, 400, 300, v);
    CHECK(srcPtr != nullptr);
    if (!srcPtr)
        return;
    PdfSource& src = *srcPtr;
    CHECK(waitPage(src, 1));
    if (src.shownPage() == 1) {
        const Pixels p = readSource(src);
        // 400x225 centralizado em 400x300: barras de 37 px em cima e embaixo
        std::printf("  barra %s | pagina %s\n", pixelText(p.at(200, 10)).c_str(), pixelText(p.at(200, 150)).c_str());
        CHECK(isColor(p.at(200, 10), 0, 0, 255));
        CHECK(isColor(p.at(200, 290), 0, 0, 255));
        CHECK(isColor(p.at(200, 150), 255, 0, 0));
    }
    src.close();
}

void testPdfAuto()
{
    section("PDF: avanco automatico por tempo e por batidas, loop e fade");
    RemoteSource::forceTransport("auto");
    const fs::path pdf = makeThreePagePdf();

    {
        ParamValues v;
        v.fillDefaults(PdfSource::describe(v));
        v.set("autoMode", "seconds");
        v.set("autoSeconds", 0.6);
        auto srcPtr = openPdf(pdf, 320, 180, v);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            PdfSource& src = *srcPtr;
            CHECK(waitPage(src, 1));
            // O relogio do avanco usa o dt dos updates: 0,05 s por passo
            int seen = 0;
            std::set<int> pages;
            const auto t0 = Clock::now();
            while (secondsSince(t0) < 20.0 && pages.size() < 3) {
                src.update(0.05);
                pages.insert(src.page());
                sleepMs(20);
            }
            (void)seen;
            std::printf("  paginas visitadas: %zu\n", pages.size());
            CHECK(pages.size() == 3);
            // Loop no fim: depois da ultima volta para a 1
            bool wrapped = false;
            const auto t1 = Clock::now();
            int last = src.page();
            while (secondsSince(t1) < 10.0 && !wrapped) {
                src.update(0.05);
                if (src.page() < last)
                    wrapped = true;
                last = src.page();
                sleepMs(20);
            }
            CHECK(wrapped);
            // Sem loop, para na ultima
            v = src.paramValues();
            v.set("loopEnd", false);
            src.setParams(v);
            const auto t2 = Clock::now();
            while (secondsSince(t2) < 12.0 && src.page() != 3) {
                src.update(0.05);
                sleepMs(20);
            }
            CHECK(src.page() == 3);
            for (int i = 0; i < 40; ++i)
                src.update(0.05); // 2 s simulados: nao passa da ultima
            CHECK(src.page() == 3);
            src.close();
        }
    }
    {
        ParamValues v;
        v.fillDefaults(PdfSource::describe(v));
        v.set("autoMode", "beats");
        v.set("autoBeats", 2);
        auto srcPtr = openPdf(pdf, 320, 180, v);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            PdfSource& src = *srcPtr;
            CHECK(waitPage(src, 1));
            src.setTempo(0.0, 120.0f);
            CHECK(src.page() == 1);
            src.setTempo(1.0, 120.0f); // 1 batida: ainda na 1
            CHECK(src.page() == 1);
            src.setTempo(2.1, 120.0f); // 2 batidas: avanca
            CHECK(src.page() == 2);
            src.setTempo(3.5, 120.0f);
            CHECK(src.page() == 2);
            src.setTempo(4.0, 120.0f);
            CHECK(src.page() == 3);
            CHECK(waitPage(src, 3));
            src.close();
        }
    }
    {
        ParamValues v;
        v.fillDefaults(PdfSource::describe(v));
        v.set("transition", "fade");
        v.set("fadeSeconds", 0.4);
        auto srcPtr = openPdf(pdf, 320, 180, v);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            PdfSource& src = *srcPtr;
            CHECK(waitPage(src, 1));
            src.goToPage(2);
            // Espera o quadro novo chegar sem avancar o relogio do fade (dt = 0)
            const bool arrived = waitUntil(src, 12.0, [&] { return src.shownPage() == 2; }, 0.0);
            CHECK(arrived);
            // t = 0: ainda parece a pagina 1
            Pixels p = readSource(src);
            std::printf("  fade t=0: %s\n", pixelText(p.at(160, 90)).c_str());
            CHECK(isColor(p.at(160, 90), 255, 0, 0, 30));
            src.update(0.2); // metade
            p = readSource(src);
            std::printf("  fade t=0,5: %s\n", pixelText(p.at(160, 90)).c_str());
            CHECK(p.at(160, 90)[0] > 70 && p.at(160, 90)[1] > 70);
            src.update(0.3); // fim
            p = readSource(src);
            std::printf("  fade fim: %s\n", pixelText(p.at(160, 90)).c_str());
            CHECK(isColor(p.at(160, 90), 0, 255, 0, 30));
            src.close();
        }
    }
}

void testThumbnail()
{
    section("miniatura da pagina 1 (worker em modo --thumb)");
    const fs::path pdf = makeThreePagePdf();
    Frame f;
    int pages = 0;
    std::string error;
    const auto t0 = Clock::now();
    const bool ok = renderDocThumbnail(pdf, 160, 90, f, &pages, &error);
    std::printf("  %s em %.0f ms (%s), %d paginas\n", ok ? "ok" : "falhou", secondsSince(t0) * 1000.0, error.c_str(), pages);
    CHECK(ok);
    if (ok) {
        CHECK(f.width == 160 && f.height == 90 && f.format == PixelFormat::RGBA8);
        CHECK(pages == 3);
        CHECK(docPageCount(pdf) == 3);
        const unsigned char* px = f.planes[0].data() + (45 * 160 + 80) * 4;
        std::printf("  centro %s\n", pixelText(px).c_str());
        CHECK(isColor(px, 255, 0, 0));
    }
}

void testWorkerCrash()
{
    section("worker morto a forca: reinicia sozinho, mantem o ultimo quadro");
    RemoteSource::forceTransport("auto");
    const fs::path pdf = makeThreePagePdf();
    auto srcPtr = openPdf(pdf, 320, 180);
    CHECK(srcPtr != nullptr);
    if (!srcPtr)
        return;
    PdfSource& src = *srcPtr;
    CHECK(waitPage(src, 1));
    src.goToPage(2);
    CHECK(waitPage(src, 2));
    const unsigned long pid1 = src.workerPid();
    const GLuint texBefore = src.texture();
    CHECK(pid1 != 0);
    CHECK(WorkerHost::processRunning(pid1));

    const unsigned long long framesBefore = src.framesShown();
    src.debugKillWorker();
    // A UI nao bloqueia e o ultimo quadro continua valido durante o reinicio
    bool sawRestarting = false;
    double worstUpdate = 0.0;
    bool keptTexture = true;
    const auto t0 = Clock::now();
    while (secondsSince(t0) < 15.0) {
        const auto u0 = Clock::now();
        src.update(0.016);
        worstUpdate = std::max(worstUpdate, secondsSince(u0));
        const SourceStatus st = src.status();
        if (st.text.rfind("Reiniciando", 0) == 0) {
            sawRestarting = true;
            keptTexture = keptTexture && src.texture() != 0;
        }
        if (src.workerPid() != 0 && src.workerPid() != pid1 && src.framesShown() > framesBefore && src.shownPage() == 2)
            break;
        sleepMs(5);
    }
    const unsigned long pid2 = src.workerPid();
    std::printf("  pid %lu -> %lu | reiniciando visto: %s | pior update %.1f ms | textura antes %u / agora %u\n", pid1, pid2,
                sawRestarting ? "sim" : "nao", worstUpdate * 1000.0, texBefore, src.texture());
    CHECK(sawRestarting);
    CHECK(keptTexture);
    CHECK(worstUpdate < 0.1);
    CHECK(pid2 != 0 && pid2 != pid1);
    CHECK(src.hostSnapshot().restarts >= 1);
    CHECK(src.shownPage() == 2); // voltou na pagina em que estava
    CHECK(src.status().text.empty());
    const Pixels p = readSource(src);
    CHECK(isColor(p.at(160, 90), 0, 255, 0));
    // Continua obedecendo comandos
    src.goToPage(3);
    CHECK(waitPage(src, 3));
    src.close();

    // O processo antigo tem de ter sumido
    CHECK(!WorkerHost::processRunning(pid1));
}

void testWorkerHang()
{
    section("worker travado (sem responder): o host detecta, reinicia e retoma");
    RemoteSource::forceTransport("auto");
    const fs::path marker = tempDir() / L"hang-uma-vez.flag";
    std::error_code ec;
    fs::remove(marker, ec);
    _wputenv_s(L"BSLSTAGE_WORKER_HANG_ONCE", marker.c_str());
    const fs::path pdf = makeThreePagePdf();
    auto srcPtr = openPdf(pdf, 320, 180);
    CHECK(srcPtr != nullptr);
    if (srcPtr) {
        PdfSource& src = *srcPtr;
        bool sawRestarting = false;
        double worstUpdate = 0.0;
        unsigned long firstPid = 0;
        const auto t0 = Clock::now();
        while (secondsSince(t0) < 45.0) {
            const auto u0 = Clock::now();
            src.update(0.016);
            worstUpdate = std::max(worstUpdate, secondsSince(u0));
            if (firstPid == 0)
                firstPid = src.workerPid();
            sawRestarting = sawRestarting || src.status().text.rfind("Reiniciando", 0) == 0;
            if (src.shownPage() == 1 && src.texture() != 0)
                break;
            sleepMs(10);
        }
        const double took = secondsSince(t0);
        std::printf("  travou, detectado e retomado em %.1f s | reiniciando visto: %s | pior update %.1f ms | pid %lu -> %lu\n",
                    took, sawRestarting ? "sim" : "nao", worstUpdate * 1000.0, firstPid, src.workerPid());
        CHECK(sawRestarting);
        CHECK(src.shownPage() == 1 && src.texture() != 0);
        CHECK(src.workerPid() != firstPid);
        CHECK(took > 10.0 && took < 30.0);
        CHECK(worstUpdate < 0.1);
        CHECK(!WorkerHost::processRunning(firstPid)); // o travado foi encerrado, nao ficou orfao
        src.close();
    }
    _wputenv_s(L"BSLSTAGE_WORKER_HANG_ONCE", L"");
}

void testPdfResize()
{
    section("PDF: o canvas muda de tamanho e a imagem acompanha");
    RemoteSource::forceTransport("auto");
    const fs::path pdf = makeThreePagePdf();
    auto srcPtr = openPdf(pdf, 320, 180);
    CHECK(srcPtr != nullptr);
    if (!srcPtr)
        return;
    PdfSource& src = *srcPtr;
    CHECK(waitPage(src, 1));
    CHECK(src.width() == 320 && src.height() == 180);
    src.setCanvasSize(480, 270);
    const bool resized = waitUntil(src, 10.0, [&] { return src.width() == 480 && src.height() == 270; });
    CHECK(resized);
    if (resized) {
        const Pixels p = readSource(src);
        CHECK(isColor(p.at(240, 135), 255, 0, 0));
    }
    src.goToPage(2);
    CHECK(waitPage(src, 2));
    CHECK(src.width() == 480);
    src.close();
}

void testWorkerGivesUp()
{
    section("worker que nunca sobe: 3 reinicios e depois Falhou com o motivo");
    RemoteSource::forceTransport("auto");
    _putenv_s("BSLSTAGE_WORKER_CRASH", "1"); // o worker sai logo na partida (so para teste)
    const fs::path pdf = makeThreePagePdf();
    auto srcPtr = openPdf(pdf, 320, 180);
    CHECK(srcPtr != nullptr);
    if (srcPtr) {
        PdfSource& src = *srcPtr;
        bool sawRestarting = false;
        std::set<std::string> texts;
        const auto t0 = Clock::now();
        while (secondsSince(t0) < 40.0 && src.hostSnapshot().state != HostState::Failed) {
            src.update(0.016);
            const std::string t = src.status().text;
            if (!t.empty())
                texts.insert(t);
            sawRestarting = sawRestarting || t.rfind("Reiniciando", 0) == 0;
            sleepMs(10);
        }
        const HostSnapshot s = src.hostSnapshot();
        std::printf("  estado final: \"%s\" | reinicios: %d | %.1f s\n", s.text.c_str(), s.restarts, secondsSince(t0));
        for (const std::string& t : texts)
            std::printf("    visto: %s\n", t.c_str());
        CHECK(s.state == HostState::Failed);
        CHECK(s.restarts == 3);
        CHECK(sawRestarting);
        CHECK(src.status().text.rfind("Falhou", 0) == 0);
        CHECK(!s.failure.empty());
        CHECK(src.status().level == SourceStatus::Level::Warning);
        src.close();
    }
    _putenv_s("BSLSTAGE_WORKER_CRASH", "");
}

void testTransportFallbackLog()
{
    section("escolha automatica do transporte de quadros");
    RemoteSource::forceTransport("auto");
    const fs::path pdf = makeThreePagePdf();
    auto srcPtr = openPdf(pdf, 320, 180);
    CHECK(srcPtr != nullptr);
    if (srcPtr) {
        CHECK(waitPage(*srcPtr, 1));
        std::printf("  AUTO escolheu: %s (%s)\n", srcPtr->transportName().c_str(), srcPtr->transportReason().c_str());
        CHECK(!srcPtr->transportName().empty());
        srcPtr->close();
    }
}

// --------------------------------------------------------------------------- Office

void testOffice()
{
    section("Office: pptx e docx convertidos pelo Microsoft Office");
    RemoteSource::forceTransport("auto");
    const std::set<DWORD> pptBefore = pidsOf(L"POWERPNT.EXE"), wordBefore = pidsOf(L"WINWORD.EXE"),
                          excelBefore = pidsOf(L"EXCEL.EXE");
    CLSID clsid{};
    if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid)) || FAILED(CLSIDFromProgID(L"Word.Application", &clsid))) {
        skip("Microsoft Office não instalado");
        return;
    }
    std::printf("  instancias do Office ja abertas antes: PowerPoint %zu, Word %zu, Excel %zu (nao serao tocadas)\n",
                pptBefore.size(), wordBefore.size(), excelBefore.size());

    const fs::path dir = tempDir();
    const fs::path pptx = dir / L"teste-slides.pptx";
    const fs::path docx = dir / L"teste-documento.docx";
    std::error_code ec;
    fs::remove(pptx, ec);
    fs::remove(docx, ec);
    std::string error;
    const bool havePptx = makePptx(pptx, 3, &error);
    std::printf("  pptx de teste: %s %s\n", havePptx ? "criado" : "FALHOU", error.c_str());
    CHECK(havePptx);
    error.clear();
    const bool haveDocx = makeDocx(docx, &error);
    std::printf("  docx de teste: %s %s\n", haveDocx ? "criado" : "FALHOU", error.c_str());
    CHECK(haveDocx);

    // Apaga o cache dos dois para medir a conversao de verdade
    if (havePptx)
        fs::remove(worker::docCachePath(pptx), ec);
    if (haveDocx)
        fs::remove(worker::docCachePath(docx), ec);

    auto run = [&](const fs::path& file, int expectedPages, const char* label) {
        if (!fs::is_regular_file(file))
            return;
        auto srcPtr = openPdf(file, 640, 360);
        CHECK(srcPtr != nullptr);
        if (!srcPtr)
            return;
        PdfSource& src = *srcPtr;
        const auto t0 = Clock::now();
        bool sawConverting = false;
        const bool got = waitUntil(
            src, 120.0,
            [&] {
                sawConverting = sawConverting || src.status().text.rfind("Convertendo", 0) == 0;
                return src.shownPage() >= 1 && src.texture() != 0;
            },
            0.016);
        std::printf("  %s: %s em %.1f s | 'Convertendo…' visto: %s | paginas: %d\n", label, got ? "pronto" : "NAO ABRIU",
                    secondsSince(t0), sawConverting ? "sim" : "nao", src.pageCount());
        CHECK(got);
        CHECK(sawConverting);
        CHECK(src.pageCount() == expectedPages);
        if (got) {
            const Pixels p = readSource(src);
            CHECK(p.w == 640 && p.h == 360);
            // Algo foi desenhado (pagina branca ou colorida), nao so o fundo preto
            long nonBlack = 0;
            for (int y = 0; y < p.h; y += 4)
                for (int x = 0; x < p.w; x += 4)
                    if (p.at(x, y)[0] + p.at(x, y)[1] + p.at(x, y)[2] > 60)
                        ++nonBlack;
            std::printf("    pixels claros: %ld\n", nonBlack);
            CHECK(nonBlack > 500);
        }
        // Navega: a ultima pagina
        src.goToPage(expectedPages);
        CHECK(waitPage(src, expectedPages, 15.0));
        src.close();
    };
    if (havePptx)
        run(pptx, 3, "pptx");
    if (haveDocx)
        run(docx, 2, "docx");

    // Cache: abrir de novo nao reconverte (rapido)
    if (havePptx) {
        const fs::path cached = worker::docCachePath(pptx);
        CHECK(fs::is_regular_file(cached));
        auto srcPtr = openPdf(pptx, 640, 360);
        if (srcPtr) {
            const auto t0 = Clock::now();
            const bool got = waitUntil(*srcPtr, 30.0, [&] { return srcPtr->shownPage() >= 1; });
            std::printf("  pptx pelo cache: %.2f s\n", secondsSince(t0));
            CHECK(got);
            CHECK(secondsSince(t0) < 5.0);
            srcPtr->close();
        }
        // Arquivo mudou (mtime): o caminho do cache muda e reconverte
        const fs::path before = worker::docCachePath(pptx);
        fs::last_write_time(pptx, fs::file_time_type::clock::now() + std::chrono::seconds(5), ec);
        const fs::path after = worker::docCachePath(pptx);
        CHECK(before != after);
    }

    // Thumbnail de um Office pelo worker
    if (haveDocx) {
        Frame f;
        int pages = 0;
        std::string terr;
        const bool ok = renderDocThumbnail(docx, 160, 90, f, &pages, &terr);
        std::printf("  miniatura do docx: %s (%s), %d paginas\n", ok ? "ok" : "falhou", terr.c_str(), pages);
        CHECK(ok);
        CHECK(pages == 2);
    }

    // O que este teste abriu tem de ter fechado; o que ja existia fica como estava
    const auto t0 = Clock::now();
    for (;;) {
        bool extra = false;
        for (DWORD pid : pidsOf(L"POWERPNT.EXE"))
            extra = extra || !pptBefore.count(pid);
        for (DWORD pid : pidsOf(L"WINWORD.EXE"))
            extra = extra || !wordBefore.count(pid);
        for (DWORD pid : pidsOf(L"EXCEL.EXE"))
            extra = extra || !excelBefore.count(pid);
        if (!extra || secondsSince(t0) > 15.0) {
            CHECK(!extra);
            break;
        }
        sleepMs(200);
    }
    for (DWORD pid : pptBefore)
        CHECK(WorkerHost::processRunning(pid)); // a instancia do usuario continua la
    for (DWORD pid : wordBefore)
        CHECK(WorkerHost::processRunning(pid));
}

void testOfficeMissingMessage()
{
    section("Office: arquivo inexistente e mensagem de erro");
    RemoteSource::forceTransport("auto");
    const fs::path ghost = tempDir() / L"nao-existe.pptx";
    auto src = std::make_unique<PdfSource>(320, 180);
    std::string error;
    CHECK(!src->open(ghost, &error));
    CHECK(!error.empty());
    std::printf("  %s\n", error.c_str());
}

// --------------------------------------------------------------------------- Web

fs::path writeHtml(const char* name, const std::string& html)
{
    const fs::path file = tempDir() / fs::path(name);
    writeText(file, html);
    return file;
}

std::unique_ptr<WebSource> openWeb(const fs::path& file, int w, int h, const ParamValues& values = {})
{
    auto src = std::make_unique<WebSource>(w, h);
    ParamValues v = values;
    v.fillDefaults(WebSource::describeFile(v));
    src->setParams(v);
    std::string error;
    if (!src->open(file, &error)) {
        std::printf("  open falhou: %s\n", error.c_str());
        return nullptr;
    }
    return src;
}

bool cefAvailable()
{
    return fs::is_regular_file(fs::path(WorkerHost::workerExePath()).parent_path() / L"cef" / L"libcef.dll");
}

void testWeb(const char* transport)
{
    section((std::string("Web: file:// vermelho opaco, fundo transparente e clique (transporte ") + transport + ")").c_str());
    if (!cefAvailable()) {
        skip("CEF não encontrado (build/Release/cef/libcef.dll): a Web está desabilitada nesta compilação");
        return;
    }
    RemoteSource::forceTransport(transport);
    const fs::path red = writeHtml("vermelho.html",
                                   "<!doctype html><html><body style=\"margin:0;background:#ff0000\"></body></html>");
    {
        auto srcPtr = openWeb(red, 640, 360);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            WebSource& src = *srcPtr;
            const auto t0 = Clock::now();
            const bool got = waitUntil(src, 60.0, [&] { return src.framesShown() > 0 && src.texture() != 0; });
            std::printf("  primeiro quadro em %.2f s | transporte: %s (%s)\n", secondsSince(t0), src.transportName().c_str(),
                        src.transportReason().c_str());
            CHECK(got);
            CHECK(src.status().level != SourceStatus::Level::Warning);
            if (got) {
                // O CEF pode mandar um quadro em branco antes do conteudo: espera o vermelho
                bool red_ok = waitUntil(src, 15.0, [&] {
                    const Pixels p = readSource(src);
                    return isColor(p.at(p.w / 2, p.h / 2), 255, 0, 0);
                });
                const Pixels p = readSource(src);
                std::printf("  pixel central %s (%dx%d)\n", pixelText(p.at(p.w / 2, p.h / 2)).c_str(), p.w, p.h);
                CHECK(red_ok);
                CHECK(p.at(p.w / 2, p.h / 2)[3] == 255);
                CHECK(p.w == 640 && p.h == 360);
            }
            src.close();
        }
    }
    {
        // Fundo transparente: sem cor no corpo, so um quadrado vermelho no canto
        const fs::path clear = writeHtml("transparente.html",
                                         "<!doctype html><html style=\"background:transparent\"><body style=\"margin:0;background:transparent\">"
                                         "<div style=\"position:absolute;left:0;top:0;width:100px;height:100px;background:#ff0000\"></div>"
                                         "</body></html>");
        ParamValues v;
        v.fillDefaults(WebSource::describeFile(v));
        v.set("transparent", true);
        auto srcPtr = openWeb(clear, 640, 360, v);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            WebSource& src = *srcPtr;
            const bool got = waitUntil(src, 60.0, [&] {
                if (src.framesShown() == 0 || src.texture() == 0)
                    return false;
                const Pixels p = readSource(src);
                return isColor(p.at(50, 50), 255, 0, 0, 30);
            });
            CHECK(got);
            const Pixels p = readSource(src);
            std::printf("  quadrado %s | fundo %s\n", pixelText(p.at(50, 50)).c_str(), pixelText(p.at(400, 250)).c_str());
            CHECK(p.at(400, 250)[3] == 0);
            CHECK(p.at(50, 50)[3] == 255);
            src.close();
        }
    }
    {
        // CSS extra injetado: a pagina e vermelha, o CSS a deixa azul
        ParamValues v;
        v.fillDefaults(WebSource::describeFile(v));
        v.set("css", "html, body { background: #0000ff !important; }");
        auto srcPtr = openWeb(red, 320, 180, v);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            WebSource& src = *srcPtr;
            const bool blue = waitUntil(src, 60.0, [&] {
                if (src.framesShown() == 0 || src.texture() == 0)
                    return false;
                const Pixels p = readSource(src);
                return isColor(p.at(160, 90), 0, 0, 255);
            });
            std::printf("  css injetado: %s\n", blue ? "azul" : "NAO aplicado");
            CHECK(blue);
            // Troca o CSS com a pagina aberta
            v.set("css", "html, body { background: #00ff00 !important; }");
            src.setParams(v);
            const bool green = waitUntil(src, 15.0, [&] {
                const Pixels p = readSource(src);
                return isColor(p.at(160, 90), 0, 255, 0);
            });
            CHECK(green);
            src.close();
        }
    }
    {
        // Gerador "Web (URL)": a URL vem do parametro (data: para nao depender de rede)
        auto src = std::make_unique<WebSource>(320, 180);
        ParamValues v;
        v.fillDefaults(WebSource::describe(v));
        v.set("url", "data:text/html,<body style=\"margin:0;background:%2300ff00\"></body>");
        src->setParams(v);
        std::string error;
        CHECK(src->open({}, &error));
        const bool green = waitUntil(*src, 60.0, [&] {
            if (src->framesShown() == 0 || src->texture() == 0)
                return false;
            const Pixels p = readSource(*src);
            return isColor(p.at(160, 90), 0, 255, 0);
        });
        std::printf("  gerador com URL por parametro: %s\n", green ? "verde" : "NAO abriu");
        CHECK(green);
        CHECK(WebSource::label(v) == "Web: " || WebSource::label(v).rfind("Web", 0) == 0);
        // Troca a URL com a fonte aberta
        v.set("url", "data:text/html,<body style=\"margin:0;background:%23ff00ff\"></body>");
        src->setParams(v);
        const bool magenta = waitUntil(*src, 20.0, [&] {
            const Pixels p = readSource(*src);
            return isColor(p.at(160, 90), 255, 0, 255);
        });
        CHECK(magenta);
        src->close();
    }
    {
        // Clique encaminhado: a pagina troca de cor ao receber o clique
        const fs::path click = writeHtml("clique.html",
                                         "<!doctype html><html><body style=\"margin:0;background:#ff0000;width:100vw;height:100vh\" "
                                         "onmousedown=\"document.body.style.background='#00ff00'\"></body></html>");
        auto srcPtr = openWeb(click, 640, 360);
        CHECK(srcPtr != nullptr);
        if (srcPtr) {
            WebSource& src = *srcPtr;
            const bool red_ok = waitUntil(src, 60.0, [&] {
                if (src.framesShown() == 0 || src.texture() == 0)
                    return false;
                const Pixels p = readSource(src);
                return isColor(p.at(320, 180), 255, 0, 0);
            });
            CHECK(red_ok);
            src.sendMouse("move", 320, 180, 0, 0, 0);
            src.sendMouse("down", 320, 180, 0, 1, 0);
            src.sendMouse("up", 320, 180, 0, 1, 0);
            const bool green = waitUntil(src, 15.0, [&] {
                const Pixels p = readSource(src);
                return isColor(p.at(320, 180), 0, 255, 0);
            });
            std::printf("  depois do clique: %s\n", green ? "verde" : "continua vermelho");
            CHECK(green);
            src.close();
        }
    }
}

void testWebCrash()
{
    section("Web: worker do navegador morto a força reinicia e volta a mostrar a página");
    if (!cefAvailable()) {
        skip("CEF não encontrado");
        return;
    }
    RemoteSource::forceTransport("auto");
    const fs::path red = writeHtml("vermelho2.html",
                                   "<!doctype html><html><body style=\"margin:0;background:#ff0000\"></body></html>");
    auto srcPtr = openWeb(red, 320, 180);
    CHECK(srcPtr != nullptr);
    if (!srcPtr)
        return;
    WebSource& src = *srcPtr;
    auto isRed = [&] {
        if (src.framesShown() == 0 || src.texture() == 0)
            return false;
        const Pixels p = readSource(src);
        return isColor(p.at(160, 90), 255, 0, 0);
    };
    CHECK(waitUntil(src, 60.0, isRed));
    const unsigned long pid1 = src.workerPid();
    const unsigned long long framesBefore = src.framesShown();
    src.debugKillWorker();
    bool sawRestarting = false;
    const auto t0 = Clock::now();
    while (secondsSince(t0) < 60.0) {
        src.update(0.016);
        sawRestarting = sawRestarting || src.status().text.rfind("Reiniciando", 0) == 0;
        if (src.workerPid() != 0 && src.workerPid() != pid1 && src.framesShown() > framesBefore && isRed())
            break;
        sleepMs(10);
    }
    std::printf("  pid %lu -> %lu | reiniciando visto: %s | %.1f s\n", pid1, src.workerPid(), sawRestarting ? "sim" : "nao",
                secondsSince(t0));
    CHECK(sawRestarting);
    CHECK(src.workerPid() != 0 && src.workerPid() != pid1);
    CHECK(isRed());
    src.close();
    // Os processos do navegador do worker morto (GPU, renderer) tambem tem de ter ido embora: a conferencia final
    // de bslstage_worker.exe pega qualquer sobra
}

// --------------------------------------------------------------------------- arquivos de demonstracao

int makeDemoDocs(const fs::path& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    writeColorPdf(dir / L"sample.pdf", {"0.93 0.2 0.22", "0.1 0.4 0.9", "0.1 0.7 0.4", "0.5 0.2 0.8"},
                  {"BSL Stage - PDF - pagina 1", "BSL Stage - PDF - pagina 2", "BSL Stage - PDF - pagina 3",
                   "BSL Stage - PDF - pagina 4"});
    std::string error;
    if (!makePptx(dir / L"sample.pptx", 4, &error))
        std::printf("pptx nao gerado: %s\n", error.c_str());
    writeText(dir / L"sample.html",
              "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
              "html,body{margin:0;background:transparent;font-family:Segoe UI,Arial,sans-serif}"
              ".badge{position:absolute;right:48px;top:40px;padding:18px 34px;border-radius:14px;"
              "background:rgba(236,51,55,.92);color:#fff;font-size:44px;font-weight:700;letter-spacing:1px}"
              ".clock{position:absolute;right:48px;top:140px;color:#fff;font-size:72px;font-weight:700;"
              "text-shadow:0 2px 12px #000}</style></head><body><div class=\"badge\">BSL Stage · Web ao vivo</div>"
              "<div class=\"clock\" id=\"c\"></div><script>"
              "function t(){var d=new Date();document.getElementById('c').textContent=d.toLocaleTimeString('pt-BR');}"
              "t();setInterval(t,250);</script></body></html>");
    std::printf("gerados em %s\n", dir.string().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    // O worker precisa do COM do chamador? Nao: so este teste cria Office via COM
    if (argc >= 3 && std::string(argv[1]) == "--make-demo-docs") {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        return makeDemoDocs(fs::absolute(fs::path(argv[2])).lexically_normal());
    }
    std::set<std::string> wanted;
    for (int i = 1; i < argc; ++i)
        wanted.insert(argv[i]);
    auto want = [&](const char* name) { return wanted.empty() || wanted.count(name) > 0; };

    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "doctest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;
    std::printf("GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    {
        std::string err;
        auto interop = render::D3D11Interop::shared(&err);
        if (interop)
            std::printf("%s\n", interop->describe().c_str());
        else
            std::printf("sem D3D11: %s\n", err.c_str());
    }
    const std::set<DWORD> workersBefore = pidsOf(L"bslstage_worker.exe");

    if (want("pdf")) {
        testPdf("auto");
        testPdf("shm");
        testPdfBackgroundAndFit();
        testPdfAuto();
        testThumbnail();
        testPdfResize();
        testTransportFallbackLog();
    }
    if (want("crash")) {
        testWorkerCrash();
        testWorkerGivesUp();
        testWorkerHang();
    }
    if (want("office")) {
        testOfficeMissingMessage();
        testOffice();
    }
    if (want("web")) {
        testWeb("auto");
        testWeb("shm");
        testWebCrash();
    }

    // Nada de worker sobrando
    section("processos auxiliares encerrados");
    const bool allStopped = WorkerHost::waitAllStopped(15000);
    CHECK(allStopped);
    {
        const auto t0 = Clock::now();
        bool extra = true;
        while (extra && secondsSince(t0) < 10.0) {
            extra = false;
            for (DWORD pid : pidsOf(L"bslstage_worker.exe"))
                extra = extra || !workersBefore.count(pid);
            if (extra)
                sleepMs(100);
        }
        std::printf("  bslstage_worker.exe sobrando: %s\n", extra ? "SIM" : "nenhum");
        CHECK(!extra);
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    std::printf("\n%d verificacoes, %d falha(s), %d pulo(s)\n", g_checks, g_failures, g_skipped);
    std::printf(g_failures == 0 ? "DOCTEST OK\n" : "DOCTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
