#include "update/Apply.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

struct Args
{
    DWORD pid = 0;
    fs::path work, zip, target, restart, installer, msi;
    std::string from, to;
};

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

Args parseArgs()
{
    Args a;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring k = argv[i];
        const bool hasValue = i + 1 < argc;
        if (k == L"--pid" && hasValue) a.pid = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 10));
        else if (k == L"--work" && hasValue) a.work = argv[++i];
        else if (k == L"--zip" && hasValue) a.zip = argv[++i];
        else if (k == L"--target" && hasValue) a.target = argv[++i];
        else if (k == L"--restart" && hasValue) a.restart = argv[++i];
        else if (k == L"--installer" && hasValue) a.installer = argv[++i];
        else if (k == L"--msi" && hasValue) a.msi = argv[++i];
        else if (k == L"--from" && hasValue) a.from = toUtf8(argv[++i]);
        else if (k == L"--to" && hasValue) a.to = toUtf8(argv[++i]);
    }
    if (argv)
        LocalFree(argv);
    return a;
}

HWND g_label = nullptr;
HWND g_bar = nullptr;

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE)
        return 0;
    return DefWindowProcW(h, m, w, l);
}

HWND makeWindow(HINSTANCE inst)
{
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"BSLStageUpdater";
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    const int w = 380, h = 110;
    HWND win = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"BSL Stage", WS_CAPTION, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                               (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    if (!win)
        return nullptr;
    g_label = CreateWindowExW(0, L"STATIC", L"Instalando a atualização… não desligue o computador.", WS_CHILD | WS_VISIBLE, 12, 10,
                              340, 20, win, nullptr, inst, nullptr);
    g_bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 12, 38, 340, 18, win, nullptr, inst, nullptr);
    SendMessageW(g_bar, PBM_SETRANGE32, 0, 1000);
    ShowWindow(win, SW_SHOWNOACTIVATE);
    UpdateWindow(win);
    return win;
}

void writeResult(const Args& a, bool ok, bool rolledBack, const std::string& error)
{
    nlohmann::json j;
    j["ok"] = ok;
    j["rolledBack"] = rolledBack;
    j["from"] = a.from;
    j["to"] = a.to;
    j["error"] = error;
    j["time"] = static_cast<int64_t>(std::time(nullptr));
    std::error_code ec;
    fs::create_directories(a.work, ec);
    std::ofstream out(a.work / L"last-result.json", std::ios::binary | std::ios::trunc);
    if (out)
        out << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

void launch(const fs::path& exe)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    const std::wstring dir = exe.parent_path().wstring();
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int work(const Args& a)
{
    std::ofstream logFile(a.work / L"updater.log", std::ios::app);
    auto log = [&](const std::string& s) {
        if (logFile)
            logFile << s << '\n' << std::flush;
    };
    log("== atualizador: " + a.from + " -> " + a.to);

    if (a.pid != 0) {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, a.pid);
        if (h) {
            const DWORD r = WaitForSingleObject(h, 120000);
            CloseHandle(h);
            if (r != WAIT_OBJECT_0) {
                log("o BSL Stage não fechou em 2 minutos; nada foi alterado");
                writeResult(a, false, false, "o BSL Stage não fechou a tempo");
                return 1;
            }
        }
    }
    Sleep(500);

    if (!a.msi.empty()) {
        const std::wstring params = L"/i \"" + a.msi.wstring() + L"\"";
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = L"msiexec.exe";
        sei.lpParameters = params.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei) || !sei.hProcess) {
            log("não foi possível iniciar o msiexec");
            writeResult(a, false, false, "não foi possível iniciar o instalador");
            return 1;
        }
        log("msiexec iniciado");
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        const bool ok = exitCode == 0 || exitCode == 3010;
        log("msiexec terminou com o código " + std::to_string(exitCode));
        writeResult(a, ok, false, ok ? "" : "a instalação foi cancelada ou não terminou (código " + std::to_string(exitCode) + ")");
        if (!a.restart.empty())
            launch(a.restart);
        return ok ? 0 : 1;
    }

    if (!a.installer.empty()) {
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", a.installer.c_str(), nullptr, a.installer.parent_path().c_str(), SW_SHOWNORMAL);
        const bool ok = reinterpret_cast<INT_PTR>(r) > 32;
        log(ok ? "instalador iniciado" : "não foi possível iniciar o instalador");
        writeResult(a, ok, false, ok ? "" : "não foi possível iniciar o instalador");
        return ok ? 0 : 1;
    }

    bsl::update::ApplyOptions o;
    o.zipFile = a.zip;
    o.targetDir = a.target;
    o.stagingDir = a.work / L"staging";
    o.backupDir = a.work / L"backup" / toWide(a.from.empty() ? "anterior" : a.from);
    o.log = log;
    o.progress = [](double f, const std::string& phase) {
        if (g_bar)
            PostMessageW(g_bar, PBM_SETPOS, static_cast<WPARAM>(f * 1000.0), 0);
        if (g_label) {
            const std::wstring text = L"Atualização: " + toWide(phase) + L"… não feche esta janela.";
            SetWindowTextW(g_label, text.c_str());
        }
    };
    const bsl::update::ApplyResult r = bsl::update::applyZipUpdate(o);
    log(r.ok ? "ok" : "falhou: " + r.error);
    writeResult(a, r.ok, r.rolledBack, r.error);

    if (!a.restart.empty())
        launch(a.restart);
    return r.ok ? 0 : 1;
}

}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    const Args a = parseArgs();
    if (a.work.empty() || (a.zip.empty() && a.installer.empty() && a.msi.empty())
        || (a.installer.empty() && a.msi.empty() && a.target.empty()))
        return 2;
    std::error_code ec;
    fs::create_directories(a.work, ec);
    HWND win = a.msi.empty() ? makeWindow(inst) : nullptr;

    std::atomic<int> code{-1};
    std::thread worker([&] { code = work(a); });
    MSG msg;
    while (code.load() < 0) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    worker.join();
    if (win)
        DestroyWindow(win);
    return code.load();
}
