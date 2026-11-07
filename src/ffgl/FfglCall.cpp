#include "ffgl/FfglCall.h"

#include <windows.h>

namespace bsl::ffgl {

namespace {

union Mixed
{
    std::uint32_t u32;
    void* ptr;
    std::uintptr_t bits;
};

using PlugMainFn = Mixed(__stdcall*)(std::uint32_t, Mixed, void*);

}

bool rawPlugMain(void* fn, std::uint32_t code, std::uintptr_t in, void* instance, std::uintptr_t* out,
                 unsigned long* exception)
{
    Mixed arg;
    arg.bits = in;
    Mixed ret;
    ret.bits = 0;
    __try {
        ret = reinterpret_cast<PlugMainFn>(fn)(code, arg, instance);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (exception)
            *exception = GetExceptionCode();
        return false;
    }
    if (out)
        *out = ret.bits;
    return true;
}

bool rawCopy(const void* src, void* dst, std::size_t n, unsigned long* exception)
{
    __try {
        const volatile unsigned char* s = static_cast<const volatile unsigned char*>(src);
        unsigned char* d = static_cast<unsigned char*>(dst);
        for (std::size_t i = 0; i < n; ++i)
            d[i] = s[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (exception)
            *exception = GetExceptionCode();
        return false;
    }
    return true;
}

bool rawCString(const char* src, char* dst, std::size_t cap, unsigned long* exception)
{
    if (cap == 0)
        return true;
    std::size_t i = 0;
    __try {
        const volatile char* s = src;
        for (; i + 1 < cap; ++i) {
            const char c = s[i];
            dst[i] = c;
            if (c == '\0')
                return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (exception)
            *exception = GetExceptionCode();
        dst[i] = '\0';
        return false;
    }
    dst[cap - 1] = '\0';
    return true;
}

bool rawSetLogCallback(void* fn, void* callback, unsigned long* exception)
{
    __try {
        reinterpret_cast<void(__stdcall*)(void*)>(fn)(callback);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (exception)
            *exception = GetExceptionCode();
        return false;
    }
    return true;
}

}
