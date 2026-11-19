#pragma once

#include <glad/gl.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

struct GLFWwindow;

namespace bsl {

class Fbo;

class OutputPresenter
{
public:
    OutputPresenter(GLFWwindow* window, GLuint program, bool letterbox, bool synchronous = false, int bottomInset = 0);
    ~OutputPresenter();
    OutputPresenter(const OutputPresenter&) = delete;
    OutputPresenter& operator=(const OutputPresenter&) = delete;

    void submit(const Fbo& source, int framebufferWidth, int framebufferHeight);
    long long stalledMs() const;

private:
    enum class State
    {
        Free,
        Writing,
        Ready,
        Presenting
    };

    struct Slot
    {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        State state = State::Free;
        GLsync fence = nullptr;
    };

    static constexpr int kSlots = 3;

    void run();
    void presentNow(const Fbo& source, int framebufferWidth, int framebufferHeight);
    void ensureTexture(Slot& slot, int width, int height);

    GLFWwindow* window_ = nullptr;
    GLuint program_ = 0;
    bool letterbox_ = false;
    bool synchronous_ = false;
    int bottomInset_ = 0;
    GLuint syncVao_ = 0;

    std::array<Slot, kSlots> slots_;
    GLuint blitFbo_ = 0;

    std::mutex mutex_;
    std::condition_variable wake_;
    int latest_ = -1;
    int latestFbWidth_ = 0;
    int latestFbHeight_ = 0;
    bool stop_ = false;
    std::atomic<long long> swapStartMs_{0};
    std::thread thread_;
};

}
