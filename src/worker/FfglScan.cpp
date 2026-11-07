#include "worker/FfglScan.h"

#include "ffgl/FfglPe.h"
#include "ffgl/FfglPlugin.h"
#include "worker/WorkerUtil.h"

#include <windows.h>

#include <memory>
#include <string>
#include <thread>

namespace bsl::worker {

namespace {

bool writeAll(HANDLE h, const std::string& data)
{
    size_t done = 0;
    while (done < data.size()) {
        DWORD n = 0;
        if (!WriteFile(h, data.data() + done, static_cast<DWORD>(data.size() - done), &n, nullptr) || n == 0)
            return false;
        done += n;
    }
    return true;
}

}

void runFfglScan(const std::wstring& dll, const std::wstring& outFile)
{
    ffgl::ScanResult result;
    std::shared_ptr<ffgl::Plugin> plugin;

    std::string detail;
    ffgl::PeVerdict verdict = ffgl::inspectPe(dll, &detail);
    for (int attempt = 0; attempt < 6 && verdict == ffgl::PeVerdict::Unreadable; ++attempt) {
        Sleep(250);
        verdict = ffgl::inspectPe(dll, &detail);
    }
    if (verdict != ffgl::PeVerdict::Candidate) {
        result.error = std::string("não é um plugin FFGL x64: ") + ffgl::peVerdictText(verdict);
    } else {
        result = ffgl::scanPluginFile(dll, &plugin);
    }
    if (!result.ok)
        fileLog("scan-ffgl " + toUtf8(dll) + ": " + result.error);

    const std::string json = result.toJson().dump(-1, ' ', false, ffgl::Json::error_handler_t::replace);
    bool written = false;
    if (!outFile.empty()) {
        HANDLE h = CreateFileW(outFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            written = writeAll(h, json);
            FlushFileBuffers(h);
            CloseHandle(h);
        }
    } else {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        written = h && h != INVALID_HANDLE_VALUE && writeAll(h, json + "\n");
    }

    std::thread([] {
        Sleep(3000);
        TerminateProcess(GetCurrentProcess(), 0);
    }).detach();
    if (plugin) {
        plugin->deinitialise();
        plugin->unload();
    }
    TerminateProcess(GetCurrentProcess(), written ? 0 : 5);
    for (;;)
        Sleep(1000);
}

}
