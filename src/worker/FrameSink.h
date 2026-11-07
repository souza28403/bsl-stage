#pragma once

#include "worker/PipeChannel.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

struct ID3D11Texture2D;

namespace bsl::worker {

class FrameSink
{
public:
    virtual ~FrameSink() = default;

    virtual const char* kind() const = 0;
    virtual bool resize(int w, int h) = 0;
    virtual bool submitBgra(const std::uint8_t* bgra, int stride, bool premultiplied, int page) = 0;
    virtual bool submitSharedHandle(HANDLE sharedHandle, int page, std::string* error)
    {
        (void)sharedHandle;
        (void)page;
        if (error)
            *error = "não suportado";
        return false;
    }
    virtual bool supportsGpuHandle() const { return false; }

    int width() const { return width_; }
    int height() const { return height_; }

protected:
    int width_ = 0;
    int height_ = 0;
};

struct SinkRequest
{
    PipeChannel* channel = nullptr;
    DWORD appPid = 0;
    bool haveLuid = false;
    LONG luidHigh = 0;
    DWORD luidLow = 0;
    std::string transport = "auto";
};

std::unique_ptr<FrameSink> createFrameSink(const SinkRequest& request);
std::unique_ptr<FrameSink> createShmSink(const SinkRequest& request);

}
