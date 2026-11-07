#include "media/ControlPanel.h"

#include "app/Log.h"
#include "media/WorkerProcess.h"

#include <windows.h>
#include <shellapi.h>

#include <mutex>
#include <string>

namespace bsl::media {

namespace {

std::wstring widen(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

struct PanelProcess
{
    HANDLE process = nullptr;
    DWORD pid = 0;
    std::string url;
};

std::mutex g_mutex;
PanelProcess g_panel;

bool stillRunning(const PanelProcess& p)
{
    return p.process && WaitForSingleObject(p.process, 0) == WAIT_TIMEOUT;
}

void closePanel(PanelProcess& p)
{
    if (p.process) {
        if (stillRunning(p))
            TerminateProcess(p.process, 0);
        CloseHandle(p.process);
    }
    p = PanelProcess{};
}

struct FindWindowData
{
    DWORD pid;
    HWND found;
};

BOOL CALLBACK findTopWindow(HWND hwnd, LPARAM lp)
{
    auto* d = reinterpret_cast<FindWindowData*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == d->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        d->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

}

bool openUrlInBrowser(const std::string& url, std::string* error)
{
    if (url.rfind("http://127.0.0.1:", 0) != 0 && url.rfind("http://localhost:", 0) != 0) {
        if (error)
            *error = "endereço não permitido";
        return false;
    }
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) {
        if (error)
            *error = "o Windows não conseguiu abrir o navegador padrão";
        return false;
    }
    return true;
}

bool openUrlInOwnWindow(const std::string& url, const std::string& title, std::string* error)
{
    if (url.rfind("http://127.0.0.1:", 0) != 0) {
        if (error)
            *error = "endereço não permitido";
        return false;
    }
    std::lock_guard lk(g_mutex);
    if (stillRunning(g_panel) && g_panel.url == url) {
        FindWindowData d{g_panel.pid, nullptr};
        EnumWindows(&findTopWindow, reinterpret_cast<LPARAM>(&d));
        if (d.found) {
            if (IsIconic(d.found))
                ShowWindow(d.found, SW_RESTORE);
            SetForegroundWindow(d.found);
            return true;
        }
    }
    closePanel(g_panel);

    const std::wstring exe = workerExePath();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(exe, ec)) {
        if (error)
            *error = "o processo auxiliar (bslstage_worker.exe) não foi encontrado";
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\" --panel \"" + widen(url) + L"\" --title \"" + widen(title) + L"\" --parent " +
                       std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (error)
            *error = "não foi possível abrir a janela do painel (erro " + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (HANDLE job = workerJob())
        AssignProcessToJobObject(job, pi.hProcess);
    CloseHandle(pi.hThread);
    g_panel.process = pi.hProcess;
    g_panel.pid = pi.dwProcessId;
    g_panel.url = url;
    log::info("Painel de controle: janela própria aberta (" + url + ")");
    return true;
}

void closeOwnControlWindow()
{
    std::lock_guard lk(g_mutex);
    closePanel(g_panel);
}

unsigned long ownControlWindowPid()
{
    std::lock_guard lk(g_mutex);
    return stillRunning(g_panel) ? g_panel.pid : 0ul;
}

}
