#include "ui/panels/PixelMapPanels.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "io/FileDialogs.h"
#include "output/PatternRaster.h"
#include "output/PixelMap.h"
#include "output/Snap.h"
#include "render/TextureLoader.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/MapView.h"

#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

namespace pm = bsl::pixelmap;
namespace fs = std::filesystem;
using snap::RectF;

// --------------------------------------------------------------------------------------------------------------
// Estado da interface (a configuração em si vive em Composition::pixelMap e vai no projeto)

// Rascunho do formulário "Adicionar painel" (os valores do exemplo do HTML)
struct Draft
{
    std::string name = "Testeira Palco";
    int x = 0;
    int y = 0;
    int w = 2048;
    int h = 128;
    bool byCabinets = false;
    int cols = 16;
    int rows = 1;
    int cabW = 128;
    int cabH = 128;
};

enum class DragKind { None, Pan, Move };

struct DragState
{
    DragKind kind = DragKind::None;
    ImVec2 startMouse{};
    ImVec2 startOffset{};
    int index = -1;
    int startX = 0;
    int startY = 0;
    bool moved = false;
    snap::Result snap;
};

struct State
{
    Draft draft;
    int selected = -1;

    // Vista do overview
    float scale = 1.0f;
    ImVec2 offset{0.0f, 0.0f};
    bool autoFit = true;
    bool recenter = false;
    int lastW = 0;
    int lastH = 0;
    DragState drag;
    bool snapOn = true;
    bool gridOn = true;

    pm::Validation val;

    std::string status;
    bool statusWarn = false;

    Texture wmTex; // marca d'água escolhida pelo usuário
    std::string wmKey;

    bool openCreate = false;
    bool openImport = false;
};
State g;

ImU32 col32(std::uint32_t rgb, float alpha = 1.0f)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f));
}

ImU32 colv(const ImVec4& c, float alpha = 1.0f)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

void setStatus(const std::string& msg, bool warn = false)
{
    g.status = msg;
    g.statusWarn = warn;
    log::info("Pixel Map: " + msg);
}

std::string utf8(const fs::path& p)
{
    return paths::toUtf8(p);
}

fs::path pathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::wstring wideName(const std::string& utf8Name)
{
    return pathFromUtf8(utf8Name).wstring();
}

const Texture* watermarkTexture(const pm::Config& c)
{
    if (c.watermark == pm::Watermark::Monogram) {
        const Texture& m = brand().monogram;
        return m ? &m : nullptr;
    }
    if (c.watermark == pm::Watermark::Image) {
        if (g.wmKey != c.watermarkPath) {
            g.wmKey = c.watermarkPath;
            g.wmTex.reset();
            g.wmTex = loadTexture(pathFromUtf8(c.watermarkPath));
        }
        return g.wmTex ? &g.wmTex : nullptr;
    }
    return nullptr;
}

// --------------------------------------------------------------------------------------------------------------
// Ações de exportar / importar (diálogos nativos do Windows)

bool writePattern(const pm::Config& cfg, const pm::Panel& p, const fs::path& file, std::string* err)
{
    pm::Image logo;
    const bool haveLogo = pm::loadWatermark(cfg, logo);
    const pm::Image img = pm::renderPattern(p, haveLogo ? &logo : nullptr);
    if (img.empty()) {
        if (err)
            *err = "painel grande demais para gerar a imagem";
        return false;
    }
    return pm::writePng(file, img, err);
}

void actionGeneratePattern(const pm::Config& cfg, const pm::Panel& p)
{
    const auto file = dialogs::saveFile(GetActiveWindow(), L"Gerar padrão HD", L"Imagem PNG (*.png)", L"*.png",
                                        wideName(pm::patternFileName(p)), L"png");
    if (!file)
        return;
    std::string err;
    if (writePattern(cfg, p, *file, &err))
        setStatus("Padrão HD gravado: " + utf8(*file) + " (" + std::to_string(p.w) + "x" + std::to_string(p.h) + ")");
    else
        setStatus("Não foi possível gravar o padrão: " + err, true);
}

void actionExportAll(const pm::Config& cfg)
{
    if (cfg.panels.empty())
        return;
    const auto folder = dialogs::pickFolder(GetActiveWindow());
    if (!folder)
        return;
    pm::Image logo;
    const bool haveLogo = pm::loadWatermark(cfg, logo);
    std::set<std::string> used;
    int ok = 0;
    std::string firstError;
    for (const pm::Panel& p : cfg.panels) {
        std::string name = pm::patternFileName(p);
        for (int n = 2; !used.insert(name).second; ++n) // dois painéis com o mesmo nome e tamanho
            name = pm::patternFileName(p).substr(0, pm::patternFileName(p).size() - 4) + "_" + std::to_string(n) + ".png";
        const pm::Image img = pm::renderPattern(p, haveLogo ? &logo : nullptr);
        std::string err;
        if (!img.empty() && pm::writePng(*folder / pathFromUtf8(name), img, &err))
            ++ok;
        else if (firstError.empty())
            firstError = p.name + ": " + (err.empty() ? "painel grande demais" : err);
    }
    if (firstError.empty())
        setStatus(std::to_string(ok) + " padrão(ões) HD gravado(s) em " + utf8(*folder));
    else
        setStatus(std::to_string(ok) + " gravado(s); falhou: " + firstError, true);
}

void actionExportOverview(const pm::Config& cfg)
{
    const auto file = dialogs::saveFile(GetActiveWindow(), L"Exportar overview", L"Imagem PNG (*.png)", L"*.png",
                                        L"BSLLight_Resolume_Map.png", L"png");
    if (!file)
        return;
    pm::Image wm;
    const bool haveWm = pm::loadWatermark(cfg, wm);
    const pm::Image img = pm::renderOverview(cfg, haveWm ? &wm : nullptr);
    std::string err;
    if (!img.empty() && pm::writePng(*file, img, &err))
        setStatus("Overview gravado: " + utf8(*file) + " (" + std::to_string(img.w) + "x" + std::to_string(img.h) + ")");
    else
        setStatus("Não foi possível gravar o overview: " + (err.empty() ? std::string("imagem grande demais") : err), true);
}

void actionExportCsv(const pm::Config& cfg)
{
    const auto file = dialogs::saveFile(GetActiveWindow(), L"Exportar tabela", L"Planilha CSV (*.csv)", L"*.csv",
                                        L"BSLLight_PixelMap.csv", L"csv");
    if (!file)
        return;
    std::ofstream out(*file, std::ios::binary | std::ios::trunc);
    const std::string csv = pm::toCsv(cfg);
    out.write(csv.data(), static_cast<std::streamsize>(csv.size()));
    out.flush();
    if (out)
        setStatus("Tabela gravada: " + utf8(*file) + " (" + std::to_string(cfg.panels.size()) + " painéis)");
    else
        setStatus("Não foi possível gravar " + utf8(*file), true);
}

