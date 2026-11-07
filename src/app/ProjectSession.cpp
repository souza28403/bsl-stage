#include "app/ProjectSession.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "io/FileDialogs.h"
#include "io/ProjectFile.h"
#include "ui/Theme.h"

#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <imgui.h>

#include <algorithm>
#include <ctime>
#include <fstream>

namespace bsl {

namespace fs = std::filesystem;

namespace {

constexpr auto kDirtyCheckInterval = std::chrono::seconds(1);
constexpr auto kAutosaveInterval = std::chrono::minutes(2);
constexpr size_t kAutosaveKeep = 3;

constexpr const char* kPopupConfirm = "Salvar alterações?###proj_confirmar";
constexpr const char* kPopupRecover = "Recuperar projeto###proj_recuperar";

fs::path autosaveDir()
{
    return paths::appDataDir() / L"autosave";
}

fs::path lockPath()
{
    return paths::appDataDir() / L"session.lock";
}

std::vector<fs::path> listAutosaves()
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(autosaveDir(), ec)) {
        const fs::path& p = entry.path();
        if (entry.is_regular_file(ec) && p.extension() == project::kExtension
            && p.filename().wstring().starts_with(L"autosave-"))
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool isProjectExtension(const fs::path& p)
{
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return ext == L".bslstage";
}

}

ProjectSession::ProjectSession(Targets targets, std::function<OutputSetup(int, int)> defaultOutput)
    : t_(targets), defaultOutput_(std::move(defaultOutput))
{
    lastDirtyCheck_ = lastAutosave_ = Clock::now();
}

ProjectSession::~ProjectSession() = default;

void* ProjectSession::hwnd() const
{
    return window_ ? static_cast<void*>(glfwGetWin32Window(window_)) : nullptr;
}

void ProjectSession::enableCrashRecovery(bool offerRecovery)
{
    crashRecovery_ = true;
    std::error_code ec;
    const bool crashed = fs::exists(lockPath(), ec);
    std::ofstream(lockPath()) << GetCurrentProcessId() << '\n';
    lockHeld_ = true;
    lastAutosave_ = Clock::now();

    if (crashed && offerRecovery && !latestAutosave().empty()) {
        modal_ = Modal::Recover;
        openPopup_ = true;
    }
}

void ProjectSession::shutdown()
{
    if (!crashRecovery_)
        return;
    std::error_code ec;
    for (const fs::path& f : listAutosaves())
        fs::remove(f, ec);
    if (lockHeld_)
        fs::remove(lockPath(), ec);
    lockHeld_ = false;
    crashRecovery_ = false;
}

fs::path ProjectSession::latestAutosave() const
{
    const auto files = listAutosaves();
    return files.empty() ? fs::path() : files.back();
}

void ProjectSession::autosaveIfNeeded()
{
    if (!crashRecovery_ || Clock::now() - lastAutosave_ < kAutosaveInterval)
        return;
    lastAutosave_ = Clock::now();
    if (!isDirtyNow())
        return;

    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    wchar_t stamp[40];
    std::wcsftime(stamp, 40, L"autosave-%Y%m%d-%H%M%S.bslstage", &tm);

    std::string error;
    const fs::path file = autosaveDir() / stamp;
    if (!project::saveFile(file, project::serialize(t_.composition, t_.output, {}), &error)) {
        log::warn("Autosave falhou: " + error);
        return;
    }
    const auto files = listAutosaves();
    std::error_code ec;
    for (size_t i = 0; i + kAutosaveKeep < files.size(); ++i)
        fs::remove(files[i], ec);
}

void ProjectSession::recoverLatestAutosave()
{
    const fs::path file = latestAutosave();
    if (file.empty())
        return;
    Composition comp;
    comp.decoderOptions = t_.composition.decoderOptions;
    OutputSetup output;
    const project::LoadResult res = project::loadFile(file, comp, output);
    if (!res.ok) {
        showMessage("Não foi possível recuperar", res.error);
        return;
    }
    applyLoaded(std::move(comp), std::move(output), {}, true);
    log::info("Autosave recuperado: " + paths::toUtf8(file.filename()));
}

std::string ProjectSession::currentText() const
{
    return project::serialize(t_.composition, t_.output, path_.parent_path());
}

bool ProjectSession::isDirtyNow() const
{
    return currentText() != snapshot_;
}

void ProjectSession::markClean()
{
    snapshot_ = currentText();
    dirty_ = false;
}

std::string ProjectSession::displayName() const
{
    return path_.empty() ? "Sem título" : paths::toUtf8(path_.filename());
}

std::string ProjectSession::windowTitle() const
{
    return "BSL Stage — " + displayName() + (dirty_ ? " *" : "");
}

bool ProjectSession::takeLoaded()
{
    const bool v = loaded_;
    loaded_ = false;
    return v;
}

void ProjectSession::showMessage(std::string title, std::string text)
{
    messages_.push_back({std::move(title), std::move(text)});
}

void ProjectSession::requestNew() { request_ = {Action::New, {}, 0, 0, 0}; }
void ProjectSession::requestOpen() { request_ = {Action::Open, {}, 0, 0, 0}; }
void ProjectSession::requestOpenPath(const fs::path& file) { request_ = {Action::OpenPath, file, 0, 0, 0}; }
void ProjectSession::requestSave() { request_ = {Action::Save, {}, 0, 0, 0}; }
void ProjectSession::requestSaveAs() { request_ = {Action::SaveAs, {}, 0, 0, 0}; }
void ProjectSession::requestQuit() { request_ = {Action::Quit, {}, 0, 0, 0}; }
void ProjectSession::requestLocate(int deck, int layer, int column)
{
    request_ = {Action::Locate, {}, deck, layer, column};
}

bool ProjectSession::openNow(const fs::path& file)
{
    return openFile(file);
}

void ProjectSession::update()
{
    const auto now = Clock::now();

    const Decision decision = decision_;
    decision_ = Decision::None;
    switch (decision) {
    case Decision::Save: {
        const Request pending = confirm_;
        confirm_ = {};
        if (saveCurrent())
            execute(pending);
        break;
    }
    case Decision::Discard: {
        const Request pending = confirm_;
        confirm_ = {};
        execute(pending);
        break;
    }
    case Decision::Cancel:
        confirm_ = {};
        break;
    case Decision::Recover:
        recoverLatestAutosave();
        break;
    default:
        break;
    }

    if (request_.action != Action::None) {
        const Request r = request_;
        request_ = {};
        if (modal_ == Modal::None)
            process(r);
    }

    if (now - lastDirtyCheck_ >= kDirtyCheckInterval) {
        lastDirtyCheck_ = now;
        dirty_ = isDirtyNow();
    }
    autosaveIfNeeded();

    const std::string title = windowTitle();
    if (window_ && title != title_) {
        glfwSetWindowTitle(window_, title.c_str());
        title_ = title;
    }
}

void ProjectSession::process(const Request& r)
{
    switch (r.action) {
    case Action::Save:
        saveCurrent();
        return;
    case Action::SaveAs:
        saveAs();
        return;
    case Action::Locate:
        doLocate(r);
        return;
    case Action::New:
    case Action::Open:
    case Action::OpenPath:
    case Action::Quit:
        if (isDirtyNow()) {
            dirty_ = true;
            confirm_ = r;
            modal_ = Modal::Confirm;
            openPopup_ = true;
        } else {
            execute(r);
        }
        return;
    case Action::None:
        return;
    }
}

void ProjectSession::execute(const Request& r)
{
    switch (r.action) {
    case Action::New:
        doNew();
        break;
    case Action::Open: {
        std::error_code ec;
        fs::path start = path_.empty() ? fs::path() : path_.parent_path();
        if (start.empty() && !t_.settings.recentProjects.empty()) {
            const std::string& last = t_.settings.recentProjects.front();
            start = fs::path(std::u8string(last.begin(), last.end())).parent_path();
        }
        if (const auto file = dialogs::openProject(static_cast<HWND>(hwnd()), start))
            openFile(*file);
        break;
    }
    case Action::OpenPath:
        openFile(r.file);
        break;
    case Action::Quit:
        quitApproved_ = true;
        break;
    default:
        break;
    }
}

bool ProjectSession::saveCurrent()
{
    if (path_.empty())
        return saveAs();
    return writeTo(path_);
}

bool ProjectSession::saveAs()
{
    std::optional<fs::path> file;
    if (qa::enabled()) {
        wchar_t buf[MAX_PATH * 4] = {};
        const DWORD n = GetEnvironmentVariableW(L"BSLSTAGE_QA_SAVE_PATH", buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0 && n < std::size(buf))
            file = fs::path(std::wstring(buf, n));
    }
    if (!file)
        file = dialogs::saveProject(static_cast<HWND>(hwnd()), path_);
    if (!file)
        return false;
    if (!isProjectExtension(*file))
        file->replace_extension(project::kExtension);
    return writeTo(*file);
}

bool ProjectSession::writeTo(const fs::path& file)
{
    const std::string text = project::serialize(t_.composition, t_.output, file.parent_path());
    std::string error;
    if (!project::saveFile(file, text, &error)) {
        log::warn("Não foi possível salvar: " + error);
        showMessage("Não foi possível salvar", error);
        return false;
    }
    path_ = file;
    snapshot_ = text;
    dirty_ = false;
    t_.settings.addRecentProject(paths::toUtf8(file));
    t_.settings.save();
    log::info("Projeto salvo: " + paths::toUtf8(file));
    return true;
}

bool ProjectSession::openFile(const fs::path& fileArg)
{
    std::error_code absEc;
    const fs::path file = fs::absolute(fileArg, absEc).lexically_normal();
    Composition comp;
    comp.decoderOptions = t_.composition.decoderOptions;
    OutputSetup output;
    const project::LoadResult res = project::loadFile(file, comp, output);
    if (!res.ok) {
        std::error_code ec;
        if (!fs::exists(file, ec)) {
            t_.settings.removeRecentProject(paths::toUtf8(file));
            t_.settings.save();
        }
        log::warn("Não foi possível abrir " + paths::toUtf8(file) + ": " + res.error);
        showMessage("Não foi possível abrir o projeto",
                    paths::toUtf8(file.filename()) + ": " + res.error + ".\n\nO projeto atual foi mantido.");
        return false;
    }

    std::string notes;
    for (const std::string& w : res.warnings) {
        log::warn(w);
        if (w.starts_with("Este projeto"))
            notes += w + "\n";
    }
    applyLoaded(std::move(comp), std::move(output), file, false);
    if (!notes.empty())
        showMessage("Aviso", notes);
    if (res.missingFiles > 0)
        log::warn(std::to_string(res.missingFiles) + " arquivo(s) não encontrado(s) neste projeto.");
    return true;
}

void ProjectSession::applyLoaded(Composition&& comp, OutputSetup&& output, const fs::path& file, bool dirtyAfter)
{
    t_.composition.stopAll();
    int next = std::max(t_.output.nextScreenId, 1);
    for (Screen& s : output.screens)
        s.id = next++;
    output.nextScreenId = next;

    t_.composition = std::move(comp);
    t_.output = std::move(output);
    t_.selection = {};
    t_.mapping = {0, 0, -1};
    path_ = file;
    loaded_ = true;
    lastAutosave_ = Clock::now();

    if (dirtyAfter) {
        snapshot_.clear();
        dirty_ = true;
    } else {
        markClean();
        if (!file.empty()) {
            t_.settings.addRecentProject(paths::toUtf8(file));
            t_.settings.save();
        }
    }
    log::info("Projeto aberto: " + (file.empty() ? std::string("(autosave)") : paths::toUtf8(file)));
}

void ProjectSession::doNew()
{
    Composition comp = Composition::createDefault();
    comp.decoderOptions = t_.composition.decoderOptions;
    OutputSetup output = defaultOutput_ ? defaultOutput_(comp.width, comp.height) : OutputSetup{};
    applyLoaded(std::move(comp), std::move(output), {}, false);
}

void ProjectSession::doLocate(const Request& r)
{
    Composition& comp = t_.composition;
    if (!comp.validCell(r.deck, r.layer, r.column))
        return;
    Clip& clip = comp.clipAt(r.deck, r.layer, r.column);
    if (clip.type != Clip::Type::File)
        return;

    fs::path start = clip.path.parent_path();
    std::error_code ec;
    if (!fs::is_directory(start, ec))
        start.clear();
    const auto file = dialogs::openMedia(static_cast<HWND>(hwnd()), start);
    if (!file)
        return;
    if (!project::relinkClip(clip, *file)) {
        showMessage("Formato não suportado", paths::toUtf8(file->filename()) + " não é um vídeo ou imagem suportado.");
        return;
    }
    const int others = project::relinkMissingInFolder(comp, file->parent_path());
    if (others > 0)
        showMessage("Arquivos reencontrados",
                    "\"" + clip.name + "\" foi localizado.\n"
                        + std::to_string(others) + " outro(s) arquivo(s) ausente(s) também "
                        + (others == 1 ? "foi reencontrado" : "foram reencontrados") + " nessa pasta.");
    else
        log::info("Arquivo localizado: " + paths::toUtf8(*file));
}

void ProjectSession::drawModals()
{
    if (modal_ == Modal::None && !messages_.empty()) {
        currentMessage_ = std::move(messages_.front());
        messages_.erase(messages_.begin());
        modal_ = Modal::Message;
        openPopup_ = true;
    }

    const char* popupName = nullptr;
    switch (modal_) {
    case Modal::Confirm: popupName = kPopupConfirm; break;
    case Modal::Recover: popupName = kPopupRecover; break;
    case Modal::Message: break;
    case Modal::None: return;
    }
    const std::string messageName = currentMessage_.title + "###proj_mensagem";
    if (modal_ == Modal::Message)
        popupName = messageName.c_str();

    if (openPopup_) {
        ImGui::OpenPopup(popupName);
        openPopup_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(popupName, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const ImVec2 buttonSize(ui::dp(120.0f), 0.0f);
    auto finish = [&](Decision d) {
        decision_ = d;
        modal_ = Modal::None;
        ImGui::CloseCurrentPopup();
    };

    switch (modal_) {
    case Modal::Confirm:
        ImGui::Text("O projeto \"%s\" tem alterações não salvas.", displayName().c_str());
        ImGui::TextColored(ui::theme::kTextDim, "Deseja salvar antes de continuar?");
        ImGui::Spacing();
        if (ImGui::Button("Salvar", buttonSize))
            finish(Decision::Save);
        ImGui::SameLine();
        if (ImGui::Button("Não salvar", buttonSize))
            finish(Decision::Discard);
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            finish(Decision::Cancel);
        break;
    case Modal::Recover:
        ImGui::TextUnformatted("O BSL Stage não foi fechado corretamente.");
        ImGui::TextUnformatted("Recuperar o último autosave?");
        ImGui::Spacing();
        if (ImGui::Button("Recuperar", buttonSize))
            finish(Decision::Recover);
        ImGui::SameLine();
        if (ImGui::Button("Ignorar", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            finish(Decision::Ignore);
        break;
    case Modal::Message:
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextWrapped("%s", currentMessage_.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape)
            || ImGui::IsKeyPressed(ImGuiKey_Enter))
            finish(Decision::CloseMessage);
        break;
    case Modal::None:
        break;
    }
    ImGui::EndPopup();
}

}
