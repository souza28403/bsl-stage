#include "ui/panels/PluginWizard.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "fx/EffectRegistry.h"
#include "fx/IsfDoc.h"
#include "fx/IsfLibrary.h"
#include "io/FileDialogs.h"
#include "plugin/PluginFiles.h"
#include "plugin/PluginPackage.h"
#include "plugin/PluginTemplates.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

namespace bsl::ui {

namespace fs = std::filesystem;
using namespace bsl::plugin;

namespace {

struct Row
{
    ParamSpec spec;
    bool fromTemplate = false;
    char label[80] = {};
    char options[200] = {};
};

struct Wizard
{
    bool open = false;
    char name[96] = "Meu plugin";
    int kind = 0;
    int tmpl = 0;
    int category = 0;
    char customCategory[64] = {};
    char author[96] = {};
    char description[200] = {};
    int folder = 0;
    std::vector<Row> rows;
    int newKind = 0;
    bool openEditor = true;
    bool showCode = false;
    std::string error;
    bool created = false;
    fs::path createdFile;
    std::string createdId;
    std::string createdName;
};
Wizard g_wiz;

struct ImportWindow
{
    bool open = false;
    fs::path file;
    PackageInfo info;
    PackageResult check;
    bool checked = false;
    bool installed = false;
    fs::path installedFolder;
    std::string message;
};
ImportWindow g_import;

struct ExportWindow
{
    bool open = false;
    fs::path shader;
    std::string typeId;
    char name[96] = {};
    char author[96] = {};
    char version[24] = "1.0.0";
    int license = 0;
    char customLicense[96] = {};
    char description[300] = {};
    std::string message;
    bool messageError = false;
    fs::path result;
};
ExportWindow g_export;

PluginUiInfo g_info;

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

void copyTo(char* dst, size_t cap, const std::string& s)
{
    std::snprintf(dst, cap, "%s", s.c_str());
}

const char* kKindNames[] = {"Número", "Cor", "Liga/desliga", "Ponto 2D", "Imagem", "Botão (evento)", "Áudio (espectro)", "Lista de opções"};
const ParamKind kKindValues[] = {ParamKind::Float, ParamKind::Color, ParamKind::Bool, ParamKind::Point2D,
                                 ParamKind::Image, ParamKind::Event, ParamKind::Audio, ParamKind::Long};

const char* kindName(ParamKind k)
{
    for (size_t i = 0; i < std::size(kKindValues); ++i)
        if (kKindValues[i] == k)
            return kKindNames[i];
    return "";
}

Kind currentKind()
{
    return g_wiz.kind == 0 ? Kind::Effect : Kind::Generator;
}

Template currentTemplate()
{
    return static_cast<Template>(g_wiz.tmpl);
}

std::vector<std::pair<std::string, fs::path>> folderChoices(PanelContext& ctx)
{
    std::vector<std::pair<std::string, fs::path>> v;
    v.push_back({"Meus plugins", paths::pluginsDir()});
    for (const std::string& f : ctx.settings.isfFolders) {
        const fs::path p = fromUtf8(f);
        std::string label = paths::toUtf8(p.filename());
        v.push_back({label.empty() ? f : label, p});
    }
    return v;
}

void applyTemplate()
{
    std::vector<Row> keep;
    for (Row& r : g_wiz.rows)
        if (!r.fromTemplate)
            keep.push_back(std::move(r));
    g_wiz.rows.clear();
    if (!templateAvailable(currentTemplate(), currentKind()))
        g_wiz.tmpl = 0;
    for (ParamSpec& p : defaultParams(currentTemplate(), currentKind())) {
        Row r;
        r.fromTemplate = true;
        copyTo(r.label, sizeof(r.label), p.label);
        std::string opts;
        for (size_t i = 0; i < p.options.size(); ++i)
            opts += (i ? ", " : "") + p.options[i];
        copyTo(r.options, sizeof(r.options), opts);
        r.spec = std::move(p);
        g_wiz.rows.push_back(std::move(r));
    }
    for (Row& r : keep)
        g_wiz.rows.push_back(std::move(r));
}

void resetWizard()
{
    g_wiz = Wizard{};
    applyTemplate();
}

std::vector<std::string> optionsFrom(const char* text)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        size_t a = 0, b = cur.size();
        while (a < b && std::isspace(static_cast<unsigned char>(cur[a])))
            ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1])))
            --b;
        if (b > a)
            out.push_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (const char* p = text; *p; ++p) {
        if (*p == ',')
            flush();
        else
            cur += *p;
    }
    flush();
    return out;
}