void actionImportCsv(pm::Config& cfg)
{
    const auto file = dialogs::openFile(GetActiveWindow(), L"Planilha CSV (*.csv)", L"*.csv;*.txt");
    if (!file)
        return;
    std::ifstream in(*file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<pm::Panel> panels;
    std::string err;
    if (!in || !pm::fromCsv(text, panels, &err)) {
        setStatus("CSV não importado: " + (err.empty() ? std::string("não foi possível ler o arquivo") : err), true);
        return;
    }
    cfg.panels = std::move(panels);
    pm::normalize(cfg);
    g.selected = -1;
    setStatus(std::to_string(cfg.panels.size()) + " painéis importados de " + utf8(file->filename()));
}

// --------------------------------------------------------------------------------------------------------------
// Pequenos blocos de interface

void dim(const char* text)
{
    ImGui::TextColored(theme::kTextDim, "%s", text);
}

bool chip(const char* label, bool on, const char* tip = nullptr)
{
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    }
    const bool pressed = ImGui::Button(label);
    if (on)
        ImGui::PopStyleColor(4);
    if (tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    return pressed;
}

bool primaryButton(const char* label, ImVec2 size = ImVec2(-FLT_MIN, 0.0f))
{
    ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool numField(const char* id, int& v, int lo, int hi, float step = 1.0f, const char* unit = nullptr)
{
    return hotInt(id, v, lo, hi, step, "%d", unit, INT_MIN, -1.0f);
}

// Duas colunas rotuladas (largura/altura, x/y...)
bool beginPair(const char* id)
{
    return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame);
}

void pairLabels(const char* a, const char* b)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    dim(a);
    ImGui::TableSetColumnIndex(1);
    dim(b);
}

void scaleText(const pm::Config& c, const pm::Validation& v)
{
    const pm::Scale s = pm::scaleOf(c);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Escala  X %.4f   Y %.4f", s.x, s.y);
    pushMono();
    if (v.scaleWarn)
        ImGui::TextColored(theme::kWarning, "%s", buf);
    else
        ImGui::TextColored(theme::kTextDim, "%s", buf);
    popMono();
}

// --------------------------------------------------------------------------------------------------------------
// Mapeamento (ponte com o OutputSetup)

void doCreateMapping(PanelContext& ctx, pm::MapMode mode, int target)
{
    const int idx = pm::createMapping(ctx.composition.pixelMap, ctx.output, mode, target, ctx.composition.width,
                                      ctx.composition.height);
    ctx.mapping.screen = idx;
    selectOnly(ctx.mapping, 0);
    mapHistoryObserve(ctx); // entra no histórico de desfazer do mapeamento (Ctrl+Z no modo Mapeamento)
    const Screen& s = ctx.output.screens[static_cast<size_t>(idx)];
    setStatus("Mapeamento " + std::string(mode == pm::MapMode::Add ? "criado" : "atualizado") + ": tela \"" + s.name + "\" "
              + std::to_string(s.width) + "x" + std::to_string(s.height) + " com " + std::to_string(s.slices.size())
              + " slice(s). Desfazer: Ctrl+Z no modo Mapeamento.");
}

int targetScreen(const PanelContext& ctx)
{
    const int n = static_cast<int>(ctx.output.screens.size());
    return ctx.mapping.screen >= 0 && ctx.mapping.screen < n ? ctx.mapping.screen : (n > 0 ? 0 : -1);
}

