#include "render/D3D11Interop.h"

#include "platform/Unavailable.h"

namespace bsl::render {

#if !BSL_WITH_D3D

InteropTexture::~InteropTexture() = default;

D3D11Interop::~D3D11Interop() = default;

std::shared_ptr<D3D11Interop> D3D11Interop::create(std::string* error)
{
    if (error)
        *error = bslplat::unavailableText("Direct3D 11");
    return nullptr;
}

std::shared_ptr<D3D11Interop> D3D11Interop::shared(std::string* error)
{
    return create(error);
}

std::string D3D11Interop::describe() const
{
    return bslplat::unavailableText("Direct3D 11");
}

std::unique_ptr<InteropTexture> D3D11Interop::createTexture(int, int, bool, int, bool)
{
    return nullptr;
}

#endif

}
