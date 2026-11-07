#include "worker/WebSession.h"
#include "worker/WorkerUtil.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_task.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace bsl::worker {

namespace fs = std::filesystem;

namespace {

class QuitTask final : public CefTask
{
public:
    void Execute() override { CefQuitMessageLoop(); }

private:
    IMPLEMENT_REFCOUNTING(QuitTask);
};

class PanelClient final : public CefClient, public CefLifeSpanHandler
{
public:
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
                       WindowOpenDisposition, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
                       CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override
    {
        return true;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser>) override { CefQuitMessageLoop(); }

private:
    IMPLEMENT_REFCOUNTING(PanelClient);
};

}

int runPanelWindow(HINSTANCE instance, const std::wstring& url, const std::wstring& title, DWORD parentPid)
{
    addCefDllDirectory();
    const fs::path runtime = cefRuntimeDir();
    if (!fs::is_regular_file(runtime / L"libcef.dll")) {
        fileLog("painel: cef\\libcef.dll nao encontrado");
        return 3;
    }
    if (url.rfind(L"http://127.0.0.1:", 0) != 0)
        return 4;

    std::error_code ec;
    const fs::path root = localDataDir() / L"cef" / (L"panel-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root, ec);

    CefMainArgs args(instance);
    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.root_cache_path) = root.wstring();
    CefString(&settings.log_file) = (root / L"cef.log").wstring();
    CefString(&settings.browser_subprocess_path) = (moduleDir() / L"bslstage_worker.exe").wstring();
    CefString(&settings.resources_dir_path) = runtime.wstring();
    CefString(&settings.locales_dir_path) = (runtime / L"locales").wstring();
    CefString(&settings.locale) = L"pt-BR";
    CefString(&settings.accept_language_list) = L"pt-BR,pt,en-US,en";
    if (!CefInitialize(args, settings, nullptr, nullptr)) {
        fileLog("painel: CefInitialize falhou (" + std::to_string(CefGetExitCode()) + ")");
        fs::remove_all(root, ec);
        return 5;
    }

    CefRefPtr<PanelClient> client = new PanelClient();
    CefWindowInfo info;
    info.SetAsPopup(nullptr, title);
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    {
        RECT work{0, 0, 1920, 1080};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int ww = work.right - work.left, wh = work.bottom - work.top;
        const int w = std::min(1280, static_cast<int>(ww * 0.8)), h = std::min(860, static_cast<int>(wh * 0.85));
        info.bounds.x = work.left + (ww - w) / 2;
        info.bounds.y = work.top + (wh - h) / 2;
        info.bounds.width = w;
        info.bounds.height = h;
    }
    CefBrowserSettings bs;
    CefBrowserHost::CreateBrowser(info, client, toUtf8(url), bs, nullptr, nullptr);

    std::atomic<bool> done{false};
    std::thread watcher([parentPid, &done] {
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        while (!done.load()) {
            if (parent && WaitForSingleObject(parent, 250) == WAIT_OBJECT_0) {
                CefPostTask(TID_UI, new QuitTask());
                break;
            }
            if (!parent)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (parent)
            CloseHandle(parent);
    });

    CefRunMessageLoop();

    done = true;
    watcher.join();
    client = nullptr;
    CefShutdown();
    fs::remove_all(root, ec);
    return 0;
}

}
