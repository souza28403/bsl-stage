#pragma once

#include "app/Settings.h"
#include "core/Composition.h"
#include "output/OutputSetup.h"
#include "ui/PanelContext.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace bsl {

// Ciclo de vida do projeto aberto: Novo / Abrir / Salvar, alteracoes nao salvas, titulo da janela,
// autosave e recuperacao apos crash, e "Localizar..." de arquivos ausentes.
//
// Os pedidos (request*) so anotam a intencao; quem executa e update(), chamado no comeco do frame
// (antes do ImGui::NewFrame), porque abrir um projeto troca a Composition inteira e os dialogos
// do Windows bloqueiam a thread.
class ProjectSession
{
public:
    struct Targets
    {
        Composition& composition;
        OutputSetup& output;
        Selection& selection;
        MappingSelection& mapping;
        Settings& settings;
    };

    // 'defaultOutput' monta a saida de um projeto novo (depende dos monitores conectados)
    ProjectSession(Targets targets, std::function<OutputSetup(int, int)> defaultOutput);
    ~ProjectSession();

    void setWindow(GLFWwindow* window) { window_ = window; }

    // Lock file + autosave. 'offerRecovery': se o ultimo encerramento foi anormal, pergunta se recupera.
    void enableCrashRecovery(bool offerRecovery);
    // Encerramento normal: apaga o lock e os autosaves
    void shutdown();

    // --- Pedidos da interface ---
    void requestNew();
    void requestOpen();                                  // abre o dialogo
    void requestOpenPath(const std::filesystem::path& file);
    void requestSave();
    void requestSaveAs();
    void requestQuit();                                  // fechar a janela / Sair
    void requestLocate(int deck, int layer, int column); // "Localizar..." de um clip ausente

    // Abre um projeto agora, sem perguntar nada (argumento de linha de comando)
    bool openNow(const std::filesystem::path& file);

    void update();      // antes do ImGui::NewFrame
    void drawModals();  // dentro do frame

    bool busy() const { return modal_ != Modal::None; }
    bool quitApproved() const { return quitApproved_; }
    // true uma vez depois de Novo/Abrir/Recuperar (o App ajusta o canvas)
    bool takeLoaded();

    // Considera o estado atual como o salvo (usado depois de montar a sessao)
    void markClean();
    bool dirty() const { return dirty_; }
    const std::filesystem::path& path() const { return path_; }
    std::string windowTitle() const;

private:
    using Clock = std::chrono::steady_clock;

    enum class Action { None, New, Open, OpenPath, Save, SaveAs, Quit, Locate };
    enum class Modal { None, Confirm, Recover, Message };
    enum class Decision { None, Save, Discard, Cancel, Recover, Ignore, CloseMessage };

    struct Request
    {
        Action action = Action::None;
        std::filesystem::path file;
        int deck = 0, layer = 0, column = 0;
    };
    struct Message
    {
        std::string title;
        std::string text;
    };

    void process(const Request& r);
    void execute(const Request& r);
    std::string currentText() const;
    bool isDirtyNow() const;
    bool saveCurrent();
    bool saveAs();
    bool writeTo(const std::filesystem::path& file);
    bool openFile(const std::filesystem::path& file);
    void applyLoaded(Composition&& comp, OutputSetup&& output, const std::filesystem::path& file, bool dirtyAfter);
    void doNew();
    void doLocate(const Request& r);
    void autosaveIfNeeded();
    void recoverLatestAutosave();
    std::filesystem::path latestAutosave() const;
    void showMessage(std::string title, std::string text);
    void* hwnd() const;
    std::string displayName() const;

    Targets t_;
    std::function<OutputSetup(int, int)> defaultOutput_;
    GLFWwindow* window_ = nullptr;

    std::filesystem::path path_;   // vazio = projeto sem titulo
    std::string snapshot_;         // serializacao do ultimo estado salvo
    bool dirty_ = false;
    bool loaded_ = false;
    bool quitApproved_ = false;

    Request request_;
    Request confirm_;              // o que espera a resposta do modal "Salvar alteracoes?"
    Modal modal_ = Modal::None;
    bool openPopup_ = false;
    Decision decision_ = Decision::None;
    std::vector<Message> messages_;
    Message currentMessage_;

    Clock::time_point lastDirtyCheck_{};
    Clock::time_point lastAutosave_{};
    bool crashRecovery_ = false;
    bool lockHeld_ = false;
    std::string title_;            // ultimo titulo aplicado a janela
};

} // namespace bsl
