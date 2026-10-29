#pragma once

// Chamadas protegidas por SEH ao plugMain de um plugin FFGL. Fica isolado em FfglCall.cpp (sem nenhum objeto C++
// com destrutor nas funcoes com __try), para que access violation, divisao por zero, excecao C++ nao tratada etc.
// dentro do plugin virem so um codigo de excecao. Nada aqui inclui o cabecalho do FFGL.

#include <cstddef>
#include <cstdint>

namespace bsl::ffgl {

// Chama fn(code, in, instance), onde fn e o plugMain. 'in' e 'out' sao a union FFMixed (8 bytes em x64).
// true = voltou normalmente; false = excecao estruturada, com o codigo em *exception.
bool rawPlugMain(void* fn, std::uint32_t code, std::uintptr_t in, void* instance, std::uintptr_t* out,
                 unsigned long* exception);

// Copia 'n' bytes de memoria do plugin sob SEH
bool rawCopy(const void* src, void* dst, std::size_t n, unsigned long* exception);

// Copia uma string C do plugin (ate cap-1 caracteres + terminador) sob SEH
bool rawCString(const char* src, char* dst, std::size_t cap, unsigned long* exception);

// SetLogCallback(cb) do plugin (opcional) sob SEH
bool rawSetLogCallback(void* fn, void* callback, unsigned long* exception);

} // namespace bsl::ffgl
