#include "media/SpoutInput.h"

#include <windows.h>

#pragma warning(push, 0)
#include <SpoutReceiver.h>
#include <SpoutSenderNames.h>
#pragma warning(pop)

namespace bsl {

struct SpoutInput::Impl
{
    SpoutReceiver receiver;
    bool named = false;
};

SpoutInput::SpoutInput() : impl_(std::make_unique<Impl>()) {}

SpoutInput::~SpoutInput()
{
    release();
}

void SpoutInput::setSender(const std::string& name)
{
    if (name == sender_ && impl_->named)
        return;
    impl_->receiver.ReleaseReceiver();
    sender_ = name;
    impl_->receiver.SetReceiverName(name.empty() ? nullptr : name.c_str());
    impl_->named = true;
    connected_ = false;
    connectedName_.clear();
}

bool SpoutInput::receive()
{
    SpoutReceiver& rx = impl_->receiver;
    if (!impl_->named)
        setSender(sender_);

    if (!texture_) {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (rx.IsUpdated()) {
        const int w = static_cast<int>(rx.GetSenderWidth());
        const int h = static_cast<int>(rx.GetSenderHeight());
        if (w > 0 && h > 0) {
            glBindTexture(GL_TEXTURE_2D, texture_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            width_ = w;
            height_ = h;
        }
    }
    const bool ok = rx.ReceiveTexture(texture_, GL_TEXTURE_2D, false);
    connected_ = ok && rx.IsConnected() && width_ > 0 && width_ == static_cast<int>(rx.GetSenderWidth())
                 && height_ == static_cast<int>(rx.GetSenderHeight());
    if (connected_) {
        connectedName_ = rx.GetSenderName() ? rx.GetSenderName() : "";
        if (rx.IsFrameNew())
            ++frames_;
    } else if (!rx.IsConnected()) {
        connectedName_.clear();
    }
    return connected_;
}

void SpoutInput::release()
{
    impl_->receiver.ReleaseReceiver();
    impl_->named = false;
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    width_ = height_ = 0;
    connected_ = false;
    connectedName_.clear();
}

std::vector<std::string> SpoutInput::listSenders()
{
    std::vector<std::string> out;
    spoutSenderNames names;
    const int count = names.GetSenderCount();
    for (int i = 0; i < count; ++i) {
        char buf[256] = {};
        if (names.GetSender(i, buf, static_cast<int>(sizeof(buf))) && buf[0])
            out.emplace_back(buf);
    }
    return out;
}

}
