#pragma once

// Pagina web off-screen (CEF) dentro do worker. So compilado quando BSL_CEF_DIR existe (BSL_WITH_CEF).
// O CEF roda multiprocesso usando o proprio worker como subprocesso (--type=...); libcef.dll fica em cef/.

#include "worker/FrameSink.h"
#include "worker/PipeChannel.h"

#include <memory>

namespace bsl::worker {

class WebSession
{
public:
    WebSession(PipeChannel& channel, const SinkRequest& request, HANDLE wake);
    ~WebSession();
    WebSession(const WebSession&) = delete;
    WebSession& operator=(const WebSession&) = delete;

    void open(const Json& openMessage);
    // url, reload, mouse, wheel, key
    void handle(const Json& message);
    void setParams(const Json& params);
    void resize(int w, int h);
    void useShm();
    // Um ciclo do laco do CEF (CefDoMessageLoopWork) e tarefas periodicas
    void pump();
    bool wantsFastPump() const { return true; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Devolve >= 0 se este processo era um subprocesso do CEF (renderer, gpu...) e ja terminou
int cefSubprocessMain(HINSTANCE instance);

} // namespace bsl::worker
