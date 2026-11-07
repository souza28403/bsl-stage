#pragma once

#include "worker/FrameSink.h"
#include "worker/PipeChannel.h"

#include <filesystem>
#include <memory>
#include <string>

namespace bsl::worker {

class WebSession
{
public:
    WebSession(PipeChannel& channel, const SinkRequest& request, HANDLE wake);
    ~WebSession();
    WebSession(const WebSession&) = delete;
    WebSession& operator=(const WebSession&) = delete;

    void open(const Json& openMessage);
    void handle(const Json& message);
    void setParams(const Json& params);
    void resize(int w, int h);
    void useShm();
    void pump();
    bool wantsFastPump() const { return true; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int cefSubprocessMain(HINSTANCE instance);

std::filesystem::path moduleDir();
std::filesystem::path cefRuntimeDir();
void addCefDllDirectory();

int runPanelWindow(HINSTANCE instance, const std::wstring& url, const std::wstring& title, DWORD parentPid);

}
