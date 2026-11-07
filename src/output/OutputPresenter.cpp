#include "output/OutputPresenter.h"

#include "render/Fbo.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>

namespace bsl {

namespace {

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

OutputPresenter::OutputPresenter(GLFWwindow* window, GLuint program, bool letterbox)
    : window_(window), program_(program), letterbox_(letterbox)
{
    thread_ = std::thread([this] { run(); });
}

OutputPresenter::~OutputPresenter()
{
    {
        std::lock_guard lk(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();

    for (Slot& s : slots_) {
        if (s.fence)
            glDeleteSync(s.fence);
        if (s.texture)
            glDeleteTextures(1, &s.texture);
        s = Slot{};
    }
    if (blitFbo_)
        glDeleteFramebuffers(1, &blitFbo_);
}

long long OutputPresenter::stalledMs() const
{
    const long long start = swapStartMs_.load();
    return start == 0 ? 0 : std::max(0LL, nowMs() - start);
}

void OutputPresenter::ensureTexture(Slot& slot, int width, int height)
{
    if (slot.texture && slot.width == width && slot.height == height)
        return;
    if (slot.texture)
        glDeleteTextures(1, &slot.texture);
    glGenTextures(1, &slot.texture);
    glBindTexture(GL_TEXTURE_2D, slot.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    slot.width = width;
    slot.height = height;
}

void OutputPresenter::submit(const Fbo& source, int framebufferWidth, int framebufferHeight)
{
    int index = -1;
    GLsync stale = nullptr;
    GLsync release = nullptr;
    {
        std::lock_guard lk(mutex_);
        for (int i = 0; i < kSlots; ++i) {
            if (slots_[static_cast<size_t>(i)].state == State::Free) {
                index = i;
                break;
            }
        }
        if (index >= 0) {
            release = slots_[static_cast<size_t>(index)].fence;
        } else if (latest_ >= 0 && slots_[static_cast<size_t>(latest_)].state == State::Ready) {
            index = latest_;
            latest_ = -1;
            stale = slots_[static_cast<size_t>(index)].fence;
        }
        if (index < 0)
            return;
        Slot& s = slots_[static_cast<size_t>(index)];
        s.fence = nullptr;
        s.state = State::Writing;
    }
    if (stale)
        glDeleteSync(stale);
    if (release) {
        glWaitSync(release, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(release);
    }

    Slot& slot = slots_[static_cast<size_t>(index)];
    ensureTexture(slot, source.width(), source.height());
    if (!blitFbo_)
        glGenFramebuffers(1, &blitFbo_);

    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source.id());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFbo_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.texture, 0);
    glBlitFramebuffer(0, 0, source.width(), source.height(), 0, 0, source.width(), source.height(),
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GLsync ready = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();

    GLsync dropped = nullptr;
    {
        std::lock_guard lk(mutex_);
        if (latest_ >= 0) {
            Slot& old = slots_[static_cast<size_t>(latest_)];
            dropped = old.fence;
            old.fence = nullptr;
            old.state = State::Free;
        }
        slot.fence = ready;
        slot.state = State::Ready;
        latest_ = index;
        latestFbWidth_ = framebufferWidth;
        latestFbHeight_ = framebufferHeight;
    }
    if (dropped)
        glDeleteSync(dropped);
    wake_.notify_one();
}

void OutputPresenter::run()
{
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    while (true) {
        int index = -1;
        int fw = 0, fh = 0;
        GLsync ready = nullptr;
        GLuint texture = 0;
        int texW = 0, texH = 0;
        {
            std::unique_lock lk(mutex_);
            wake_.wait(lk, [this] { return stop_ || latest_ >= 0; });
            if (stop_)
                break;
            index = latest_;
            latest_ = -1;
            Slot& s = slots_[static_cast<size_t>(index)];
            s.state = State::Presenting;
            ready = s.fence;
            s.fence = nullptr;
            texture = s.texture;
            texW = s.width;
            texH = s.height;
            fw = latestFbWidth_;
            fh = latestFbHeight_;
        }

        if (ready) {
            glWaitSync(ready, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync(ready);
        }

        const bool visible = fw > 0 && fh > 0 && texW > 0 && texH > 0;
        if (visible) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            glViewport(0, 0, fw, fh);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            int vx = 0, vy = 0, vw = fw, vh = fh;
            if (letterbox_) {
                const double k = std::min(static_cast<double>(fw) / texW, static_cast<double>(fh) / texH);
                vw = std::max(1, static_cast<int>(texW * k));
                vh = std::max(1, static_cast<int>(texH * k));
                vx = (fw - vw) / 2;
                vy = (fh - vh) / 2;
            }
            glViewport(vx, vy, vw, vh);

            glUseProgram(program_);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
        }

        GLsync release = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        {
            std::lock_guard lk(mutex_);
            Slot& s = slots_[static_cast<size_t>(index)];
            s.fence = release;
            s.state = State::Free;
        }

        if (visible) {
            swapStartMs_ = nowMs();
            glfwSwapBuffers(window_);
            swapStartMs_ = 0;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glDeleteVertexArrays(1, &vao);
    glFinish();
    glfwMakeContextCurrent(nullptr);
}

}