PluginSpec buildSpec()
{
    PluginSpec spec;
    spec.name = g_wiz.name;
    spec.kind = currentKind();
    spec.tmpl = currentTemplate();
    const auto& cats = suggestedCategories();
    spec.category = g_wiz.category >= 0 && g_wiz.category < static_cast<int>(cats.size()) ? cats[static_cast<size_t>(g_wiz.category)]
                                                                                       : std::string(g_wiz.customCategory);
    spec.author = g_wiz.author;
    spec.description = g_wiz.description;
    std::vector<std::string> taken;
    for (const Row& r : g_wiz.rows)
        if (r.fromTemplate)
            taken.push_back(r.spec.name);
    for (const Row& r : g_wiz.rows) {
        ParamSpec p = r.spec;
        p.label = r.label;
        if (!r.fromTemplate) {
            p.name = identFromLabel(r.label, taken);
            taken.push_back(p.name);
        }
        if (p.kind == ParamKind::Long)
            p.options = optionsFrom(r.options);
        spec.params.push_back(std::move(p));
    }
    return spec;
}

fs::path vsCodePath()
{
    std::vector<fs::path> candidates;
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0)
        candidates.push_back(fs::path(buf) / L"Programs" / L"Microsoft VS Code" / L"Code.exe");
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH) > 0)
        candidates.push_back(fs::path(buf) / L"Microsoft VS Code" / L"Code.exe");
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec))
            return c;
    }
    return {};
}

bool launchEditor(const fs::path& file)
{
    const std::wstring quoted = L"\"" + file.wstring() + L"\"";
    if (const fs::path code = vsCodePath(); !code.empty())
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", code.c_str(), quoted.c_str(), nullptr, SW_SHOWNORMAL)) > 32)
            return true;
    wchar_t exe[MAX_PATH] = {};
    if (reinterpret_cast<INT_PTR>(FindExecutableW(file.c_str(), nullptr, exe)) > 32)
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32)
            return true;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
}

fs::path pickOpenPackage()
{
    if (qa::enabled()) {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetEnvironmentVariableW(L"BSLSTAGE_QA_PLUGIN_FILE", buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0 && n < std::size(buf))
            return fs::path(std::wstring(buf, n));
        return {};
    }
    if (auto f = dialogs::openFile(GetActiveWindow(), L"Plugin do BSL Stage (*.bslplugin)", L"*.bslplugin"))
        return *f;
    return {};
}

fs::path pickSavePackage(const std::string& defaultName)
{
    if (qa::enabled()) {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetEnvironmentVariableW(L"BSLSTAGE_QA_PLUGIN_EXPORT", buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0 && n < std::size(buf))
            return fs::path(std::wstring(buf, n));
        return {};
    }
    const std::u8string u = fromUtf8(defaultName).u8string();
    if (auto f = dialogs::saveFile(GetActiveWindow(), L"Exportar plugin", L"Plugin do BSL Stage (*.bslplugin)", L"*.bslplugin",
                                   fs::path(u).wstring(), L"bslplugin"))
        return *f;
    return {};
}

