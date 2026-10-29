#pragma once

#include "fx/EffectChain.h"

namespace bsl::fx {

// Aplica uma cadeia de efeitos a uma textura w x h. Efeitos em bypass sao pulados e cadeia vazia (ou toda em bypass)
// nao custa nada: devolve um Lease vazio e quem chamou segue com a textura de entrada. Os FBOs vem do pool de
// 'ctx' (ping-pong: no maximo dois FBOs de tamanho w x h em uso por vez).
//
// O resultado (Lease) precisa ficar vivo enquanto a textura for lida; ao soltar, o FBO volta ao pool.
// Cada efeito pode mexer em qualquer estado do GL: ao fim, o estado basico (programa, framebuffer, unidade 0) e restaurado.
class ChainRunner
{
public:
    static FboPool::Lease run(EffectChain& chain, const EffectContext& ctx, GLuint inputTex, int w, int h);
};

} // namespace bsl::fx
