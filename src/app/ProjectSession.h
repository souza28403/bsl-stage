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

    ProjectSession(Targets targets, std::function<OutputSetup(int, int)> defaultOutput);
    ~ProjectSession();

    void setWindow(GLFWwindow* window) { window_ = window; }

    void enableCrashRecovery(bool offerRecovery);
    void shutdown();

    void requestNew();
    void requestOpen();
    void requestOpenPath(const std::filesystem::path& file);
    void requestSave();
    void requestSaveAs();
    void requestQuit();
    void requestLocate(int deck, int layer, int column);

    bool openNow(const std::filesystem::path& file);

    void update();
    void drawModals();

    bool busy() const { return modal_ != Modal::None; }
    bool quitApproved() const { return quitApproved_; }
    bool takeLoaded();

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

    std::filesystem::path path_;
    std::string snapshot_;
    bool dirty_ = false;
    bool loaded_ = false;
    bool quitApproved_ = false;

    Request request_;
    Request confirm_;
    Modal modal_ = Modal::None;
    bool openPopup_ = false;
    Decision decision_ = Decision::None;
    std::vector<Message> messages_;
    Message currentMessage_;

    Clock::time_point lastDirtyCheck_{};
    Clock::time_point lastAutosave_{};
    bool crashRecovery_ = false;
    bool lockHeld_ = false;
    std::string title_;
};

}