void drawParamRow(size_t i, Row& r, bool& remove)
{
    const std::string n = std::to_string(i + 1);
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::SetNextItemWidth(dp(210.0f));
    ImGui::InputTextWithHint(("##plug_p" + n + "_nome").c_str(), "Nome que aparece na interface", r.label, sizeof(r.label));
    ImGui::SameLine();
    ImGui::TextColored(theme::kTextDim, "%s", kindName(r.spec.kind));
    ImGui::SameLine();
    const bool locked = r.fromTemplate && r.spec.kind == ParamKind::Audio && currentTemplate() == Template::AudioReactive;
    ImGui::BeginDisabled(locked);
    if (iconButton(ICON_X, ("##plug_p" + n + "_remover").c_str(), locked ? "O modelo de áudio precisa deste parâmetro" : "Remover este parâmetro"))
        remove = true;
    ImGui::EndDisabled();

    ImGui::Indent(dp(14.0f));
    switch (r.spec.kind) {
    case ParamKind::Float: {
        float lo = static_cast<float>(r.spec.minV), hi = static_cast<float>(r.spec.maxV), def = static_cast<float>(r.spec.defV);
        ImGui::SetNextItemWidth(dp(80.0f));
        if (ImGui::DragFloat(("mín##plug_p" + n + "_min").c_str(), &lo, 0.05f, -1000.0f, 1000.0f, "%.3g"))
            r.spec.minV = lo;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(dp(80.0f));
        if (ImGui::DragFloat(("máx##plug_p" + n + "_max").c_str(), &hi, 0.05f, -1000.0f, 1000.0f, "%.3g"))
            r.spec.maxV = hi;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(dp(80.0f));
        if (ImGui::DragFloat(("padrão##plug_p" + n + "_def").c_str(), &def, 0.05f, -1000.0f, 1000.0f, "%.3g"))
            r.spec.defV = def;
        break;
    }
    case ParamKind::Color: {
        float c[4] = {r.spec.defColor[0], r.spec.defColor[1], r.spec.defColor[2], r.spec.defColor[3]};
        if (ImGui::ColorEdit4(("cor padrão##plug_p" + n + "_cor").c_str(), c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
            for (int k = 0; k < 4; ++k)
                r.spec.defColor[k] = c[k];
        break;
    }
    case ParamKind::Bool:
        ImGui::Checkbox(("ligado por padrão##plug_p" + n + "_bool").c_str(), &r.spec.defBool);
        break;
    case ParamKind::Point2D:
        ImGui::SetNextItemWidth(dp(180.0f));
        ImGui::DragFloat2(("padrão (px em 1920x1080)##plug_p" + n + "_ponto").c_str(), r.spec.defPoint, 1.0f, 0.0f, 1920.0f, "%.0f");
        break;
    case ParamKind::Image:
        if (ImGui::Button(("Escolher imagem…##plug_p" + n + "_imagem").c_str())) {
            if (!qa::enabled())
                if (auto f = dialogs::openFile(GetActiveWindow(), L"Imagens", L"*.png;*.jpg;*.jpeg;*.bmp;*.tga"))
                    r.spec.imageFile = paths::toUtf8(*f);
        }
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "%s",
                           r.spec.imageFile.empty() ? "nenhuma (será criada uma imagem de exemplo)" : paths::toUtf8(fromUtf8(r.spec.imageFile).filename()).c_str());
        break;
    case ParamKind::Event:
        ImGui::TextColored(theme::kTextDim, "Botão: o shader vê \"verdadeiro\" por um quadro quando o usuário clica.");
        break;
    case ParamKind::Audio:
        ImGui::TextColored(theme::kTextDim, "Espectro do som do PC ou do microfone (32 faixas, grave à esquerda).");
        break;
    case ParamKind::Long:
        ImGui::SetNextItemWidth(dp(300.0f));
        ImGui::InputTextWithHint(("opções##plug_p" + n + "_opcoes").c_str(), "Opção A, Opção B, Opção C", r.options, sizeof(r.options));
        break;
    }
    if (!r.fromTemplate) {
        std::vector<std::string> taken;
        for (size_t k = 0; k < i; ++k)
            taken.push_back(g_wiz.rows[k].fromTemplate ? g_wiz.rows[k].spec.name : identFromLabel(g_wiz.rows[k].label, taken));
        ImGui::TextColored(theme::kTextDim, "variável no código: %s", identFromLabel(r.label, taken).c_str());
    } else {
        ImGui::TextColored(theme::kTextDim, "variável no código: %s", r.spec.name.c_str());
    }
    ImGui::Unindent(dp(14.0f));
    ImGui::Spacing();
    ImGui::PopID();
}

void drawCreated(PanelContext& ctx)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kSuccess, "Plugin criado!");
    ImGui::TextWrapped("\"%s\" já está na aba Efeitos, em ISF > %s. Arraste para um clip ou camada.", g_wiz.createdName.c_str(),
                       "a categoria que você escolheu");
    ImGui::TextColored(theme::kTextDim, "Arquivo: %s", paths::toUtf8(g_wiz.createdFile).c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Para mudar o que ele faz, abra o arquivo no editor, altere o código e salve: o efeito recompila sozinho, sem fechar o "
                       "programa e sem perder os valores. Se houver um erro, o painel do efeito mostra a linha e a versão anterior continua rodando.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(ICON_FILE_CODE "  Abrir no editor##plug_abrir_editor"))
        openPluginInEditor(g_wiz.createdFile);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FOLDER_OPEN "  Mostrar na pasta##plug_mostrar_pasta"))
        showPluginInFolder(g_wiz.createdFile);
    ImGui::SameLine();
    if (ImGui::Button(ICON_SPARKLES "  Ver na aba Efeitos##plug_ver_efeitos")) {
        requestFocusTab("efeitos");
    }
    ImGui::Spacing();
    if (ImGui::Button("Criar outro##plug_outro")) {
        resetWizard();
    }
    ImGui::SameLine();
    if (ImGui::Button("Fechar##plug_fechar"))
        g_wiz.open = false;
    (void)ctx;
}

