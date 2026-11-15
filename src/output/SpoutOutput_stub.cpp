#include "output/SpoutOutput.h"

#include "platform/Unavailable.h"

namespace bsl {

struct SpoutOutput::Impl
{
    bool warned = false;
};

SpoutOutput::SpoutOutput() : impl_(std::make_unique<Impl>()) {}

SpoutOutput::~SpoutOutput() = default;

void SpoutOutput::setName(const std::string& name)
{
    name_ = name;
}

bool SpoutOutput::send(unsigned int, int, int)
{
    if (!impl_->warned) {
        impl_->warned = true;
        bslplat::reportUnavailable("Spout");
    }
    return false;
}

void SpoutOutput::release() {}

}
