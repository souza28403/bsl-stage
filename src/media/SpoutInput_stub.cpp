#include "media/SpoutInput.h"

#include "platform/Unavailable.h"

namespace bsl {

struct SpoutInput::Impl
{
    bool warned = false;
};

SpoutInput::SpoutInput() : impl_(std::make_unique<Impl>()) {}

SpoutInput::~SpoutInput() = default;

void SpoutInput::setSender(const std::string& name)
{
    sender_ = name;
}

bool SpoutInput::receive()
{
    if (!impl_->warned) {
        impl_->warned = true;
        bslplat::reportUnavailable("Spout");
    }
    connected_ = false;
    return false;
}

void SpoutInput::release()
{
    connected_ = false;
    connectedName_.clear();
}

std::vector<std::string> SpoutInput::listSenders()
{
    return {};
}

}
