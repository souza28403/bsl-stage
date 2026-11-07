#include "media/DocThumb.h"

#include "media/DocInfo.h"
#include "media/MediaFormats.h"
#include "media/WorkerProcess.h"
#include "media/WorkerProtocol.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <vector>

namespace bsl::media {

std::wstring workerExePath()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return (std::filesystem::path(std::wstring(buf, n)).parent_path() / wp::kWorkerExeName).wstring();
}

HANDLE workerJob()
{
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return j;
    }();
    return job;
}

namespace {

HANDLE cancelEvent()
{
    static HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return ev;
}

}

void cancelDocThumbnails()
{
    SetEvent(cancelEvent());
}

bool renderDocThumbnail(const std::filesystem::path& fileIn, int w, int h, Frame& out, int* pages, std::string* error)
{
    namespace fs = std::filesystem;
    std::error_code absEc;
    const fs::path file = fs::absolute(fileIn, absEc);
    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg;
        return false;
    };
    const std::wstring exe = workerExePath();
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec))
        return fail("bslstage_worker.exe não encontrado");

    wchar_t tmpDir[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, tmpDir);
    static std::atomic<unsigned> counter{0};
    const fs::path outFile = fs::path(std::wstring(tmpDir, n))
                             / (L"bslstage-thumb-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                                + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(counter.fetch_add(1)) + L".bin");

    std::wstring cmd = L"\"" + exe + L"\" --thumb \"" + file.wstring() + L"\" \"" + outFile.wstring() + L"\" "
                       + std::to_wstring(w) + L" " + std::to_wstring(h);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring dir = fs::path(exe).parent_path().wstring();
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        dir.c_str(), &si, &pi))
        return fail("não foi possível iniciar o processo auxiliar");
    if (HANDLE job = workerJob())
        AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    const std::string ext = lowerExtension(file);
    const DWORD timeoutMs = (ext == "pdf") ? 40000 : 300000;
    DWORD code = 1;
    HANDLE waits[2] = {pi.hProcess, cancelEvent()};
    DWORD waited = WaitForMultipleObjects(2, waits, FALSE, timeoutMs);
    if (waited == WAIT_OBJECT_0 + 1)
        waited = WaitForSingleObject(pi.hProcess, 5000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        fs::remove(outFile, ec);
        return fail("o processo auxiliar demorou demais");
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        fs::remove(outFile, ec);
        return fail("o processo auxiliar falhou (código " + std::to_string(code) + ")");
    }
    std::ifstream in(outFile, std::ios::binary);
    std::int32_t head[3] = {0, 0, 0};
    if (!in || !in.read(reinterpret_cast<char*>(head), sizeof(head)) || head[0] != w || head[1] != h) {
        in.close();
        fs::remove(outFile, ec);
        return fail("miniatura inválida");
    }
    out.allocate(w, h, PixelFormat::RGBA8);
    in.read(reinterpret_cast<char*>(out.planes[0].data()), static_cast<std::streamsize>(out.planes[0].size()));
    const bool ok = static_cast<size_t>(in.gcount()) == out.planes[0].size();
    in.close();
    fs::remove(outFile, ec);
    if (!ok)
        return fail("miniatura incompleta");
    out.colorSpace = ColorSpace::BT709;
    out.range = ColorRange::Full;
    if (pages)
        *pages = head[2];
    setDocPageCount(file, head[2]);
    return true;
}

}