void drawMappingModals(PanelContext& ctx)
{
    pm::Config& cfg = ctx.composition.pixelMap;
    const char* createName = "Criar mapeamento##pm_modal_criar";
    const char* importName = "Importar do mapeamento##pm_modal_importar";
    if (g.openCreate) {
        ImGui::OpenPopup(createName);
        g.openCreate = false;
    }
    if (g.openImport) {
        ImGui::OpenPopup(importName);
        g.openImport = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(createName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const int target = targetScreen(ctx);
        const std::string name = target >= 0 ? ctx.output.screens[static_cast<size_t>(target)].name : std::string();
        ImGui::Text("Os %d painéis viram uma tela de %dx%d com um slice por painel.", static_cast<int>(cfg.panels.size()),
                    cfg.physW, cfg.physH);
        ImGui::TextColored(theme::kTextDim, "Entrada do slice: o retângulo escalado no canvas. Saída: a posição física.");
        ImGui::Spacing();
        if (target >= 0) {
            const std::string label = "Substituir \"" + name + "\"##pm_criar_substituir";
            if (primaryButton(label.c_str(), ImVec2(dp(300.0f), 0.0f))) {
                doCreateMapping(ctx, pm::MapMode::Replace, target);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::Button("Adicionar nova tela##pm_criar_adicionar", ImVec2(dp(300.0f), 0.0f))) {
            doCreateMapping(ctx, pm::MapMode::Add, -1);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Cancelar##pm_criar_cancelar", ImVec2(dp(300.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(importName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const int target = targetScreen(ctx);
        if (target < 0) {
            ImGui::TextUnformatted("Não há telas no mapeamento.");
        } else {
            const Screen& s = ctx.output.screens[static_cast<size_t>(target)];
            ImGui::Text("Substituir os %d painéis atuais pelos %d slices de \"%s\"?", static_cast<int>(cfg.panels.size()),
                        static_cast<int>(s.slices.size()), s.name.c_str());
            ImGui::TextColored(theme::kTextDim, "O físico total vira %dx%d.", s.width, s.height);
            ImGui::Spacing();
            if (primaryButton("Importar##pm_importar_ok", ImVec2(dp(140.0f), 0.0f))) {
                if (pm::importFromScreen(cfg, s)) {
                    g.selected = -1;
                    setStatus(std::to_string(cfg.panels.size()) + " painéis importados da tela \"" + s.name + "\".");
                } else {
                    setStatus("A tela \"" + s.name + "\" não tem slices.", true);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancelar##pm_importar_cancelar", ImVec2(dp(140.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace

// --------------------------------------------------------------------------------------------------------------

void pixelMapPrepare(PanelContext& ctx)
{
    pm::Config& cfg = ctx.composition.pixelMap;
    pm::syncInput(cfg, ctx.composition.width, ctx.composition.height);
    pm::normalize(cfg);
    g.val = pm::validate(cfg);
    if (g.selected >= static_cast<int>(cfg.panels.size()))
        g.selected = static_cast<int>(cfg.panels.size()) - 1;
}

void pixelMapReset()
{
    g.selected = -1;
    g.drag = {};
    g.autoFit = true;
    g.lastW = g.lastH = 0;
    g.draft = Draft{};
    g.status.clear();
    g.wmKey.clear();
    g.wmTex.reset();
}

void pixelMapRelease()
{
    g.wmTex.reset();
    g.wmKey.clear();
}

PixelMapUiInfo pixelMapUiInfo()
{
    PixelMapUiInfo i;
    i.selected = g.selected;
    i.snap = g.snapOn;
    i.dragging = g.drag.kind == DragKind::Move;
    i.guides = static_cast<int>(g.drag.snap.guides.size());
    i.zoom = g.scale;
    i.status = g.status;
    return i;
}

// ==============================================================================================================
// Configuração

void drawPixelMapConfigPanel(PanelContext& ctx)
{
    beginPanel(panel::kPmConfig);
    pm::Config& cfg = ctx.composition.pixelMap;
    const pm::Validation& val = g.val;

    // --- Input -----------------------------------------------------------------------------------------------
    ImGui::SeparatorText("Input (composição)");
    if (ImGui::BeginTable("##pm_input", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("h", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + dp(4.0f));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        dim("Largura");
        ImGui::TableSetColumnIndex(1);
        dim("Altura");
        ImGui::TableNextRow();
        ImGui::BeginDisabled(cfg.linkCanvas);
        ImGui::TableSetColumnIndex(0);
        numField("##pm_in_w", cfg.inW, 1, pm::kMaxDim, 4.0f, "px");
        ImGui::TableSetColumnIndex(1);
        numField("##pm_in_h", cfg.inH, 1, pm::kMaxDim, 4.0f, "px");
        ImGui::EndDisabled();
        ImGui::TableSetColumnIndex(2);
        if (iconButton(cfg.linkCanvas ? ICON_LOCK : ICON_LOCK_OPEN, "##pm_cadeado",
                       cfg.linkCanvas ? "Ligado ao canvas da composição. Clique para editar o input livremente"
                                      : "Input livre. Clique para ligar de novo ao canvas da composição",
                       ImVec2(0.0f, 0.0f), cfg.linkCanvas ? IconButton_Active : 0))
            cfg.linkCanvas = !cfg.linkCanvas;
        ImGui::EndTable();
    }
    if (cfg.linkCanvas)
        dim("Acompanha o canvas da composição");
    else
        dim("Input livre (a fonte e o mapeamento usam o canvas real)");

    // --- Físico ----------------------------------------------------------------------------------------------
    ImGui::SeparatorText("Físico total (processadora)");
    if (beginPair("##pm_fis")) {
        pairLabels("Largura", "Altura");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        numField("##pm_fis_w", cfg.physW, 1, pm::kMaxDim, 4.0f, "px");
        ImGui::TableSetColumnIndex(1);
        numField("##pm_fis_h", cfg.physH, 1, pm::kMaxDim, 4.0f, "px");
        ImGui::EndTable();
    }
    scaleText(cfg, val);

    // --- Marca d'água ------------------------------------------------------------------------------------------
    ImGui::SeparatorText("Marca d'água");
    if (chip("Monograma BSL##pm_wm_mono", cfg.watermark == pm::Watermark::Monogram, "A marca da BSL Light (padrão)"))
        cfg.watermark = pm::Watermark::Monogram;
    ImGui::SameLine();
    if (chip("Escolher imagem…##pm_wm_img", cfg.watermark == pm::Watermark::Image, "PNG, JPG ou BMP")) {
        if (const auto file = dialogs::openFile(GetActiveWindow(), L"Imagens (*.png;*.jpg;*.jpeg;*.bmp)", L"*.png;*.jpg;*.jpeg;*.bmp")) {
            pm::Image test;
            std::string err;
            if (pm::loadImageFile(*file, test, &err)) {
                cfg.watermark = pm::Watermark::Image;
                cfg.watermarkPath = utf8(*file);
                g.wmKey.clear();
            } else {
                setStatus("Imagem não carregada: " + err, true);
            }
        }
    }
    ImGui::SameLine();
    if (chip("Sem marca##pm_wm_none", cfg.watermark == pm::Watermark::None, "Sem marca d'água no overview e sem logo no padrão"))
        cfg.watermark = pm::Watermark::None;
    if (cfg.watermark == pm::Watermark::Image && !cfg.watermarkPath.empty())
        dim(utf8(pathFromUtf8(cfg.watermarkPath).filename()).c_str());

    // --- Adicionar painel ---------------------------------------------------------------------------------------
    ImGui::SeparatorText("Adicionar painel");
    Draft& d = g.draft;
    if (chip("Em pixels##pm_modo_px", !d.byCabinets, "Largura e altura reais, direto em px"))
        d.byCabinets = false;
    ImGui::SameLine();
    if (chip("Em gabinetes##pm_modo_gab", d.byCabinets, "Colunas × linhas × px do gabinete (como no LEDMapperPro)"))
        d.byCabinets = true;

    dim("Nome do painel");
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", d.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##pm_nome", buf, sizeof(buf)))
            d.name = buf;
    }
    ImGui::PushID("draft");
    if (!d.byCabinets) {
        if (beginPair("##pm_dim")) {
            pairLabels("Largura real", "Altura real");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            numField("##pm_dw", d.w, 1, pm::kMaxDim, 4.0f, "px");
            ImGui::TableSetColumnIndex(1);
            numField("##pm_dh", d.h, 1, pm::kMaxDim, 4.0f, "px");
            ImGui::EndTable();
        }
    } else {
        if (beginPair("##pm_gab")) {
            pairLabels("Colunas", "Linhas");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            numField("##pm_dcols", d.cols, 1, 256, 0.25f);
            ImGui::TableSetColumnIndex(1);
            numField("##pm_drows", d.rows, 1, 256, 0.25f);
            pairLabels("Largura do gabinete", "Altura do gabinete");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            numField("##pm_dcabw", d.cabW, 1, pm::kMaxDim, 1.0f, "px");
            ImGui::TableSetColumnIndex(1);
            numField("##pm_dcabh", d.cabH, 1, pm::kMaxDim, 1.0f, "px");
            ImGui::EndTable();
        }
        const long long tw = std::min<long long>(static_cast<long long>(d.cols) * d.cabW, pm::kMaxDim);
        const long long th = std::min<long long>(static_cast<long long>(d.rows) * d.cabH, pm::kMaxDim);
        pushMono();
        ImGui::TextColored(theme::kTextDim, "= %lld x %lld px", tw, th);
        popMono();
    }
    if (beginPair("##pm_pos")) {
        pairLabels("Posição X (NovaLCT)", "Posição Y (NovaLCT)");
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        numField("##pm_dx", d.x, -pm::kMaxDim, pm::kMaxDim, 1.0f, "px");
        ImGui::TableSetColumnIndex(1);
        numField("##pm_dy", d.y, -pm::kMaxDim, pm::kMaxDim, 1.0f, "px");
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::Spacing();
    ImGui::BeginDisabled(static_cast<int>(cfg.panels.size()) >= pm::kMaxPanels);
    if (primaryButton("Adicionar painel##pm_adicionar")) {
        pm::Panel p = pm::makePanel(d.name.empty() ? pm::nextPanelName(cfg) : d.name, d.x, d.y, d.w, d.h);
        if (d.byCabinets) {
            p.byCabinets = true;
            p.cols = d.cols;
            p.rows = d.rows;
            p.cabW = d.cabW;
            p.cabH = d.cabH;
            pm::applyCabinets(p);
        }
        cfg.panels.push_back(std::move(p));
        g.selected = static_cast<int>(cfg.panels.size()) - 1;
        d.name = pm::nextPanelName(cfg);
    }
    ImGui::EndDisabled();

    // --- Mapeamento ----------------------------------------------------------------------------------------------
    ImGui::SeparatorText("Mapeamento");
    ImGui::BeginDisabled(cfg.panels.empty());
    if (ImGui::Button("Criar mapeamento##pm_criar_mapeamento", ImVec2(-FLT_MIN, 0.0f))) {
        if (ctx.output.screens.empty())
            doCreateMapping(ctx, pm::MapMode::Add, -1);
        else
            g.openCreate = true; // pergunta: substituir a tela ou adicionar uma nova
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Cria ou atualiza uma tela com o físico total e um slice por painel:\n"
                          "entrada = retângulo escalado no canvas, saída = posição física");
    ImGui::BeginDisabled(ctx.output.screens.empty());
    if (ImGui::Button("Importar do mapeamento##pm_importar_mapeamento", ImVec2(-FLT_MIN, 0.0f))) {
        if (cfg.panels.empty()) { // nada a perder: importa direto
            if (const int t = targetScreen(ctx); t >= 0 && pm::importFromScreen(cfg, ctx.output.screens[static_cast<size_t>(t)])) {
                g.selected = -1;
                setStatus(std::to_string(cfg.panels.size()) + " painéis importados da tela \""
                          + ctx.output.screens[static_cast<size_t>(t)].name + "\".");
            } else {
                setStatus("A tela não tem slices.", true);
            }
        } else {
            g.openImport = true;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Lê os slices da tela selecionada no mapeamento: o físico total é a resolução da tela e cada slice vira um painel");

    // --- Exportar -------------------------------------------------------------------------------------------------
    ImGui::SeparatorText("Exportar");
    ImGui::BeginDisabled(cfg.panels.empty());
    if (ImGui::Button("Exportar todos os padrões##pm_exportar_todos", ImVec2(-FLT_MIN, 0.0f)))
        actionExportAll(cfg);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Escolhe uma pasta e grava 1 PNG HD por painel, na resolução física");
    if (ImGui::Button("Exportar overview (PNG)##pm_exportar_overview", ImVec2(-FLT_MIN, 0.0f)))
        actionExportOverview(cfg);
    if (ImGui::Button("Exportar tabela (CSV)##pm_exportar_csv", ImVec2(-FLT_MIN, 0.0f)))
        actionExportCsv(cfg);
    ImGui::EndDisabled();
    if (ImGui::Button("Importar CSV…##pm_importar_csv", ImVec2(-FLT_MIN, 0.0f)))
        actionImportCsv(cfg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Mesmo formato do Exportar tabela: nome;fisico_x;fisico_y;fisico_w;fisico_h;... (as colunas da composição são recalculadas)");

    if (!g.status.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, g.statusWarn ? theme::kWarning : theme::kTextDim);
        ImGui::TextWrapped("%s", g.status.c_str());
        ImGui::PopStyleColor();
    }

    drawMappingModals(ctx);
    endPanel();
}

// ==============================================================================================================
// Overview

namespace {

int hitPanel(const pm::Config& cfg, double px, double py, int preferred)
{
    auto inside = [&](int i) {
        const pm::Panel& p = cfg.panels[static_cast<size_t>(i)];
        return px >= p.x && px < p.x + p.w && py >= p.y && py < p.y + p.h;
    };
    const int n = static_cast<int>(cfg.panels.size());
    if (preferred >= 0 && preferred < n && inside(preferred)) // o selecionado ganha (dá para arrastar um painel coberto)
        return preferred;
    for (int i = n - 1; i >= 0; --i)
        if (inside(i))
            return i;
    return -1;
}

void centeredText(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const std::string& text, float maxWidth)
{
    if (size < dp(6.0f) || text.empty())
        return;
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    if (maxWidth > 0.0f && ts.x > maxWidth) {
        size *= maxWidth / ts.x;
        if (size < dp(6.0f))
            return;
        ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    }
    dl->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), color, text.c_str());
}

} // namespace

void drawPixelMapOverviewPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kPmOverview, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    pm::Config& cfg = ctx.composition.pixelMap;
    const pm::Scale sc = pm::scaleOf(cfg);
    const ImGuiIO& io = ImGui::GetIO();
    const int n = static_cast<int>(cfg.panels.size());

    // --- Barra -------------------------------------------------------------------------------------------------
    iconToggle(ICON_MAGNET, "##pm_ima", g.snapOn,
               "Ímã: encaixa nas bordas, cantos e centros dos outros painéis e da área física (Alt desliga enquanto segura)");
    ImGui::SameLine();
    iconToggle(ICON_GRID, "##pm_grade", g.gridOn, "Grade do overview (20 px e 100 px da composição)");
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + dp(10.0f));
    if (iconButton(ICON_MAXIMIZE, "##pm_ajustar", "Ajustar à vista"))
        g.autoFit = true;
    ImGui::SameLine();
    if (ImGui::Button("100%##pm_100")) {
        g.autoFit = false;
        g.scale = 1.0f;
        g.recenter = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Zoom 100%%: 1 px da composição = 1 px da tela");

    // --- Área da vista ---------------------------------------------------------------------------------------------
    const float captionH = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 vpSize(avail.x, avail.y - captionH);
    if (vpSize.x < dp(60.0f) || vpSize.y < dp(60.0f)) {
        endPanel();
        ImGui::PopStyleVar();
        return;
    }
    ImGui::InvisibleButton("##pm_vista", vpSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    for (ImGuiKey k : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiMod_Alt})
        ImGui::SetItemKeyOwner(k);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    const bool deactivated = ImGui::IsItemDeactivated();
    const ImVec2 vpMin = ImGui::GetItemRectMin();
    const ImVec2 vpMax = ImGui::GetItemRectMax();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImVec2 mouse = io.MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float cw = static_cast<float>(cfg.inW), ch = static_cast<float>(cfg.inH);
    const float margin = dp(28.0f);
    const float fitScale = std::max(0.001f, std::min((vpSize.x - margin * 2.0f) / cw, (vpSize.y - margin * 2.0f) / ch));
    if (g.lastW != cfg.inW || g.lastH != cfg.inH) {
        g.autoFit = true;
        g.lastW = cfg.inW;
        g.lastH = cfg.inH;
    }
    if (g.autoFit) {
        g.scale = fitScale;
        g.offset = ImVec2((vpSize.x - cw * g.scale) * 0.5f, (vpSize.y - ch * g.scale) * 0.5f);
    } else if (g.recenter) {
        g.offset = ImVec2((vpSize.x - cw * g.scale) * 0.5f, (vpSize.y - ch * g.scale) * 0.5f);
    }
    g.recenter = false;

    auto toScreen = [&](double x, double y) {
        return ImVec2(vpMin.x + g.offset.x + static_cast<float>(x) * g.scale, vpMin.y + g.offset.y + static_cast<float>(y) * g.scale);
    };
    auto toContent = [&](ImVec2 m) {
        return Point2{(m.x - vpMin.x - g.offset.x) / g.scale, (m.y - vpMin.y - g.offset.y) / g.scale};
    };
    auto toPhys = [&](ImVec2 m) {
        const Point2 c = toContent(m);
        return Point2{static_cast<float>(c.x / sc.x), static_cast<float>(c.y / sc.y)};
    };

    // Zoom na roda, no ponto do cursor
    if (hovered && io.MouseWheel != 0.0f && g.drag.kind == DragKind::None) {
        const Point2 anchor = toContent(mouse);
        g.scale = std::clamp(g.scale * std::pow(1.12f, io.MouseWheel), 0.02f, 64.0f);
        g.offset = ImVec2(mouse.x - vpMin.x - anchor.x * g.scale, mouse.y - vpMin.y - anchor.y * g.scale);
        g.autoFit = false;
    }

    const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space) && focused;

    // --- Mouse ------------------------------------------------------------------------------------------------------
    if (activated) {
        DragState d;
        d.startMouse = mouse;
        if (io.MouseDown[ImGuiMouseButton_Middle] || (spaceHeld && io.MouseDown[ImGuiMouseButton_Left])) {
            d.kind = DragKind::Pan;
            d.startOffset = g.offset;
        } else {
            const Point2 ph = toPhys(mouse);
            const int hit = hitPanel(cfg, ph.x, ph.y, g.selected);
            g.selected = hit;
            if (hit >= 0) {
                d.kind = DragKind::Move;
                d.index = hit;
                d.startX = cfg.panels[static_cast<size_t>(hit)].x;
                d.startY = cfg.panels[static_cast<size_t>(hit)].y;
            } else {
                d.kind = DragKind::Pan; // arrastar o vazio move a vista
                d.startOffset = g.offset;
            }
        }
        g.drag = d;
    }
    if (g.drag.kind != DragKind::None && !active && !deactivated)
        g.drag = {};
    if (g.drag.kind != DragKind::None && (active || deactivated)) {
        DragState& d = g.drag;
        if (!d.moved && std::hypot(mouse.x - d.startMouse.x, mouse.y - d.startMouse.y) > dp(3.0f))
            d.moved = true;
        d.snap = {};
        if (d.kind == DragKind::Pan) {
            g.offset = ImVec2(d.startOffset.x + (mouse.x - d.startMouse.x), d.startOffset.y + (mouse.y - d.startMouse.y));
            g.autoFit = false;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (d.kind == DragKind::Move && d.moved && d.index >= 0 && d.index < n) {
            // Tudo em px FÍSICOS (NovaLCT): o deslocamento do mouse na tela vira px da composição e depois físicos
            pm::Panel& p = cfg.panels[static_cast<size_t>(d.index)];
            const float dx = std::round((mouse.x - d.startMouse.x) / g.scale / static_cast<float>(sc.x));
            const float dy = std::round((mouse.y - d.startMouse.y) / g.scale / static_cast<float>(sc.y));
            const float x0 = static_cast<float>(d.startX) + dx, y0 = static_cast<float>(d.startY) + dy;
            const float x1 = x0 + static_cast<float>(p.w), y1 = y0 + static_cast<float>(p.h);
            snap::Moving m;
            m.xs = {x0, (x0 + x1) * 0.5f, x1};
            m.ys = {y0, (y0 + y1) * 0.5f, y1};
            m.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}};
            m.extent = {x0, y0, x1, y1};
            snap::Targets t;
            for (int j = 0; j < n; ++j) {
                if (j == d.index)
                    continue;
                const pm::Panel& o = cfg.panels[static_cast<size_t>(j)];
                snap::addRect(t, {static_cast<float>(o.x), static_cast<float>(o.y), static_cast<float>(o.x + o.w),
                                  static_cast<float>(o.y + o.h)});
            }
            snap::addBounds(t, {0.0f, 0.0f, static_cast<float>(cfg.physW), static_cast<float>(cfg.physH)});
            snap::Options o;
            o.enabled = g.snapOn;
            o.altHeld = io.KeyAlt;
            o.thresholdPx = dp(8.0f);
            o.zoom = g.scale * static_cast<float>(std::min(sc.x, sc.y));
            const snap::Result r = snap::snapMoving(m, t, o);
            p.x = std::clamp(static_cast<int>(std::lround(x0 + r.dx)), -pm::kMaxDim, pm::kMaxDim);
            p.y = std::clamp(static_cast<int>(std::lround(y0 + r.dy)), -pm::kMaxDim, pm::kMaxDim);
            d.snap = r;
        }
        if (deactivated)
            g.drag = {};
    }

    // --- Teclado (só com o foco na vista) -------------------------------------------------------------------------
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (focused && !io.WantTextInput && !popupOpen && g.drag.kind == DragKind::None) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g.selected = -1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && g.selected >= 0 && g.selected < n) {
            cfg.panels.erase(cfg.panels.begin() + g.selected);
            g.selected = std::min(g.selected, static_cast<int>(cfg.panels.size()) - 1);
        } else if (g.selected >= 0 && g.selected < n && !io.KeyCtrl && !io.KeyAlt) {
            const int step = io.KeyShift ? 10 : 1;
            int dx = 0, dy = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dx -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) dy -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) dy += step;
            pm::Panel& p = cfg.panels[static_cast<size_t>(g.selected)];
            p.x = std::clamp(p.x + dx, -pm::kMaxDim, pm::kMaxDim);
            p.y = std::clamp(p.y + dy, -pm::kMaxDim, pm::kMaxDim);
        }
    }

    // ============================================================================================================
    // Desenho
    dl->PushClipRect(vpMin, vpMax, true);
    dl->AddRectFilled(vpMin, vpMax, colv(theme::kField));

    const ImVec2 ia = toScreen(0.0, 0.0), ib = toScreen(cw, ch);
    dl->AddRectFilled(ia, ib, IM_COL32(5, 5, 5, 255));

    if (g.gridOn) {
        const float x0 = std::max(ia.x, vpMin.x), x1 = std::min(ib.x, vpMax.x);
        const float y0 = std::max(ia.y, vpMin.y), y1 = std::min(ib.y, vpMax.y);
        for (const auto& [step, alpha] : {std::pair<int, float>{20, 0.05f}, std::pair<int, float>{100, 0.15f}}) {
            if (static_cast<float>(step) * g.scale < dp(4.0f))
                continue;
            const ImU32 gc = col32(pm::kPaletteRgb[0], alpha);
            for (int c = 0; static_cast<float>(c) <= cw; c += step) {
                const float x = ia.x + static_cast<float>(c) * g.scale;
                if (x >= x0 && x <= x1)
                    dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), gc);
            }
            for (int c = 0; static_cast<float>(c) <= ch; c += step) {
                const float y = ia.y + static_cast<float>(c) * g.scale;
                if (y >= y0 && y <= y1)
                    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), gc);
            }
        }
    }

    // Marca d'água no centro (10%, como no HTML)
    if (const Texture* wm = watermarkTexture(cfg); wm && wm->height > 0) {
        const float mw = std::min(cw * 0.4f, 400.0f);
        const float mh = mw / (static_cast<float>(wm->width) / static_cast<float>(wm->height));
        dl->AddImage(static_cast<ImTextureID>(wm->id), toScreen((cw - mw) * 0.5f, (ch - mh) * 0.5f),
                     toScreen((cw + mw) * 0.5f, (ch + mh) * 0.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 26));
    }

    const float line1 = std::max(1.0f, dp(1.0f));
    const float line2 = std::max(1.5f, dp(2.0f));
    ImFont* bold = fonts().bold;
    ImFont* mono = fonts().mono;
    int hoverIdx = -1;
    if (hovered && g.drag.kind == DragKind::None) {
        const Point2 ph = toPhys(mouse);
        hoverIdx = hitPanel(cfg, ph.x, ph.y, g.selected);
    }
    for (int i = 0; i < n; ++i) {
        const pm::Panel& p = cfg.panels[static_cast<size_t>(i)];
        const pm::RectD r = pm::scaledRect(cfg, p);
        const bool overlap = g.val.overlap.size() > static_cast<size_t>(i) && g.val.overlap[static_cast<size_t>(i)];
        const bool outside = g.val.outside.size() > static_cast<size_t>(i) && g.val.outside[static_cast<size_t>(i)];
        std::uint32_t border = pm::panelColor(p, i);
        float fillA = pm::panelFill(p, i);
        if (outside) {
            border = pm::kInvalidRgb;
            fillA = 0.28f;
        } else if (overlap) {
            border = pm::kAmberRgb;
            fillA = 0.28f;
        }
        const ImVec2 a = toScreen(r.x, r.y), b = toScreen(r.x + r.w, r.y + r.h);
        dl->AddRectFilled(a, b, col32(border, fillA));
        if (outside)
            drawDashedRect(dl, a, b, col32(border), dp(6.0f), dp(4.0f));
        else
            dl->AddRect(a, b, col32(border), 0.0f, 0, line2);
        qa::noteRect("pixelmap:painel:" + std::to_string(i), a.x, a.y, b.x, b.y);

        const double fs = std::min({r.w * 0.08, r.h * 0.25, 30.0});
        const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float fpx = static_cast<float>(fs) * g.scale;
        const float maxW = (b.x - a.x) * 0.96f;
        dl->PushClipRect(ImVec2(std::max(a.x, vpMin.x), std::max(a.y, vpMin.y)), ImVec2(std::min(b.x, vpMax.x), std::min(b.y, vpMax.y)), true);
        centeredText(dl, bold, fpx * 1.0f, ImVec2(mid.x, mid.y - fpx * 0.6f), IM_COL32(255, 255, 255, 255), pm::upperUtf8(p.name), maxW);
        const pm::RectD rr{0, 0, std::round(r.w), std::round(r.h)};
        char res[48];
        std::snprintf(res, sizeof(res), "%dx%d", static_cast<int>(rr.w), static_cast<int>(rr.h));
        centeredText(dl, mono, fpx * 0.6f, ImVec2(mid.x, mid.y + fpx * 0.6f), col32(border), res, maxW);
        dl->PopClipRect();

        if (overlap || outside) {
            const ImVec2 tc(b.x - dp(10.0f), a.y + dp(10.0f));
            drawIcon(dl, ICON_ALERT, tc, col32(border), 1.0f);
        }
        if (i == g.selected) {
            const float o = dp(3.0f);
            drawDashedRect(dl, ImVec2(a.x - o, a.y - o), ImVec2(b.x + o, b.y + o), colv(theme::kTextStrong), dp(6.0f), dp(4.0f));
        } else if (i == hoverIdx) {
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 120), 0.0f, 0, line1);
        }
    }
    dl->AddRect(ia, ib, colv(theme::kBorder), 0.0f, 0, line1);
    qa::noteRect("pixelmap:canvas", ia.x, ia.y, ib.x, ib.y);
    qa::noteRect("pixelmap:vista", vpMin.x, vpMin.y, vpMax.x, vpMax.y);

    // Linhas-guia do ímã (físicas -> composição)
    if (g.drag.kind == DragKind::Move) {
        const float gl = std::max(1.0f, dp(1.0f));
        const float ext = dp(28.0f);
        for (const snap::Guide& gd : g.drag.snap.guides) {
            ImVec2 p0 = gd.vertical ? toScreen(gd.pos * sc.x, gd.lo * sc.y) : toScreen(gd.lo * sc.x, gd.pos * sc.y);
            ImVec2 p1 = gd.vertical ? toScreen(gd.pos * sc.x, gd.hi * sc.y) : toScreen(gd.hi * sc.x, gd.pos * sc.y);
            if (gd.vertical) {
                p0.y -= ext;
                p1.y += ext;
            } else {
                p0.x -= ext;
                p1.x += ext;
            }
            dl->AddLine(p0, p1, colv(theme::kAccent), gl);
        }
        for (const Point2& mk : g.drag.snap.marks) {
            const ImVec2 p = toScreen(mk.x * sc.x, mk.y * sc.y);
            dl->AddCircle(p, dp(6.5f), colv(theme::kAccent), 0, std::max(1.0f, dp(1.5f)));
            dl->AddLine(ImVec2(p.x - dp(9.0f), p.y), ImVec2(p.x + dp(9.0f), p.y), colv(theme::kAccent), gl);
            dl->AddLine(ImVec2(p.x, p.y - dp(9.0f)), ImVec2(p.x, p.y + dp(9.0f)), colv(theme::kAccent), gl);
        }
    }

    // Aviso discreto: escala não inteira
    if (g.val.scaleWarn) {
        char msg[200];
        std::snprintf(msg, sizeof(msg), "Escala %.3f × %.3f (não inteira): 1 px físico ≠ 1 px da composição", sc.x, sc.y);
        const ImVec2 sz = ImGui::CalcTextSize(msg);
        const ImVec2 pos(vpMin.x + dp(10.0f), vpMax.y - sz.y - dp(10.0f));
        dl->AddRectFilled(ImVec2(pos.x - dp(6.0f), pos.y - dp(3.0f)), ImVec2(pos.x + sz.x + dp(6.0f), pos.y + sz.y + dp(3.0f)),
                          colv(theme::kBackground, 0.8f), rounding());
        dl->AddText(pos, colv(theme::kWarning), msg);
        qa::noteRect("pixelmap:aviso-escala", pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);
    }
    dl->PopClipRect();

    // Dica do painel sob o mouse
    if (hoverIdx >= 0 && hoverIdx < n) {
        const pm::Panel& p = cfg.panels[static_cast<size_t>(hoverIdx)];
        const RectI r = pm::scaledRectRounded(cfg, p);
        ImGui::SetTooltip("%s\nFísico: X %d  Y %d  %d x %d px\nComposição: X %d  Y %d  %d x %d px", p.name.c_str(), p.x, p.y, p.w,
                          p.h, r.x, r.y, r.w, r.h);
    }

    // --- Legenda ---------------------------------------------------------------------------------------------------
    {
        char buf[320];
        std::snprintf(buf, sizeof(buf), "Input %dx%d  |  Físico %dx%d  |  %d painel(is)  |  %d%%", cfg.inW, cfg.inH, cfg.physW,
                      cfg.physH, n, static_cast<int>(std::lround(g.scale * 100.0f)));
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%s", buf);
        popMono();
    }
    endPanel();
    ImGui::PopStyleVar();
}

