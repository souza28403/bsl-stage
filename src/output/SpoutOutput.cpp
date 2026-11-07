#include "output/SpoutOutput.h"

#include <windows.h>

#pragma warning(push, 0)
#include <SpoutSender.h>
#pragma warning(pop)

namespace bsl {

struct SpoutOutput::Impl
{
    SpoutSender sender;
    bool named = false;
};

SpoutOutput::SpoutOutput() : impl_(std::make_unique<Impl>()) {}

SpoutOutput::~SpoutOutput()
{
    release();
}

void SpoutOutput::setName(const std::string& name)
{
    if (name == name_)
        return;
    release();
    name_ = name;
}

bool SpoutOutput::send(unsigned int texture, int width, int height)
{
    if (texture == 0 || width <= 0 || height <= 0)
        return false;
    if (!impl_->named) {
        impl_->sender.SetSenderName(name_.empty() ? "BSL Stage" : name_.c_str());
        impl_->named = true;
    }
    return impl_->sender.SendTexture(texture, 0x0DE1 , static_cast<unsigned int>(width),
                                     static_cast<unsigned int>(height), true, 0);
}

void SpoutOutput::release()
{
    impl_->sender.ReleaseSender();
    impl_->named = false;
}

}