void drawWizard(PanelContext& ctx)
{
    if (!g_wiz.open)
        return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(700.0f), dp(780.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Novo plugin###janela_novo_plugin", &g_wiz.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_wiz.open = false;

    if (g_wiz.created) {
        drawCreated(ctx);
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Crie seu próprio efeito ou gerador. O assistente escreve um arquivo de shader (ISF) pronto, que você pode editar "
                       "depois. Nada é enviado para fora do seu computador.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    PluginSpec spec = buildSpec();
    const std::string problem = validateSpec(spec);
    ImGui::BeginChild("##plug_corpo", ImVec2(0.0f, -dp(96.0f)), ImGuiChildFlags_None);
    const float labelW = dp(120.0f);
    ImGui::AlignTextToFramePadding();
    dimLabel("Nome");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##plug_nome", "Ex.: Onda colorida", g_wiz.name, sizeof(g_wiz.name));

    ImGui::AlignTextToFramePadding();
    dimLabel("Tipo");
    ImGui::SameLine(labelW);
    int kind = g_wiz.kind;
    ImGui::RadioButton("Efeito (muda a imagem que entra)##plug_tipo_efeito", &kind, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Gerador (cria a imagem do zero)##plug_tipo_gerador", &kind, 1);
    if (kind != g_wiz.kind) {
        g_wiz.kind = kind;
        applyTemplate();
    }

    ImGui::AlignTextToFramePadding();
    dimLabel("Categoria");
    ImGui::SameLine(labelW);
    {
        const auto& cats = suggestedCategories();
        const std::string preview = g_wiz.category >= 0 && g_wiz.category < static_cast<int>(cats.size())
                                        ? cats[static_cast<size_t>(g_wiz.category)]
                                        : (g_wiz.customCategory[0] ? std::string(g_wiz.customCategory) : std::string("Outra…"));
        ImGui::SetNextItemWidth(dp(220.0f));
        if (beginCombo("##plug_categoria", preview.c_str())) {
            for (size_t i = 0; i < cats.size(); ++i)
                if (ImGui::Selectable((cats[i] + "##plug_cat_" + std::to_string(i)).c_str(), g_wiz.category == static_cast<int>(i)))
                    g_wiz.category = static_cast<int>(i);
            if (ImGui::Selectable("Outra (digitar)…##plug_cat_outra", g_wiz.category < 0))
                g_wiz.category = -1;
            ImGui::EndCombo();
        }
        if (g_wiz.category < 0) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dp(220.0f));
            ImGui::InputTextWithHint("##plug_categoria_texto", "Nome da categoria", g_wiz.customCategory, sizeof(g_wiz.customCategory));
        }
    }

    ImGui::AlignTextToFramePadding();
    dimLabel("Autor (opcional)");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(dp(220.0f));
    ImGui::InputTextWithHint("##plug_autor", "Seu nome", g_wiz.author, sizeof(g_wiz.author));

    const auto folders = folderChoices(ctx);
    g_wiz.folder = std::clamp(g_wiz.folder, 0, static_cast<int>(folders.size()) - 1);
    ImGui::AlignTextToFramePadding();
    dimLabel("Salvar em");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(dp(220.0f));
    if (beginCombo("##plug_pasta", folders[static_cast<size_t>(g_wiz.folder)].first.c_str())) {
        for (size_t i = 0; i < folders.size(); ++i)
            if (ImGui::Selectable((folders[i].first + "##plug_pasta_" + std::to_string(i)).c_str(), g_wiz.folder == static_cast<int>(i)))
                g_wiz.folder = static_cast<int>(i);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    pushMono();
    ImGui::TextColored(theme::kTextDim, "%s", paths::toUtf8(folders[static_cast<size_t>(g_wiz.folder)].second).c_str());
    popMono();

    ImGui::SeparatorText("Modelo");
    for (int t = 0; t < kTemplateCount; ++t) {
        const Template tm = static_cast<Template>(t);
        const bool avail = templateAvailable(tm, currentKind());
        ImGui::BeginDisabled(!avail);
        const std::string label = std::string(templateLabel(tm, currentKind())) + "##plug_modelo_" + std::to_string(t);
        if (ImGui::Selectable(label.c_str(), g_wiz.tmpl == t, 0, ImVec2(dp(190.0f), 0.0f)) && g_wiz.tmpl != t) {
            g_wiz.tmpl = t;
            applyTemplate();
        }
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "%s", avail ? templateHint(tm, currentKind())
                                                         : (currentKind() == Kind::Effect ? "Só para geradores." : "Só para efeitos."));
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Parâmetros (os controles que aparecem no programa)");
    ImGui::SetNextItemWidth(dp(180.0f));
    if (beginCombo("##plug_novo_tipo", kKindNames[g_wiz.newKind])) {
        for (int k = 0; k < static_cast<int>(std::size(kKindNames)); ++k)
            if (ImGui::Selectable((std::string(kKindNames[k]) + "##plug_tipo_param_" + std::to_string(k)).c_str(), g_wiz.newKind == k))
                g_wiz.newKind = k;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_wiz.rows.size() >= 32);
    if (ImGui::Button(ICON_PLUS "  Adicionar parâmetro##plug_adicionar_param")) {
        Row r;
        r.spec.kind = kKindValues[g_wiz.newKind];
        std::snprintf(r.label, sizeof(r.label), "Parâmetro %d", static_cast<int>(g_wiz.rows.size()) + 1);
        switch (r.spec.kind) {
        case ParamKind::Float:
            r.spec.minV = 0.0;
            r.spec.maxV = 1.0;
            r.spec.defV = 0.5;
            break;
        case ParamKind::Long:
            std::snprintf(r.options, sizeof(r.options), "Opção A, Opção B");
            break;
        case ParamKind::Audio:
            std::snprintf(r.label, sizeof(r.label), "Espectro do áudio");
            break;
        case ParamKind::Event:
            std::snprintf(r.label, sizeof(r.label), "Disparo");
            break;
        case ParamKind::Image:
            std::snprintf(r.label, sizeof(r.label), "Imagem");
            break;
        default:
            break;
        }
        g_wiz.rows.push_back(std::move(r));
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    int removeAt = -1;
    for (size_t i = 0; i < g_wiz.rows.size(); ++i) {
        bool remove = false;
        drawParamRow(i, g_wiz.rows[i], remove);
        if (remove)
            removeAt = static_cast<int>(i);
    }
    if (removeAt >= 0)
        g_wiz.rows.erase(g_wiz.rows.begin() + removeAt);
    if (g_wiz.rows.empty())
        ImGui::TextColored(theme::kTextDim, "Sem parâmetros. Você pode adicionar abaixo, ou editar o arquivo depois.");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Ver o código que será criado##plug_codigo")) {
        if (problem.empty()) {
            std::string code = generateIsf(spec, std::vector<std::string>(static_cast<size_t>(std::count_if(spec.params.begin(), spec.params.end(), [](const ParamSpec& p) { return p.kind == ParamKind::Image; })), "imagem.png"));
            pushMono();
            ImGui::InputTextMultiline("##plug_codigo_texto", code.data(), code.size() + 1, ImVec2(-FLT_MIN, dp(200.0f)), ImGuiInputTextFlags_ReadOnly);
            popMono();
        } else {
            ImGui::TextColored(theme::kWarning, "%s", problem.c_str());
        }
    }

    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Checkbox("Abrir no editor depois de criar##plug_abrir_depois", &g_wiz.openEditor);
    if (!problem.empty())
        ImGui::TextColored(theme::kWarning, "%s", problem.c_str());
    if (!g_wiz.error.empty())
        ImGui::TextColored(theme::kError, "%s", g_wiz.error.c_str());

    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button(ICON_PACKAGE_PLUS "  Criar plugin##plug_criar")) {
        const fs::path folder = folders[static_cast<size_t>(g_wiz.folder)].second;
        const CreateResult res = createPlugin(spec, folder);
        if (!res.ok) {
            g_wiz.error = res.error;
        } else {
            g_wiz.error.clear();
            fx::IsfLibrary::instance().rescan();
            g_wiz.created = true;
            g_wiz.createdFile = res.file;
            g_wiz.createdName = paths::toUtf8(res.file.stem());
            g_wiz.createdId = fx::IsfLibrary::instance().idForFile(res.file).value_or(std::string());
            log::info("Novo plugin criado: " + paths::toUtf8(res.file));
            if (g_wiz.openEditor)
                openPluginInEditor(res.file);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar##plug_cancelar"))
        g_wiz.open = false;
    ImGui::End();
}

void checkImport()
{
    g_import.checked = true;
    g_import.installed = false;
    g_import.message.clear();
    g_import.check = inspectPackage(g_import.file, g_import.info);
}

void drawImport(PanelContext& ctx)
{
    if (!g_import.open)
        return;
    if (g_import.file.empty() && !g_import.checked) {
        g_import.checked = true;
        const fs::path f = pickOpenPackage();
        if (f.empty()) {
            g_import.open = false;
            g_import.checked = false;
            return;
        }
        g_import.file = f;
        checkImport();
    } else if (!g_import.checked) {
        checkImport();
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(620.0f), dp(520.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Importar plugin###janela_importar_plugin", &g_import.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_import.open = false;

    pushMono();
    ImGui::TextColored(theme::kTextDim, "%s", paths::toUtf8(g_import.file).c_str());
    popMono();
    ImGui::Spacing();

    if (!g_import.check.ok) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kError, "Este pacote não foi aceito e nada foi instalado.");
        ImGui::TextWrapped("%s", g_import.check.error.c_str());
        ImGui::PopTextWrapPos();
    } else if (g_import.installed) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kSuccess, "Plugin instalado!");
        ImGui::TextWrapped("%s", g_import.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(ICON_SPARKLES "  Ver na aba Efeitos##plug_import_ver"))
            requestFocusTab("efeitos");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FOLDER_OPEN "  Mostrar na pasta##plug_import_pasta"))
            showPluginInFolder(g_import.installedFolder / L"manifest.json");
    } else {
        const Manifest& m = g_import.info.manifest;
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
        ImGui::TextUnformatted(m.name.c_str());
        ImGui::PopFont();
        if (!m.author.empty())
            ImGui::TextColored(theme::kTextDim, "por %s", m.author.c_str());
        if (!m.version.empty())
            ImGui::TextColored(theme::kTextDim, "versão %s", m.version.c_str());
        ImGui::PushTextWrapPos(0.0f);
        if (!m.description.empty())
            ImGui::TextWrapped("%s", m.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::SeparatorText("Licença do autor");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kWarning, "%s", m.license.c_str());
        ImGui::TextColored(theme::kTextDim,
                           "Leia com atenção: é com esta licença que o autor permite usar o plugin. Instalar significa que você aceita estes "
                           "termos. O BSL Stage não confere a licença; ele só mostra o que o autor declarou.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::SeparatorText("Conteúdo");
        for (const PackageFile& f : g_import.info.files)
            ImGui::TextColored(theme::kTextDim, "%s  (%llu bytes)", f.name.c_str(), static_cast<unsigned long long>(f.size));
        if (!g_import.info.warnings.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            for (const std::string& w : g_import.info.warnings)
                ImGui::TextColored(theme::kWarning, "Aviso: %s", w.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        ImGui::TextColored(theme::kTextDim, "O pacote só tem shader, imagens e texto: nenhum programa é executado.");
        ImGui::Spacing();
        if (ImGui::Button(ICON_DOWNLOAD "  Aceito a licença e instalar##plug_importar_instalar")) {
            const InstallResult res = installPackage(g_import.file, paths::pluginsDir());
            if (!res.ok) {
                g_import.check.ok = false;
                g_import.check.error = res.error;
            } else {
                g_import.installed = true;
                g_import.installedFolder = res.folder;
                g_import.message = "Instalado em " + paths::toUtf8(res.folder) + ". Ele já aparece na aba Efeitos.";
                fx::IsfLibrary::instance().rescan();
                log::info("Plugin importado: " + paths::toUtf8(res.folder));
            }
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Fechar##plug_importar_fechar"))
        g_import.open = false;
    ImGui::End();
    (void)ctx;
}

void drawExport()
{
    if (!g_export.open)
        return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(560.0f), dp(480.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Exportar plugin###janela_exportar_plugin", &g_export.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_export.open = false;

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Cria um arquivo .bslplugin com o shader, as imagens e os dados abaixo, para você compartilhar. Só publique o que é seu "
                       "ou o que a licença original permite (veja docs/plugins/licencas.md).");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const float labelW = dp(110.0f);
    auto field = [&](const char* label, const char* id, char* buf, size_t cap, const char* hint) {
        ImGui::AlignTextToFramePadding();
        dimLabel(label);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint(id, hint, buf, cap);
    };
    field("Nome", "##plug_exp_nome", g_export.name, sizeof(g_export.name), "Nome do plugin");
    field("Autor", "##plug_exp_autor", g_export.author, sizeof(g_export.author), "Seu nome");
    field("Versão", "##plug_exp_versao", g_export.version, sizeof(g_export.version), "1.0.0");
    ImGui::AlignTextToFramePadding();
    dimLabel("Licença");
    ImGui::SameLine(labelW);
    const auto& lic = suggestedLicenses();
    const std::string preview = g_export.license >= 0 && g_export.license < static_cast<int>(lic.size())
                                    ? lic[static_cast<size_t>(g_export.license)]
                                    : (g_export.customLicense[0] ? std::string(g_export.customLicense) : std::string("Outra…"));
    ImGui::SetNextItemWidth(dp(240.0f));
    if (beginCombo("##plug_exp_licenca", preview.c_str())) {
        for (size_t i = 0; i < lic.size(); ++i)
            if (ImGui::Selectable((lic[i] + "##plug_exp_lic_" + std::to_string(i)).c_str(), g_export.license == static_cast<int>(i)))
                g_export.license = static_cast<int>(i);
        if (ImGui::Selectable("Outra (digitar)…##plug_exp_lic_outra", g_export.license < 0))
            g_export.license = -1;
        ImGui::EndCombo();
    }
    if (g_export.license < 0) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##plug_exp_licenca_texto", "Nome da licença", g_export.customLicense, sizeof(g_export.customLicense));
    }
    ImGui::AlignTextToFramePadding();
    dimLabel("Descrição");
    ImGui::SameLine(labelW);
    ImGui::InputTextMultiline("##plug_exp_descricao", g_export.description, sizeof(g_export.description), ImVec2(-FLT_MIN, dp(90.0f)));

    ImGui::Spacing();
    pushMono();
    ImGui::TextColored(theme::kTextDim, "%s", paths::toUtf8(g_export.shader).c_str());
    popMono();
    if (!g_export.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(g_export.messageError ? theme::kError : theme::kSuccess, "%s", g_export.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (ImGui::Button(ICON_UPLOAD "  Exportar…##plug_exp_exportar")) {
        Manifest m;
        m.name = g_export.name;
        m.author = g_export.author;
        m.version = g_export.version;
        m.license = g_export.license >= 0 && g_export.license < static_cast<int>(lic.size()) ? lic[static_cast<size_t>(g_export.license)]
                                                                                              : std::string(g_export.customLicense);
        m.description = g_export.description;
        const fs::path out = pickSavePackage(safeFileName(m.name.empty() ? "plugin" : m.name) + ".bslplugin");
        if (!out.empty()) {
            const PackageResult r = exportPlugin(g_export.shader, m, out);
            g_export.messageError = !r.ok;
            g_export.message = r.ok ? "Pacote criado em " + paths::toUtf8(out) : r.error;
            if (r.ok) {
                g_export.result = out;
                g_info.exportedFile = paths::toUtf8(out);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Fechar##plug_exp_fechar"))
        g_export.open = false;
    ImGui::End();
}

}

void openNewPluginWizard()
{
    resetWizard();
    g_wiz.open = true;
}

void openImportPluginWindow(const fs::path& file)
{
    g_import = ImportWindow{};
    g_import.open = true;
    g_import.file = file;
}

void openExportPluginWindow(const std::string& typeId, const fs::path& shaderFile)
{
    g_export = ExportWindow{};
    g_export.open = true;
    g_export.typeId = typeId;
    g_export.shader = shaderFile;

    std::string text;
    copyTo(g_export.name, sizeof(g_export.name), paths::toUtf8(shaderFile.stem()));
    if (readTextFile(shaderFile, text, 1u << 20)) {
        const fx::IsfParseResult parsed = fx::parseIsf(text, "", paths::toUtf8(shaderFile.stem()));
        if (parsed.ok) {
            if (parsed.doc->credit != "Criado no BSL Stage")
                copyTo(g_export.author, sizeof(g_export.author), parsed.doc->credit);
            copyTo(g_export.description, sizeof(g_export.description), parsed.doc->description);
        }
    }
}

void drawPluginWizard(PanelContext& ctx)
{
    drawWizard(ctx);
    drawImport(ctx);
    drawExport();
}

void openPluginInEditor(const fs::path& file)
{
    g_info.lastAction = "editor";
    g_info.lastPath = paths::toUtf8(file);
    if (qa::enabled()) {
        g_info.message = "(teste) o editor não foi aberto.";
        return;
    }
    if (!launchEditor(file))
        g_info.message = "Não foi possível abrir o editor. O arquivo está em " + paths::toUtf8(file);
    else
        g_info.message.clear();
}

void showPluginInFolder(const fs::path& file)
{
    g_info.lastAction = "pasta";
    g_info.lastPath = paths::toUtf8(file);
    if (qa::enabled()) {
        g_info.message = "(teste) o Explorer não foi aberto.";
        return;
    }
    paths::revealInExplorer(file);
}

std::string duplicateUserPlugin(const fs::path& file)
{

    std::error_code ec;
    fs::path target;
    const fs::path sampleDir = paths::isfDir();
    const fs::path parent = file.parent_path();
    const std::string a = paths::toUtf8(fs::weakly_canonical(parent, ec));
    const std::string b = paths::toUtf8(fs::weakly_canonical(sampleDir, ec));
    if (!b.empty() && a.size() >= b.size() && _stricmp(a.substr(0, b.size()).c_str(), b.c_str()) == 0)
        target = paths::pluginsDir();
    const CreateResult r = duplicatePlugin(file, target);
    g_info.lastAction = "duplicar";
    g_info.lastPath = r.ok ? paths::toUtf8(r.file) : std::string();
    if (!r.ok) {
        g_info.message = r.error;
        return r.error;
    }
    fx::IsfLibrary::instance().rescan();
    g_info.message = "Cópia criada: " + paths::toUtf8(r.file.filename());
    return g_info.message;
}

PluginUiInfo pluginUiInfo()
{
    PluginUiInfo i = g_info;
    i.wizardOpen = g_wiz.open;
    i.created = g_wiz.created;
    i.createdFile = paths::toUtf8(g_wiz.createdFile);
    i.createdId = g_wiz.createdId;
    i.error = g_wiz.error;
    i.rows = static_cast<int>(g_wiz.rows.size());
    i.importOpen = g_import.open;
    i.importValid = g_import.checked && g_import.check.ok;
    i.importName = g_import.info.manifest.name;
    i.importLicense = g_import.info.manifest.license;
    i.importAuthor = g_import.info.manifest.author;
    i.importError = g_import.check.ok ? std::string() : g_import.check.error;
    i.installedFolder = paths::toUtf8(g_import.installedFolder);
    i.exportOpen = g_export.open;
    return i;
}

}