// ==============================================================================================================
// Tabela

namespace {

// Cor do painel: clique troca pela próxima da paleta do HTML; o menu da linha tem as 4 + automática + qualquer cor
void colorSwatch(pm::Panel& p, int index)
{
    const float side = ImGui::GetFrameHeight() * 0.62f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##pm_cor", ImVec2(side + dp(6.0f), ImGui::GetFrameHeight()));
    const std::uint32_t c = pm::panelColor(p, index);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = pos.y + ImGui::GetFrameHeight() * 0.5f;
    dl->AddRectFilled(ImVec2(pos.x + dp(3.0f), cy - side * 0.5f), ImVec2(pos.x + dp(3.0f) + side, cy + side * 0.5f), col32(c), dp(3.0f));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cor do painel: clique para trocar pela próxima da paleta; use ⋯ para escolher outra");
    if (ImGui::IsItemClicked()) {
        int k = 0;
        for (int i = 0; i < 4; ++i)
            if (pm::kPaletteRgb[i] == c)
                k = i;
        p.color = pm::kPaletteRgb[(k + 1) % 4];
    }
}

void rowMenu(pm::Panel& p)
{
    if (iconButton(ICON_ELLIPSIS, "##pm_mais", "Gabinetes e cor do painel"))
        ImGui::OpenPopup("##pm_mais_popup");
    if (ImGui::BeginPopup("##pm_mais_popup")) {
        ImGui::SeparatorText("Definição");
        bool cab = p.byCabinets;
        if (ImGui::Checkbox("Por gabinetes##pm_gab_on", &cab)) {
            p.byCabinets = cab;
            if (cab) { // começa com 1 gabinete do tamanho atual
                p.cols = 1;
                p.rows = 1;
                p.cabW = p.w;
                p.cabH = p.h;
            }
        }
        if (p.byCabinets) {
            if (ImGui::BeginTable("##pm_gab_t", 2, ImGuiTableFlags_SizingStretchSame)) {
                pairLabels("Colunas", "Linhas");
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                numField("##pm_cols", p.cols, 1, 256, 0.25f);
                ImGui::TableSetColumnIndex(1);
                numField("##pm_rows", p.rows, 1, 256, 0.25f);
                pairLabels("Larg. gabinete", "Alt. gabinete");
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                numField("##pm_cabw", p.cabW, 1, pm::kMaxDim, 1.0f, "px");
                ImGui::TableSetColumnIndex(1);
                numField("##pm_cabh", p.cabH, 1, pm::kMaxDim, 1.0f, "px");
                ImGui::EndTable();
            }
        }
        ImGui::SeparatorText("Cor");
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            if (i > 0)
                ImGui::SameLine();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float side = ImGui::GetFrameHeight();
            if (ImGui::InvisibleButton("##pm_pal", ImVec2(side, side)))
                p.color = pm::kPaletteRgb[i];
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), col32(pm::kPaletteRgb[i]), dp(3.0f));
            if (p.color == pm::kPaletteRgb[i])
                ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + side, pos.y + side), colv(theme::kTextStrong), dp(3.0f), 0, 2.0f);
            ImGui::PopID();
        }
        float rgbf[3] = {static_cast<float>((p.color >> 16) & 0xFF) / 255.0f, static_cast<float>((p.color >> 8) & 0xFF) / 255.0f,
                         static_cast<float>(p.color & 0xFF) / 255.0f};
        if (p.color == 0) {
            rgbf[0] = rgbf[1] = rgbf[2] = 0.5f;
        }
        if (ImGui::ColorEdit3("##pm_cor_rgb", rgbf, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            const auto b = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            p.color = std::max<std::uint32_t>(1u, (b(rgbf[0]) << 16) | (b(rgbf[1]) << 8) | b(rgbf[2]));
        }
        ImGui::SameLine();
        if (ImGui::Button("Automática##pm_cor_auto"))
            p.color = 0;
        ImGui::EndPopup();
    }
}

} // namespace

