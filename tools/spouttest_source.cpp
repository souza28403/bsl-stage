#include "core/Generators.h"
#include "media/SpoutSource.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "spouttest_source.h"

namespace spouttest {

bool receiveWithAppSource(const std::string& sender, const std::function<void()>& sendOne, AppSourceResult& out)
{
    if (!gladLoadGL(glfwGetProcAddress))
        return false;

    const bsl::GeneratorInfo* info = bsl::findGenerator(bsl::kSpoutId);
    if (!info || !info->live)
        return false;
    out.registered = true;

    for (int i = 0; i < 40 && !out.listed; ++i) {
        sendOne();
        const std::vector<std::string> senders = bsl::media::SpoutSource::activeSenders();
        out.listed = std::find(senders.begin(), senders.end(), sender) != senders.end();
        if (!out.listed)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::unique_ptr<bsl::media::ISource> src = info->create(0, 0);
    bsl::media::ParamValues params;
    params.set("sender", sender);
    src->setParams(params);
    if (!src->open({}))
        return false;
    auto* spout = dynamic_cast<bsl::media::SpoutSource*>(src.get());
    if (!spout)
        return false;
    out.isLive = src->isLive();

    for (int i = 0; i < 300 && !(spout->connected() && spout->frameCount() > 2); ++i) {
        sendOne();
        src->update(0.016);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    out.connected = spout->connected();
    out.status = src->status().text;
    out.width = src->width();
    out.height = src->height();
    if (out.connected && out.width > 0 && out.height > 0) {
        out.rgba.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4);
        glBindTexture(GL_TEXTURE_2D, src->texture());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out.rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    src->close();
    return true;
}

}
