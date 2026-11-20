#include "app/ShowCacheProc.h"

#include "media/WorkerProcess.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace bsl::detail {

ShowProc::~ShowProc()
{
    if (process_) {
        kill();
        CloseHandle(static_cast<HANDLE>(process_));
    }
}

std::unique_ptr<ShowProc> ShowProc::start(const std::filesystem::path& job, const std::filesystem::path& progress,
                                          std::string& error)
{
    namespace fs = std::filesystem;
    const std::wstring exe = media::workerExePath();
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) {
        error = "bslstage_worker.exe não encontrado";
        return nullptr;
    }
    std::wstring cmd = L"\"" + exe + L"\" --hap-convert \"" + job.wstring() + L"\" \"" + progress.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring dir = fs::path(exe).parent_path().wstring();
    const DWORD flags = CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_SUSPENDED;
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(), &si, &pi)) {
        error = "não foi possível iniciar a conversão (erro " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    if (HANDLE jobHandle = static_cast<HANDLE>(media::workerJob()))
        AssignProcessToJobObject(jobHandle, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    std::unique_ptr<ShowProc> p(new ShowProc());
    p->process_ = pi.hProcess;
    return p;
}

bool ShowProc::running(int& exitCode)
{
    if (!process_)
        return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(static_cast<HANDLE>(process_), &code))
        return false;
    if (code == STILL_ACTIVE)
        return true;
    exitCode = static_cast<int>(code);
    return false;
}

void ShowProc::kill()
{
    if (!process_)
        return;
    DWORD code = 0;
    if (GetExitCodeProcess(static_cast<HANDLE>(process_), &code) && code == STILL_ACTIVE) {
        TerminateProcess(static_cast<HANDLE>(process_), 1);
        WaitForSingleObject(static_cast<HANDLE>(process_), 3000);
    }
}

}
