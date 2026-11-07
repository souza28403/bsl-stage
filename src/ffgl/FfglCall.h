#pragma once

#include <cstddef>
#include <cstdint>

namespace bsl::ffgl {

bool rawPlugMain(void* fn, std::uint32_t code, std::uintptr_t in, void* instance, std::uintptr_t* out,
                 unsigned long* exception);

bool rawCopy(const void* src, void* dst, std::size_t n, unsigned long* exception);

bool rawCString(const char* src, char* dst, std::size_t cap, unsigned long* exception);

bool rawSetLogCallback(void* fn, void* callback, unsigned long* exception);

}