void drawPixelMapTablePanel(PanelContext& ctx)
{
    beginPanel(panel::kPmTable);
    pm::Config& cfg = ctx.composition.pixelMap;
    const int n = static_cast<int>(cfg.panels.size());

    if (n == 0) {
        ImGui::TextColored(theme::kTextDim, "Nenhum painel. Adicione na Configuração, importe um CSV ou use \"Importar do mapeamento\".");
        endPanel();
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY
                                  | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    const float footer = ImGui::GetTextLineHeightWithSpacing() + dp(4.0f);
    if (ImGui::BeginTable("##pm_tabela", 10, flags, ImVec2(0.0f, std::max(dp(60.0f), ImGui::GetContentRegionAvail().y - footer)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##cor", ImGuiTableColumnFlags_WidthFixed, dp(30.0f));
        ImGui::TableSetupColumn("Identificação", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Físico X", ImGuiTableColumnFlags_WidthFixed, dp(76.0f));
        ImGui::TableSetupColumn("Físico Y", ImGuiTableColumnFlags_WidthFixed, dp(76.0f));
        ImGui::TableSetupColumn("Largura", ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        ImGui::TableSetupColumn("Altura", ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        ImGui::TableSetupColumn("Proporção na composição", ImGuiTableColumnFlags_WidthFixed, dp(190.0f));
        ImGui::TableSetupColumn("Padrão", ImGuiTableColumnFlags_WidthFixed, dp(128.0f));
        ImGui::TableSetupColumn("##mais", ImGuiTableColumnFlags_WidthFixed, dp(34.0f));
        ImGui::TableSetupColumn("##rem", ImGuiTableColumnFlags_WidthFixed, dp(34.0f));
        ImGui::TableHeadersRow();

        const float frameH = ImGui::GetFrameHeight();
        const float rowH = std::max(frameH + dp(8.0f), ImGui::GetTextLineHeight() * 2.0f + dp(8.0f));
        const float off = (rowH - frameH) * 0.5f;
        int removeIdx = -1;
        for (int i = 0; i < n; ++i) {
            pm::Panel& p = cfg.panels[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow(0, rowH);
            const float rowY = ImGui::GetCursorScreenPos().y;
            const bool overlap = g.val.overlap.size() > static_cast<size_t>(i) && g.val.overlap[static_cast<size_t>(i)];
            const bool outside = g.val.outside.size() > static_cast<size_t>(i) && g.val.outside[static_cast<size_t>(i)];
            if (i == g.selected)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, colv(theme::kAccent, 0.16f));

            auto lower = [&]() { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + off); };

            ImGui::TableSetColumnIndex(0);
            lower();
            colorSwatch(p, i);

            ImGui::TableSetColumnIndex(1);
            lower();
            {
                const bool warn = overlap || outside;
                const float iconW = warn ? ImGui::GetFrameHeight() : 0.0f;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", p.name.c_str());
                ImGui::SetNextItemWidth(-FLT_MIN - iconW);
                if (ImGui::InputText("##pm_nome", buf, sizeof(buf)))
                    p.name = buf;
                if (warn) {
                    ImGui::SameLine(0.0f, dp(4.0f));
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight() - dp(4.0f), ImGui::GetFrameHeight()));
                    drawIcon(ImGui::GetWindowDrawList(), ICON_ALERT, ImVec2(pos.x + ImGui::GetFrameHeight() * 0.4f, pos.y + ImGui::GetFrameHeight() * 0.5f),
                             col32(outside ? pm::kInvalidRgb : pm::kAmberRgb));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(outside ? "Fora da área física total" : "Sobrepõe outro painel");
                }
            }
            ImGui::TableSetColumnIndex(2);
            lower();
            numField("##pm_x", p.x, -pm::kMaxDim, pm::kMaxDim, 1.0f);
            ImGui::TableSetColumnIndex(3);
            lower();
            numField("##pm_y", p.y, -pm::kMaxDim, pm::kMaxDim, 1.0f);
            ImGui::TableSetColumnIndex(4);
            lower();
            ImGui::BeginDisabled(p.byCabinets);
            numField("##pm_w", p.w, 1, pm::kMaxDim, 1.0f);
            ImGui::EndDisabled();
            if (p.byCabinets && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Definido por gabinetes: %d colunas × %d px", p.cols, p.cabW);
            ImGui::TableSetColumnIndex(5);
            lower();
            ImGui::BeginDisabled(p.byCabinets);
            numField("##pm_h", p.h, 1, pm::kMaxDim, 1.0f);
            ImGui::EndDisabled();
            if (p.byCabinets && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Definido por gabinetes: %d linhas × %d px", p.rows, p.cabH);

            ImGui::TableSetColumnIndex(6);
            {
                const RectI r = pm::scaledRectRounded(cfg, p);
                const std::uint32_t c = pm::panelColor(p, i);
                char big[64], small[64];
                std::snprintf(big, sizeof(big), "%d x %d px", r.w, r.h);
                std::snprintf(small, sizeof(small), "X: %d | Y: %d", r.x, r.y);
                ImGui::PushFont(fonts().bold, fonts().bold->LegacySize * 1.1f);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col32(c)), "%s", big);
                ImGui::PopFont();
                pushMono();
                ImGui::TextColored(theme::kTextDim, "%s", small);
                popMono();
            }
            ImGui::TableSetColumnIndex(7);
            lower();
            if (ImGui::Button("Gerar padrão HD##pm_gerar", ImVec2(-FLT_MIN, 0.0f)))
                actionGeneratePattern(cfg, p);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Exporta um PNG de %d x %d px (a resolução física) para este painel", p.w, p.h);
            ImGui::TableSetColumnIndex(8);
            lower();
            rowMenu(p);
            ImGui::TableSetColumnIndex(9);
            lower();
            if (iconButton(ICON_TRASH, "##pm_remover", "Remover o painel"))
                removeIdx = i;

            // Clique em qualquer parte da linha seleciona o painel
            const ImVec2 wmin = ImGui::GetWindowPos();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)
                && ImGui::IsMouseHoveringRect(ImVec2(wmin.x, rowY), ImVec2(wmin.x + ImGui::GetWindowWidth(), rowY + rowH), false)
                && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                g.selected = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removeIdx >= 0 && removeIdx < static_cast<int>(cfg.panels.size())) {
            cfg.panels.erase(cfg.panels.begin() + removeIdx);
            if (g.selected == removeIdx)
                g.selected = -1;
            else if (g.selected > removeIdx)
                --g.selected;
        }
    }

    // Rodapé: totais e validações
    {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%d painel(is)  |  %d sobreposto(s)  |  %d fora da área física", n, g.val.overlapCount,
                      g.val.outsideCount);
        pushMono();
        ImGui::TextColored(g.val.overlapCount || g.val.outsideCount ? theme::kWarning : theme::kTextDim, "%s", buf);
        popMono();
    }
    endPanel();
}

} // namespace bsl::ui
